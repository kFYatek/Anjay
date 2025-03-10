/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_TEST_DM_H
#define ANJAY_TEST_DM_H

#include <avsystem/commons/avs_unit_mocksock.h>

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_raw_buffer.h>

#include "tests/core/coap/utils.h"
#include "tests/utils/mock_clock.h"
#include "tests/utils/mock_dm.h"

anjay_t *_anjay_test_dm_init(const anjay_configuration_t *config);

void _anjay_test_dm_unsched_notify_clb(anjay_t *anjay);

void _anjay_test_dm_unsched_reload_sockets(anjay_t *anjay);

avs_net_socket_t *_anjay_test_dm_create_socket(bool connected);

avs_net_socket_t *_anjay_test_dm_install_socket(anjay_t *anjay,
                                                anjay_ssid_t ssid);
void _anjay_test_dm_finish(anjay_t *anjay);

int _anjay_test_dm_fake_security_list_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_dm_list_ctx_t *ctx);

int _anjay_test_dm_fake_security_list_resources(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_dm_resource_list_ctx_t *ctx);

int _anjay_test_dm_fake_security_read(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_output_ctx_t *ctx);

static inline int
_anjay_test_dm_instance_reset_NOOP(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    return 0;
}

static const anjay_dm_object_def_t *const OBJ = &(const anjay_dm_object_def_t) {
    .oid = 42,
    .handlers = { ANJAY_MOCK_DM_HANDLERS,
                  .instance_reset = _anjay_test_dm_instance_reset_NOOP }
};

static const anjay_dm_object_def_t *const OBJ_NOATTRS =
        &(const anjay_dm_object_def_t) {
            .oid = 93,
            .handlers = { ANJAY_MOCK_DM_HANDLERS_BASIC,
                          .instance_reset = _anjay_test_dm_instance_reset_NOOP }
        };

static const anjay_dm_object_def_t *const OBJ_WITH_RESET =
        &(const anjay_dm_object_def_t) {
            .oid = 25,
            .handlers = { ANJAY_MOCK_DM_HANDLERS,
                          .instance_reset = _anjay_mock_dm_instance_reset }
        };

static const anjay_dm_object_def_t *const OBJ_WITH_TRANSACTION = &(
        const anjay_dm_object_def_t) {
    .oid = 69,
    .handlers = { ANJAY_MOCK_DM_HANDLERS_BASIC, ANJAY_MOCK_DM_HANDLERS_REST,
                  ANJAY_MOCK_DM_HANDLERS_TRANSACTION,
                  .instance_reset = _anjay_test_dm_instance_reset_NOOP }
};

static anjay_dm_object_def_t *const EXECUTE_OBJ = &(anjay_dm_object_def_t) {
    .oid = 128,
    .handlers = { ANJAY_MOCK_DM_HANDLERS }
};

static const anjay_dm_object_def_t *const FAKE_SECURITY =
        &(const anjay_dm_object_def_t) {
            .oid = 0,
            .handlers = {
                .list_instances = _anjay_test_dm_fake_security_list_instances,
                .list_resources = _anjay_test_dm_fake_security_list_resources,
                .resource_read = _anjay_test_dm_fake_security_read,
                ANJAY_MOCK_DM_HANDLERS_TRANSACTION_NOOP
            }
        };

static const anjay_dm_object_def_t *const FAKE_SECURITY2 =
        &(const anjay_dm_object_def_t) {
            .oid = 0,
            .handlers = { ANJAY_MOCK_DM_HANDLERS }
        };

static const anjay_dm_object_def_t *const FAKE_SERVER =
        &(const anjay_dm_object_def_t) {
            .oid = 1,
            .handlers = { ANJAY_MOCK_DM_HANDLERS }
        };

#define DM_TEST_CONFIGURATION(...)                \
    &(anjay_configuration_t) {                    \
        .endpoint_name = "urn:dev:os:anjay-test", \
        .in_buffer_size = 4096,                   \
        .out_buffer_size = 4096, __VA_ARGS__      \
    }

#define DM_TEST_INIT_OBJECTS__(ObjDefs, ...)                        \
    reset_token_generator();                                        \
    anjay_t *anjay = _anjay_test_dm_init((__VA_ARGS__));            \
    do {                                                            \
        for (size_t _i = 0; _i < AVS_ARRAY_SIZE((ObjDefs)); ++_i) { \
            AVS_UNIT_ASSERT_SUCCESS(                                \
                    anjay_register_object(anjay, (ObjDefs)[_i]));   \
        }                                                           \
    } while (false)

#define DM_TEST_POST_INIT__                                                   \
    _anjay_test_dm_unsched_notify_clb(anjay);                                 \
    AVS_UNIT_ASSERT_EQUAL(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX), \
                          INT_MAX)

#define DM_TEST_INIT_GENERIC(ObjDefs, Ssids, ...)                          \
    DM_TEST_INIT_OBJECTS__(ObjDefs, __VA_ARGS__);                          \
    avs_net_socket_t *mocksocks[AVS_ARRAY_SIZE((Ssids))];                  \
    for (size_t _i = AVS_ARRAY_SIZE((Ssids)) - 1;                          \
         _i < AVS_ARRAY_SIZE((Ssids));                                     \
         --_i) {                                                           \
        mocksocks[_i] = _anjay_test_dm_install_socket(anjay, (Ssids)[_i]); \
    }                                                                      \
    (void) mocksocks;                                                      \
    DM_TEST_POST_INIT__

#define DM_TEST_DEFAULT_OBJECTS                                  \
    &OBJ, &FAKE_SECURITY, &FAKE_SERVER,                          \
            (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, \
            (const anjay_dm_object_def_t *const *) &OBJ_WITH_RESET

#define DM_TEST_INIT_WITH_OBJECTS(...)                                \
    const anjay_dm_object_def_t *const *obj_defs[] = { __VA_ARGS__ }; \
    anjay_ssid_t ssids[] = { 1 };                                     \
    DM_TEST_INIT_GENERIC(obj_defs, ssids, DM_TEST_CONFIGURATION())

#define DM_TEST_INIT_WITH_SSIDS(...)                   \
    const anjay_dm_object_def_t *const *obj_defs[] = { \
        DM_TEST_DEFAULT_OBJECTS                        \
    };                                                 \
    anjay_ssid_t ssids[] = { __VA_ARGS__ };            \
    DM_TEST_INIT_GENERIC(obj_defs, ssids, DM_TEST_CONFIGURATION())

#define DM_TEST_INIT_WITHOUT_SERVER                            \
    const anjay_dm_object_def_t *const *obj_defs[] = {         \
        DM_TEST_DEFAULT_OBJECTS                                \
    };                                                         \
    DM_TEST_INIT_OBJECTS__(obj_defs, DM_TEST_CONFIGURATION()); \
    DM_TEST_POST_INIT__

#define DM_TEST_INIT DM_TEST_INIT_WITH_SSIDS(1)

#define DM_TEST_INIT_WITH_CONFIG(...)                  \
    const anjay_dm_object_def_t *const *obj_defs[] = { \
        DM_TEST_DEFAULT_OBJECTS                        \
    };                                                 \
    anjay_ssid_t ssids[] = { 1 };                      \
    DM_TEST_INIT_GENERIC(obj_defs, ssids, DM_TEST_CONFIGURATION(__VA_ARGS__))

#define DM_TEST_FINISH _anjay_test_dm_finish(anjay)

#define DM_TEST_EXPECT_RESPONSE(                                \
        Mocksock, Type, Code, Id, ... /* Payload, Opts... */)   \
    do {                                                        \
        const coap_test_msg_t *response =                       \
                COAP_MSG(Type, Code, Id, __VA_ARGS__);          \
        avs_unit_mocksock_expect_output(                        \
                Mocksock, response->content, response->length); \
    } while (0)

#define DM_TEST_REQUEST_FROM_CLIENT DM_TEST_EXPECT_RESPONSE

#define DM_TEST_REQUEST(Mocksock, Type, Code, Id, ... /* Payload, Opts... */) \
    do {                                                                      \
        const coap_test_msg_t *request =                                      \
                COAP_MSG(Type, Code, Id, __VA_ARGS__);                        \
        avs_unit_mocksock_input(Mocksock, request->content, request->length); \
    } while (0)

#define DM_TEST_EXPECT_READ_NULL_ATTRS(Ssid, Iid, Rid)                     \
    do {                                                                   \
        _anjay_mock_dm_expect_list_instances(                              \
                anjay,                                                     \
                &OBJ,                                                      \
                0,                                                         \
                (const anjay_iid_t[]) { Iid, ANJAY_ID_INVALID });          \
        if (Rid >= 0) {                                                    \
            _anjay_mock_dm_expect_list_resources(                          \
                    anjay,                                                 \
                    &OBJ,                                                  \
                    Iid,                                                   \
                    0,                                                     \
                    (const anjay_mock_dm_res_entry_t[]) {                  \
                            { 0, ANJAY_DM_RES_RW, Rid == 0 },              \
                            { 1, ANJAY_DM_RES_RW, Rid == 1 },              \
                            { 2, ANJAY_DM_RES_RW, Rid == 2 },              \
                            { 3, ANJAY_DM_RES_RW, Rid == 3 },              \
                            { 4, ANJAY_DM_RES_RW, Rid == 4 },              \
                            { 5, ANJAY_DM_RES_RW, Rid == 5 },              \
                            { 6, ANJAY_DM_RES_RW, Rid == 6 },              \
                            ANJAY_MOCK_DM_RES_END });                      \
            _anjay_mock_dm_expect_resource_read_attrs(                     \
                    anjay,                                                 \
                    &OBJ,                                                  \
                    Iid,                                                   \
                    (anjay_rid_t) Rid,                                     \
                    Ssid,                                                  \
                    0,                                                     \
                    &ANJAY_DM_R_ATTRIBUTES_EMPTY);                         \
        }                                                                  \
        _anjay_mock_dm_expect_instance_read_default_attrs(                 \
                anjay, &OBJ, Iid, Ssid, 0, &ANJAY_DM_OI_ATTRIBUTES_EMPTY); \
        _anjay_mock_dm_expect_object_read_default_attrs(                   \
                anjay, &OBJ, Ssid, 0, &ANJAY_DM_OI_ATTRIBUTES_EMPTY);      \
        _anjay_mock_dm_expect_list_instances(anjay,                        \
                                             &FAKE_SERVER,                 \
                                             0,                            \
                                             (const anjay_iid_t[]) {       \
                                                     ANJAY_ID_INVALID });  \
    } while (0)

#endif /* ANJAY_TEST_DM_H */
