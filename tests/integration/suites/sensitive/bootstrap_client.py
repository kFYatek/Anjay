# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.


from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *
from framework import test_suite

from suites.default.bootstrap_client import BootstrapTest


class BootstrapIncorrectData(BootstrapTest.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self, **kwargs):
        super().setUp(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
                      num_servers_passed=0, **kwargs)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def test_bootstrap_backoff(self, num_attempts):
        # NOTE: mbed TLS (hence, the demo client) sends Client Key Exchange, Change Cipher Spec and Encrypted Handshake
        # Message as three separate UDP packets, but as three send() calls without any recv() attempts between them.
        # On the server side (i.e., in this test code), the fatal Alert ("Unknown PSK identity" in this case) is sent
        # after receiving Client Key Exchange. Depending on the timing of processing on both endpoints, it may be the
        # case that the Alert is sent by the server before the Change Cipher Spec and Encrypted Handshake are even
        # generated by the client. In that case, on the server we'd get "handshake failed" exception TWICE - once due to
        # the actual error, and then second time - because mbed TLS will attempt to interpret the Change Cipher Spec
        # datagram as a "malformed Client Hello". So we need to somehow discard the packets on the server side. We
        # cannot "fake-close" the socket after failed handshake, as that causes ICMP unreachable packets to be
        # generated, which in turn causes the client to restart the handshake. So we instead do this convoluted flow of
        # calling reset() just before Bootstrap Finish, so that we're absolutely sure that all leftover messages are
        # discarded just before we get the new Client Hello.

        holdoff_s = 0
        last_time = time.time()
        for attempt in range(num_attempts):
            # Create Security Object instance with deliberately wrong keys
            self.perform_typical_bootstrap(server_iid=1,
                                           security_iid=2,
                                           server_uri='coaps://127.0.0.1:%d' % self.serv.get_listen_port(),
                                           secure_identity=self.PSK_IDENTITY + b'hurr',
                                           secure_key=self.PSK_KEY + b'durr',
                                           security_mode=SecurityMode.PreSharedKey,
                                           finish=False,
                                           holdoff_s=max(last_time + holdoff_s - time.time(), 0))
            last_time = time.time()

            self.serv.reset()
            self.perform_bootstrap_finish()
            with self.assertRaisesRegex(RuntimeError, 'handshake failed'):
                self.serv.recv()

            holdoff_s = min(max(2 * holdoff_s, 3), 120)

    def runTest(self):
        self.test_bootstrap_backoff(3)

        # now bootstrap the right keys
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       secure_identity=self.PSK_IDENTITY,
                                       secure_key=self.PSK_KEY,
                                       security_mode=SecurityMode.PreSharedKey,
                                       finish=False,
                                       holdoff_s=12)

        self.serv.reset()
        self.perform_bootstrap_finish()
        self.assertDemoRegisters()

        # Trigger update
        self.communicate('send-update')
        update_pkt = self.assertDemoUpdatesRegistration(
            self.serv, respond=False)
        # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
        self.serv.send(Lwm2mErrorResponse.matching(
            update_pkt)(code=coap.Code.RES_BAD_REQUEST))
        # This should cause client attempt to re-register.
        register_pkt = self.assertDemoRegisters(self.serv, respond=False)
        # To which we respond with 4.03 Forbidden, finishing off the communication.
        self.serv.send(Lwm2mErrorResponse.matching(
            register_pkt)(code=coap.Code.RES_FORBIDDEN))

        # check that bootstrap backoff is restarted
        self.test_bootstrap_backoff(2)


class ClientInitiatedBootstrapOnlyWithIncorrectData(BootstrapIncorrectData):
    def setUp(self):
        super().setUp(legacy_server_initiated_bootstrap_allowed=False)
