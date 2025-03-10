# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *


class UpdateTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # should send a correct Update
        self.communicate('send-update')
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, query=[], content=b''), pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())

        # should not send more messages after receiving a correct response
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))

        # should automatically send Updates before lifetime expires
        LIFETIME = 2

        self.serv.send(Lwm2mWrite(ResPath.Server[1].Lifetime, str(LIFETIME)))
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), pkt)
        self.assertDemoUpdatesRegistration(lifetime=LIFETIME)

        # wait for auto-scheduled Update
        self.assertDemoUpdatesRegistration(timeout_s=LIFETIME)


class UpdateServerDownReconnectTest(test_suite.PcapEnabledTest, test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # respond with Port Unreachable to the next packet
        with self.serv.fake_close():
            self.communicate('send-update')
            self.wait_until_icmp_unreachable_count(1, timeout_s=10)

        # client should abort
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)
        self.assertEqual(self.get_socket_count(), 0)

    def tearDown(self):
        super().tearDown(auto_deregister=False)
        self.assertEqual(self.count_icmp_unreachable_packets(), 1)


class ReconnectTest(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.communicate('reconnect')

        # server is connected, so only a packet from the same remote port will pass this assertion
        self.assertDtlsReconnect()


class UpdateFallbacksToRegisterAfterLifetimeExpiresTest(test_suite.Lwm2mSingleServerTest):
    LIFETIME = 4

    def setUp(self):
        super().setUp(auto_register=False, lifetime=self.LIFETIME,
                      extra_cmdline_args=['--ack-random-factor', '1', '--ack-timeout', '1',
                                          '--max-retransmit', '1'])
        self.assertDemoRegisters(lifetime=self.LIFETIME)

    def runTest(self):
        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1)

        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1, respond=False)
        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1, respond=False)
        self.assertDemoRegisters(lifetime=self.LIFETIME, timeout_s=self.LIFETIME / 2 + 1)


class UpdateFallbacksToRegisterAfterCoapClientErrorResponse(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        def check(code: coap.Code):
            self.communicate('send-update')

            req = self.serv.recv()
            self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''), req)
            self.serv.send(Lwm2mErrorResponse.matching(req)(code))

            self.assertDemoRegisters()

        # check all possible client (4.xx) errors
        for detail in range(32):
            if detail == 13:
                # TODO: do not ignore Request Entity Too Large (T2171)
                continue
            check(coap.Code(4, detail))


class ReconnectFailsWithCoapErrorCodeTest(test_suite.Lwm2mSingleServerTest):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,)),
                            pkt)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_INTERNAL_SERVER_ERROR))

        # client should abort
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=10)
        self.assertEqual(self.get_socket_count(), 0)


class ReconnectFailsWithConnectionRefusedTest(test_suite.Lwm2mDtlsSingleServerTest,
                                              test_suite.Lwm2mDmOperations):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # should try to resume DTLS session
        with self.serv.fake_close():
            self.communicate('reconnect')

            # give the process some time to fail
            time.sleep(1)

        # client should abort
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)
        self.assertEqual(self.get_socket_count(), 0)


class ConcurrentRequestWhileWaitingForResponse(test_suite.Lwm2mSingleServerTest,
                                               test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.communicate('send-update')

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, query=[], content=b''), pkt)

        self.read_path(self.serv, ResPath.Device.Manufacturer)

        self.serv.send(Lwm2mChanged.matching(pkt)())


class UpdateAfterLifetimeChange(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mWrite(ResPath.Server[1].Lifetime, b'5')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertDemoUpdatesRegistration(lifetime=5)
        # Next update should be there shortly
        self.assertDemoUpdatesRegistration(timeout_s=5)

        req = Lwm2mWrite(ResPath.Server[1].Lifetime, b'86400')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertDemoUpdatesRegistration(lifetime=86400)


class NoUpdateDuringShutdownTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate(
            'schedule-update-on-exit')  # tearDown() expects a De-Register operation and will fail on  # unexpected Update


class ExternalSetLifetimeForcesUpdate(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate('set-lifetime 1 9001')
        self.assertDemoUpdatesRegistration(lifetime=9001)


class ExternalSetLifetimeForcesUpdateOnlyIfChanged(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # default lifetime is 86400 or so, so we should not have any updates
        self.communicate('set-lifetime 1 86400')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)


class UpdateImmediatelyDisabledTest(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create_instance(self.serv, OID.Test, iid=1)
        # no Update message expected in this case
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)


class UpdateImmediatelyEnabledTest(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--update-immediately-on-dm-change'])

    def runTest(self):
        self.create_instance(self.serv, OID.Test, iid=42)
        # Update message shall be automatically generated
        pkt = self.assertDemoUpdatesRegistration(self.serv, content=ANY)
        self.assertIn(f'</{OID.Test}/42>'.encode(), pkt.content)
