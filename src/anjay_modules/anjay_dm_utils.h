/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_H

#ifdef ANJAY_WITH_SEND
#    include <anjay/lwm2m_send.h>
#endif // ANJAY_WITH_SEND

#include <anjay_modules/dm/anjay_modules.h>

#include <assert.h>
#include <limits.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

// NOTE: A lot of code depends on numerical values of these constants.
// Please be careful when refactoring.
typedef enum {
    ANJAY_ID_OID,
    ANJAY_ID_IID,
    ANJAY_ID_RID,
    ANJAY_ID_RIID,
    _ANJAY_URI_PATH_MAX_LENGTH
} anjay_id_type_t;

/**
 * A data type that represents a data model path.
 *
 * It may represent a root path, an Object path, an Object Instance path, a
 * Resource path, or a Resource Instance path.
 *
 * The path is terminated either by an @ref ANJAY_ID_INVALID value, or
 * end-of-array (in case of Resource Instance paths). In case of root, Object
 * and Object Instance paths, the array elements past the terminating invalid ID
 * value are undefined and shall not be used. They are NOT required to be set to
 * @ref ANJAY_ID_INVALID. Paths object that numerically differ only in values
 * past the terminating invalid ID shall be treated as equal (and this is how
 * @ref _anjay_uri_path_equal is implemented).
 *
 * The <c>ids</c> array is designed to be safely and meaningfully indexed by
 * @ref anjay_id_type_t values.
 */
typedef struct {
    uint16_t ids[_ANJAY_URI_PATH_MAX_LENGTH];
} anjay_uri_path_t;

static inline size_t _anjay_uri_path_length(const anjay_uri_path_t *path) {
    size_t result;
    for (result = 0; result < AVS_ARRAY_SIZE(path->ids); ++result) {
        if (path->ids[result] == ANJAY_ID_INVALID) {
            break;
        }
    }
    return result;
}

static inline bool _anjay_uri_path_has(const anjay_uri_path_t *path,
                                       anjay_id_type_t id_type) {
    return _anjay_uri_path_length(path) > id_type;
}

static inline bool _anjay_uri_path_leaf_is(const anjay_uri_path_t *path,
                                           anjay_id_type_t id_type) {
    return _anjay_uri_path_length(path) == (size_t) id_type + 1u;
}

static inline int _anjay_uri_path_compare(const anjay_uri_path_t *left,
                                          const anjay_uri_path_t *right) {
    for (size_t i = 0; i < AVS_ARRAY_SIZE(left->ids); ++i) {
        if (left->ids[i] < right->ids[i]) {
            return -1;
        } else if (left->ids[i] > right->ids[i]) {
            return 1;
        } else if (left->ids[i] == ANJAY_ID_INVALID) {
            break;
        }
    }
    return 0;
}

static inline bool _anjay_uri_path_equal(const anjay_uri_path_t *left,
                                         const anjay_uri_path_t *right) {
    return _anjay_uri_path_compare(left, right) == 0;
}

static inline bool _anjay_uri_path_outside_base(const anjay_uri_path_t *path,
                                                const anjay_uri_path_t *base) {
    for (size_t i = 0; i < AVS_ARRAY_SIZE(base->ids); ++i) {
        if (base->ids[i] == ANJAY_ID_INVALID) {
            // base is no longer than path, previous IDs validated
            return false;
        } else if (path->ids[i] != base->ids[i]) {
            // path is shorter than base (path->ids[i] == ANJAY_ID_INVALID)
            // or IDs differ
            return true;
        }
    }
    return false;
}

/**
 * Returns true if array of ids can be splitted into two consistent parts:
 * - valid ids from the beginning
 * - ANJAY_INVALID_ID from the end
 */
static inline bool _anjay_uri_path_normalized(const anjay_uri_path_t *path) {
    for (size_t i = _anjay_uri_path_length(path) + 1;
         i < AVS_ARRAY_SIZE(path->ids);
         ++i) {
        if (path->ids[i] != ANJAY_ID_INVALID) {
            return false;
        }
    }
    return true;
}

#ifdef ANJAY_WITH_LWM2M11
void _anjay_uri_path_update_common_prefix(const anjay_uri_path_t **prefix_ptr,
                                          anjay_uri_path_t *prefix_buf,
                                          const anjay_uri_path_t *path);
#endif // ANJAY_WITH_LWM2M11

const char *_anjay_debug_make_path__(char *buffer,
                                     size_t buffer_size,
                                     const anjay_uri_path_t *uri);

#define ANJAY_DEBUG_MAKE_PATH(path) \
    (_anjay_debug_make_path__(&(char[32]){ 0 }[0], 32, (path)))

#define URI_PATH_INITIALIZER(Oid, Iid, Rid, Riid) \
    {                                             \
        .ids = {                                  \
            [ANJAY_ID_OID] = (Oid),               \
            [ANJAY_ID_IID] = (Iid),               \
            [ANJAY_ID_RID] = (Rid),               \
            [ANJAY_ID_RIID] = (Riid)              \
        }                                         \
    }

#define RESOURCE_INSTANCE_PATH_INITIALIZER(Oid, Iid, Rid, Riid) \
    URI_PATH_INITIALIZER(Oid, Iid, Rid, Riid)

#define RESOURCE_PATH_INITIALIZER(Oid, Iid, Rid) \
    URI_PATH_INITIALIZER(Oid, Iid, Rid, ANJAY_ID_INVALID)

#define INSTANCE_PATH_INITIALIZER(Oid, Iid) \
    URI_PATH_INITIALIZER(Oid, Iid, ANJAY_ID_INVALID, ANJAY_ID_INVALID)

#define OBJECT_PATH_INITIALIZER(Oid)                              \
    URI_PATH_INITIALIZER(Oid, ANJAY_ID_INVALID, ANJAY_ID_INVALID, \
                         ANJAY_ID_INVALID)

#define ROOT_PATH_INITIALIZER()                                                \
    URI_PATH_INITIALIZER(ANJAY_ID_INVALID, ANJAY_ID_INVALID, ANJAY_ID_INVALID, \
                         ANJAY_ID_INVALID)

#define MAKE_URI_PATH(...) \
    ((const anjay_uri_path_t) URI_PATH_INITIALIZER(__VA_ARGS__))

#define MAKE_RESOURCE_INSTANCE_PATH(Oid, Iid, Rid, Riid) \
    MAKE_URI_PATH(Oid, Iid, Rid, Riid)

#define MAKE_RESOURCE_PATH(Oid, Iid, Rid) \
    MAKE_URI_PATH(Oid, Iid, Rid, ANJAY_ID_INVALID)

#define MAKE_INSTANCE_PATH(Oid, Iid) \
    MAKE_URI_PATH(Oid, Iid, ANJAY_ID_INVALID, ANJAY_ID_INVALID)

#define MAKE_OBJECT_PATH(Oid) \
    MAKE_URI_PATH(Oid, ANJAY_ID_INVALID, ANJAY_ID_INVALID, ANJAY_ID_INVALID)

#define MAKE_ROOT_PATH()                                                \
    MAKE_URI_PATH(ANJAY_ID_INVALID, ANJAY_ID_INVALID, ANJAY_ID_INVALID, \
                  ANJAY_ID_INVALID)

typedef enum anjay_request_action {
    ANJAY_ACTION_READ,
#ifdef ANJAY_WITH_LWM2M11
    ANJAY_ACTION_READ_COMPOSITE,
#endif // ANJAY_WITH_LWM2M11
    ANJAY_ACTION_DISCOVER,
    ANJAY_ACTION_WRITE,
#ifdef ANJAY_WITH_LWM2M11
    ANJAY_ACTION_WRITE_COMPOSITE,
#endif // ANJAY_WITH_LWM2M11
    ANJAY_ACTION_WRITE_UPDATE,
    ANJAY_ACTION_WRITE_ATTRIBUTES,
    ANJAY_ACTION_EXECUTE,
    ANJAY_ACTION_CREATE,
    ANJAY_ACTION_DELETE,
    ANJAY_ACTION_BOOTSTRAP_FINISH
} anjay_request_action_t;

typedef enum {
    ANJAY_DM_WRITE_TYPE_INVALID = -1,
    ANJAY_DM_WRITE_TYPE_UPDATE,
    ANJAY_DM_WRITE_TYPE_REPLACE
} anjay_dm_write_type_t;

static inline anjay_dm_write_type_t _anjay_dm_write_type_from_request_action(
        anjay_request_action_t request_action) {
    switch (request_action) {
    case ANJAY_ACTION_WRITE:
        return ANJAY_DM_WRITE_TYPE_REPLACE;
    case ANJAY_ACTION_WRITE_UPDATE:
    case ANJAY_ACTION_CREATE:
#ifdef ANJAY_WITH_LWM2M11
    case ANJAY_ACTION_WRITE_COMPOSITE:
#endif // ANJAY_WITH_LWM2M11
        return ANJAY_DM_WRITE_TYPE_UPDATE;
    default:
        AVS_UNREACHABLE("Unexpected request action");
        return ANJAY_DM_WRITE_TYPE_INVALID;
    }
}

#ifdef ANJAY_WITH_THREAD_SAFETY

struct anjay_unlocked_dm_object_def_struct {
    anjay_oid_t oid;
    const char *version;
    anjay_unlocked_dm_handlers_t handlers;
};

#endif // ANJAY_WITH_THREAD_SAFETY

int _anjay_dm_read_resource_into_ctx(anjay_unlocked_t *anjay,
                                     const anjay_uri_path_t *path,
                                     anjay_unlocked_output_ctx_t *ctx);

int _anjay_dm_read_resource_into_stream(anjay_unlocked_t *anjay,
                                        const anjay_uri_path_t *path,
                                        avs_stream_t *stream);

int _anjay_dm_read_resource_into_buffer(anjay_unlocked_t *anjay,
                                        const anjay_uri_path_t *path,
                                        char *buffer,
                                        size_t buffer_size,
                                        size_t *out_bytes_read);

static inline int _anjay_dm_read_resource_string(anjay_unlocked_t *anjay,
                                                 const anjay_uri_path_t *path,
                                                 char *buffer,
                                                 size_t buffer_size) {
    assert(buffer && buffer_size > 0);
    size_t bytes_read;
    int result =
            _anjay_dm_read_resource_into_buffer(anjay, path, buffer,
                                                buffer_size - 1, &bytes_read);
    if (!result) {
        buffer[bytes_read] = '\0';
    }
    return result;
}

static inline int _anjay_dm_read_resource_i64(anjay_unlocked_t *anjay,
                                              const anjay_uri_path_t *path,
                                              int64_t *out_value) {
    size_t bytes_read;
    int result = _anjay_dm_read_resource_into_buffer(
            anjay, path, (char *) out_value, sizeof(*out_value), &bytes_read);
    if (result) {
        return result;
    }
    return bytes_read != sizeof(*out_value);
}

#ifdef ANJAY_WITH_LWM2M11
static inline int _anjay_dm_read_resource_u64(anjay_unlocked_t *anjay,
                                              const anjay_uri_path_t *path,
                                              uint64_t *out_value) {
    size_t bytes_read;
    int result = _anjay_dm_read_resource_into_buffer(
            anjay, path, (char *) out_value, sizeof(*out_value), &bytes_read);
    if (result) {
        return result;
    }
    return bytes_read != sizeof(*out_value);
}
#endif // ANJAY_WITH_LWM2M11

static inline int _anjay_dm_read_resource_u16(anjay_unlocked_t *anjay,
                                              const anjay_uri_path_t *path,
                                              uint16_t *out_value) {
    int64_t value;
    int result = _anjay_dm_read_resource_i64(anjay, path, &value);
    if (result) {
        return result;
    }
    if (value < 0 || value >= UINT16_MAX) {
        return -1;
    }
    *out_value = (uint16_t) value;
    return 0;
}

static inline int _anjay_dm_read_resource_bool(anjay_unlocked_t *anjay,
                                               const anjay_uri_path_t *path,
                                               bool *out_value) {
    size_t bytes_read;
    int result = _anjay_dm_read_resource_into_buffer(
            anjay, path, (char *) out_value, sizeof(*out_value), &bytes_read);
    if (result) {
        return result;
    }
    return bytes_read != sizeof(*out_value);
}

static inline int _anjay_dm_read_resource_objlnk(anjay_unlocked_t *anjay,
                                                 const anjay_uri_path_t *path,
                                                 anjay_oid_t *out_oid,
                                                 anjay_iid_t *out_iid) {
    size_t bytes_read;
    uint32_t objlnk_encoded;
    int result = _anjay_dm_read_resource_into_buffer(anjay, path,
                                                     (char *) &objlnk_encoded,
                                                     sizeof(objlnk_encoded),
                                                     &bytes_read);
    if (result) {
        return result;
    }
    if (bytes_read != sizeof(objlnk_encoded)) {
        return -1;
    }
    *out_iid = (anjay_iid_t) (objlnk_encoded & 0xFFFF);
    *out_oid = (anjay_oid_t) (objlnk_encoded >> 16);
    return 0;
}

#ifdef ANJAY_WITH_LWM2M11
/**
 * Reads an array of uint32_t values from the data model and returns them via an
 * array allocated using @ref avs_malloc .
 *
 * @param      anjay                   Anjay object to operate on.
 * @param      path                    Resource path to pull data from.
 * @param[out] out_array               Pointer to a variable that on success
 *                                     will be set to a pointer to array of read
 *                                     values. Must not be NULL.
 * @param[out] out_array_size_elements Pointer to a variable that on success
 *                                     will be set to the number of uint32_t
 *                                     values read from data model and available
 *                                     in @p out_array . Must not be NULL.
 *
 * @returns 0 on success, a negative value (one of ANJAY_ERR_* constants) on
 *          error.
 *
 * Notes:
 * - on error, out_array and out_array_size_elements are not modified.
 * - in case 0 elements are read successfully, @p out_array may or may not be
 *   set to a non-NULL value. It is always necessary to call @ref avs_free on
 *   @p out_array whenever this function succeeds.
 */
int _anjay_dm_read_resource_u32_array(anjay_unlocked_t *anjay,
                                      const anjay_uri_path_t *path,
                                      uint32_t **out_array,
                                      size_t *out_array_size_elements);
#endif // ANJAY_WITH_LWM2M11

typedef struct anjay_dm anjay_dm_t;

typedef int
anjay_dm_foreach_object_handler_t(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj,
                                  void *data);

int _anjay_dm_foreach_object(anjay_unlocked_t *anjay,
                             anjay_dm_foreach_object_handler_t *handler,
                             void *data);

typedef int
anjay_dm_foreach_instance_handler_t(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t *obj,
                                    anjay_iid_t iid,
                                    void *data);

int _anjay_dm_foreach_instance(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj,
                               anjay_dm_foreach_instance_handler_t *handler,
                               void *data);

int _anjay_dm_get_sorted_instance_list(anjay_unlocked_t *anjay,
                                       const anjay_dm_installed_object_t *obj,
                                       AVS_LIST(anjay_iid_t) *out);

int _anjay_dm_instance_present(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj_ptr,
                               anjay_iid_t iid);

typedef int
anjay_dm_foreach_resource_handler_t(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t *obj,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_dm_resource_kind_t kind,
                                    anjay_dm_resource_presence_t presence,
                                    void *data);

int _anjay_dm_foreach_resource(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj,
                               anjay_iid_t iid,
                               anjay_dm_foreach_resource_handler_t *handler,
                               void *data);

/**
 * Checks if the specific resource is supported and present, and what is its
 * kind. This function internally calls @ref _anjay_dm_foreach_resource, so it
 * is not optimal to use for multiple resources within the same Object Instance.
 *
 * NOTE: It is REQUIRED that the presence of the Object and Object Instance is
 * checked beforehand, this function does not perform such checks.
 *
 * @param anjay        Anjay object to operate on
 * @param obj_ptr      Definition of an Object in which the queried resource is
 * @param iid          ID of Object Instance in which the queried resource is
 * @param rid          ID of the queried Resource
 * @param out_kind     Pointer to a variable in which the kind of the resource
 *                     will be stored. May be NULL, in which case only the
 *                     presence is checked.
 * @param out_presence Pointer to a variable in which the presence information
 *                     about the resource will be stored. May be NULL, in which
 *                     case only the kind is checked.
 *
 * @returns 0 for success, or a non-zero error code in case of error.
 *
 * NOTE: Two scenarios are possible if the resource is not currently present in
 * the object:
 * - If the resource is not Supported (i.e., it has not been enumerated by the
 *   list_resources handler at all), the function fails, returning
 *   ANJAY_ERR_NOT_FOUND
 * - If the resource is Supported, but not Present (i.e., it has been enumerated
 *   by the list_resources handler with presence set to ANJAY_DM_RES_ABSENT),
 *   the function succeeds (returns 0), but *out_presence is set to
 *   ANJAY_DM_RES_ABSENT
 */
int _anjay_dm_resource_kind_and_presence(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_dm_resource_kind_t *out_kind,
        anjay_dm_resource_presence_t *out_presence);

typedef int anjay_dm_foreach_resource_instance_handler_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        void *data);

int _anjay_dm_foreach_resource_instance(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_dm_foreach_resource_instance_handler_t *handler,
        void *data);

static inline bool _anjay_dm_res_kind_valid(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_R || kind == ANJAY_DM_RES_W
           || kind == ANJAY_DM_RES_RW || kind == ANJAY_DM_RES_RM
           || kind == ANJAY_DM_RES_WM || kind == ANJAY_DM_RES_RWM
           || kind == ANJAY_DM_RES_E || kind == ANJAY_DM_RES_BS_RW;
}

static inline bool
_anjay_dm_res_kind_single_readable(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_R || kind == ANJAY_DM_RES_RW;
}

static inline bool _anjay_dm_res_kind_readable(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_R || kind == ANJAY_DM_RES_RW
           || kind == ANJAY_DM_RES_RM || kind == ANJAY_DM_RES_RWM;
}

static inline bool _anjay_dm_res_kind_writable(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_W || kind == ANJAY_DM_RES_RW
           || kind == ANJAY_DM_RES_WM || kind == ANJAY_DM_RES_RWM;
}

static inline bool
_anjay_dm_res_kind_executable(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_E;
}

static inline bool _anjay_dm_res_kind_multiple(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_RM || kind == ANJAY_DM_RES_WM
           || kind == ANJAY_DM_RES_RWM;
}

static inline bool
_anjay_dm_res_kind_bootstrappable(anjay_dm_resource_kind_t kind) {
    return kind == ANJAY_DM_RES_BS_RW;
}

/**
 * Writes to a resource whose location is determined by the path extracted
 * from Input Context (@p in_ctx). Note that it does NOT check whether the
 * resource is writable - it is enough that it represents a value (i.e. is not
 * an executable resource).
 */
int _anjay_dm_write_resource_and_move_to_next_entry(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_unlocked_input_ctx_t *in_ctx,
        anjay_notify_queue_t *notify_queue);

#ifdef ANJAY_WITH_LWM2M11
/**
 * Works as @ref _anjay_dm_write_resource , but takes value of type @c int64_t
 * instead of input context.
 */
int _anjay_dm_write_resource_i64(anjay_unlocked_t *anjay,
                                 anjay_uri_path_t path,
                                 int64_t value,
                                 anjay_notify_queue_t *notify_queue);

/**
 * Works as @ref _anjay_dm_write_resource , but takes value of type @c uint64_t
 * instead of input context.
 */
int _anjay_dm_write_resource_u64(anjay_unlocked_t *anjay,
                                 anjay_uri_path_t path,
                                 uint64_t value,
                                 anjay_notify_queue_t *notify_queue);
#endif // ANJAY_WITH_LWM2M11

typedef enum {
    ANJAY_DM_HANDLER_object_read_default_attrs,
    ANJAY_DM_HANDLER_object_write_default_attrs,
    ANJAY_DM_HANDLER_list_instances,
    ANJAY_DM_HANDLER_instance_reset,
    ANJAY_DM_HANDLER_instance_create,
    ANJAY_DM_HANDLER_instance_remove,
    ANJAY_DM_HANDLER_instance_read_default_attrs,
    ANJAY_DM_HANDLER_instance_write_default_attrs,
    ANJAY_DM_HANDLER_list_resources,
    ANJAY_DM_HANDLER_resource_read,
    ANJAY_DM_HANDLER_resource_write,
    ANJAY_DM_HANDLER_resource_execute,
    ANJAY_DM_HANDLER_resource_reset,
    ANJAY_DM_HANDLER_list_resource_instances,
    ANJAY_DM_HANDLER_resource_read_attrs,
    ANJAY_DM_HANDLER_resource_write_attrs,
    ANJAY_DM_HANDLER_transaction_begin,
    ANJAY_DM_HANDLER_transaction_validate,
    ANJAY_DM_HANDLER_transaction_commit,
    ANJAY_DM_HANDLER_transaction_rollback,
#ifdef ANJAY_WITH_LWM2M11
    ANJAY_DM_HANDLER_resource_instance_read_attrs,
    ANJAY_DM_HANDLER_resource_instance_write_attrs,
#endif // ANJAY_WITH_LWM2M11
} anjay_dm_handler_t;

/**
 * Checks whether a specific data model handler is implemented for a given
 * Object, with respect to the Attribute Storage subsystem.
 *
 * The basic idea is that if this function returns <c>true</c> for a given
 * handler, it means that the corresponding <c>_anjay_dm_*</c> function called
 * with the same <c>anjay</c> and <c>obj_ptr</c> arguments will forward to some
 * actually implemented code (rather than defaulting to
 * <c>ANJAY_ERR_METHOD_NOT_ALLOWED</c>).
 *
 * @param anjay          Anjay object to operate on
 *
 * @param obj_ptr        Handle to an object to check handlers in
 *
 * @param handler_type   Type of handler to check
 *
 * @return Boolean value determining whether an applicable handler
 *         implementation is available
 */
bool _anjay_dm_handler_implemented(const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_dm_handler_t handler_type);

int _anjay_dm_call_object_read_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out);
int _anjay_dm_call_object_write_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs);
int _anjay_dm_call_list_instances(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_unlocked_dm_list_ctx_t *ctx);
int _anjay_dm_call_instance_reset(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid);
int _anjay_dm_call_instance_create(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_iid_t iid);
int _anjay_dm_call_instance_remove(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_iid_t iid);
int _anjay_dm_call_instance_read_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out);
int _anjay_dm_call_instance_write_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs);
int _anjay_dm_call_list_resources(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_unlocked_dm_resource_list_ctx_t *ctx);

int _anjay_dm_call_resource_read(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_unlocked_output_ctx_t *ctx);
int _anjay_dm_call_resource_write(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_unlocked_input_ctx_t *ctx);
int _anjay_dm_call_resource_execute(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_unlocked_execute_ctx_t *execute_ctx);
int _anjay_dm_call_resource_reset(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid);
int _anjay_dm_call_list_resource_instances(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_unlocked_dm_list_ctx_t *ctx);
int _anjay_dm_call_resource_read_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out);
int _anjay_dm_call_resource_write_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs);
#ifdef ANJAY_WITH_LWM2M11
int _anjay_dm_call_resource_instance_read_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out);
int _anjay_dm_call_resource_instance_write_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs);
#endif // ANJAY_WITH_LWM2M11

int _anjay_dm_call_transaction_begin(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr);
int _anjay_dm_call_transaction_validate(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr);
int _anjay_dm_call_transaction_commit(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr);
int _anjay_dm_call_transaction_rollback(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr);

/**
 * Starts a transaction on the data model. If a transaction is already in
 * progress, it has nesting semantics.
 *
 * @param anjay ANJAY object to operate on.
 */
avs_error_t _anjay_dm_transaction_begin(anjay_unlocked_t *anjay);

/**
 * Includes a given object in transaction, calling its <c>transaction_begin</c>
 * handler if not already called during the current global transaction.
 *
 * During the outermost call to @ref _anjay_dm_transaction_finish, the
 * <c>transaction_commit</c> (preceded by <c>transaction_validate</c>) or
 * <c>transaction_rollback</c> handler will be called on all objects included
 * in this way.
 *
 * This function is automatically called by @ref _anjay_dm_call_instance_reset,
 * @ref _anjay_dm_call_instance_create, @ref _anjay_dm_call_instance_remove and
 * @ref _anjay_dm_read_resource .
 *
 * NOTE: Attempting to call this function without a global transaction in place
 * will cause an assertion fail.
 *
 * @param anjay   ANJAY object to operate on.
 * @param obj_ptr Handle to an object to include in transaction.
 *
 * @return 0 for success, or an error code.
 */
int _anjay_dm_transaction_include_object(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr);

/**
 * After having been called a number of times corresponding to number of
 * preceding calls to @ref _anjay_dm_transaction_begin, finishes the transaction
 * by performing either a commit or a rollback, depending on the value of the
 * <c>result</c> parameter.
 *
 * @param anjay  ANJAY object to operate on.
 * @param result Result code from the operations performed during the
 *               transaction. 0 denotes a success and causes the transaction to
 *               be committed. Non-zero value is treated as an error code and
 *               causes the transaction to be rolled back.
 *
 * @return Final result code of the transaction. If an error occurred during the
 *         transaction handling routines (e.g. the transaction did not
 *         validate), a nonzero error code from those routines is returned.
 *         Otherwise, <c>result</c> is propagated. Note that it means that
 *         <c>0</c> is returned only after a successful commit following a
 *         successful transaction (denoted by <c>result == 0</c>).
 */
int _anjay_dm_transaction_finish(anjay_unlocked_t *anjay, int result);

bool _anjay_dm_transaction_object_included(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr);

const anjay_dm_installed_object_t *
_anjay_dm_find_object_by_oid(anjay_unlocked_t *anjay, anjay_oid_t oid);

bool _anjay_dm_ssid_exists(anjay_unlocked_t *anjay, anjay_ssid_t ssid);

int _anjay_ssid_from_security_iid(anjay_unlocked_t *anjay,
                                  anjay_iid_t security_iid,
                                  uint16_t *out_ssid);

#ifdef ANJAY_WITH_LWM2M11
int _anjay_server_uri_from_security_iid(anjay_unlocked_t *anjay,
                                        anjay_iid_t security_iid,
                                        char *out_uri,
                                        size_t out_size);
#endif // ANJAY_WITH_LWM2M11

bool _anjay_dm_attributes_empty(const anjay_dm_oi_attributes_t *attrs);
bool _anjay_dm_resource_attributes_empty(const anjay_dm_r_attributes_t *attrs);

bool _anjay_dm_attributes_full(const anjay_dm_oi_attributes_t *attrs);
bool _anjay_dm_resource_attributes_full(const anjay_dm_r_attributes_t *attrs);

int _anjay_dm_verify_resource_present(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t *obj,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_dm_resource_kind_t *out_kind);

int _anjay_dm_verify_resource_instance_present(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid);

int _anjay_dm_verify_instance_present(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid);

#define ANJAY_DM_OID_SECURITY 0
#define ANJAY_DM_OID_SERVER 1
#define ANJAY_DM_OID_ACCESS_CONTROL 2
#define ANJAY_DM_OID_DEVICE 3
#define ANJAY_DM_OID_FIRMWARE_UPDATE 5

#define ANJAY_DM_RID_SECURITY_SERVER_URI 0
#define ANJAY_DM_RID_SECURITY_BOOTSTRAP 1
#define ANJAY_DM_RID_SECURITY_MODE 2
#define ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY 3
#define ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY 4
#define ANJAY_DM_RID_SECURITY_SECRET_KEY 5
#define ANJAY_DM_RID_SECURITY_SSID 10
#define ANJAY_DM_RID_SECURITY_CLIENT_HOLD_OFF_TIME 11
#define ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT 12
#ifdef ANJAY_WITH_LWM2M11
#    define ANJAY_DM_RID_SECURITY_MATCHING_TYPE 13
#    define ANJAY_DM_RID_SECURITY_SNI 14
#    define ANJAY_DM_RID_SECURITY_CERTIFICATE_USAGE 15
#    define ANJAY_DM_RID_SECURITY_DTLS_TLS_CIPHERSUITE 16
#endif // ANJAY_WITH_LWM2M11

#define ANJAY_DM_RID_SERVER_SSID 0
#define ANJAY_DM_RID_SERVER_LIFETIME 1
#define ANJAY_DM_RID_SERVER_DEFAULT_PMIN 2
#define ANJAY_DM_RID_SERVER_DEFAULT_PMAX 3
#define ANJAY_DM_RID_SERVER_DISABLE 4
#define ANJAY_DM_RID_SERVER_DISABLE_TIMEOUT 5
#define ANJAY_DM_RID_SERVER_NOTIFICATION_STORING 6
#define ANJAY_DM_RID_SERVER_BINDING 7
#ifdef ANJAY_WITH_LWM2M11
#    define ANJAY_DM_RID_SERVER_TLS_DTLS_ALERT_CODE 11
#    define ANJAY_DM_RID_SERVER_LAST_BOOTSTRAPPED 12
#    define ANJAY_DM_RID_SERVER_BOOTSTRAP_ON_REGISTRATION_FAILURE 16
#    define ANJAY_DM_RID_SERVER_COMMUNICATION_RETRY_COUNT 17
#    define ANJAY_DM_RID_SERVER_COMMUNICATION_RETRY_TIMER 18
#    define ANJAY_DM_RID_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER 19
#    define ANJAY_DM_RID_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT 20
#    define ANJAY_DM_RID_SERVER_PREFERRED_TRANSPORT 22
#    define ANJAY_DM_RID_SERVER_MUTE_SEND 23
#endif // ANJAY_WITH_LWM2M11

#define ANJAY_DM_RID_ACCESS_CONTROL_OID 0
#define ANJAY_DM_RID_ACCESS_CONTROL_OIID 1
#define ANJAY_DM_RID_ACCESS_CONTROL_ACL 2
#define ANJAY_DM_RID_ACCESS_CONTROL_OWNER 3

#define ANJAY_DM_RID_DEVICE_FIRMWARE_VERSION 3
#define ANJAY_DM_RID_DEVICE_SOFTWARE_VERSION 19

#ifdef ANJAY_WITH_THREAD_SAFETY

anjay_oid_t
_anjay_dm_installed_object_oid(const anjay_dm_installed_object_t *obj);

const char *
_anjay_dm_installed_object_version(const anjay_dm_installed_object_t *obj);

#else // ANJAY_WITH_THREAD_SAFETY

static inline anjay_oid_t
_anjay_dm_installed_object_oid(const anjay_dm_installed_object_t *obj) {
    assert(obj);
    assert(*obj);
    assert(**obj);
    return (**obj)->oid;
}

static inline const char *
_anjay_dm_installed_object_version(const anjay_dm_installed_object_t *obj) {
    assert(obj);
    assert(*obj);
    assert(**obj);
    return (**obj)->version;
}

#endif // ANJAY_WITH_THREAD_SAFETY

int _anjay_register_object_unlocked(
        anjay_unlocked_t *anjay,
        AVS_LIST(anjay_dm_installed_object_t) *elem_ptr_move);

#ifdef ANJAY_WITH_SEND
int _anjay_send_batch_data_add_current_unlocked(
        anjay_send_batch_builder_t *builder,
        anjay_unlocked_t *anjay,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid);

int _anjay_send_batch_data_add_current_multiple_unlocked(
        anjay_send_batch_builder_t *builder,
        anjay_unlocked_t *anjay,
        const anjay_send_resource_path_t *paths,
        size_t paths_length,
        bool ignore_not_found);

anjay_send_result_t
_anjay_send_deferrable_unlocked(anjay_unlocked_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_send_batch_t *data,
                                anjay_send_finished_handler_t *finished_handler,
                                void *finished_handler_data);
#endif // ANJAY_WITH_SEND

void _anjay_dm_emit_unlocked(anjay_unlocked_dm_list_ctx_t *ctx, uint16_t id);

void _anjay_dm_emit_res_unlocked(anjay_unlocked_dm_resource_list_ctx_t *ctx,
                                 anjay_rid_t rid,
                                 anjay_dm_resource_kind_t kind,
                                 anjay_dm_resource_presence_t presence);

anjay_unlocked_ret_bytes_ctx_t *
_anjay_ret_bytes_begin_unlocked(anjay_unlocked_output_ctx_t *ctx,
                                size_t length);

int _anjay_ret_bytes_append_unlocked(anjay_unlocked_ret_bytes_ctx_t *ctx,
                                     const void *data,
                                     size_t length);

int _anjay_ret_bytes_unlocked(anjay_unlocked_output_ctx_t *ctx,
                              const void *data,
                              size_t length);

int _anjay_ret_string_unlocked(anjay_unlocked_output_ctx_t *ctx,
                               const char *value);

int _anjay_ret_i64_unlocked(anjay_unlocked_output_ctx_t *ctx, int64_t value);

int _anjay_ret_double_unlocked(anjay_unlocked_output_ctx_t *ctx, double value);

int _anjay_ret_bool_unlocked(anjay_unlocked_output_ctx_t *ctx, bool value);

int _anjay_ret_objlnk_unlocked(anjay_unlocked_output_ctx_t *ctx,
                               anjay_oid_t oid,
                               anjay_iid_t iid);

#ifdef ANJAY_WITH_LWM2M11
int _anjay_ret_u64_unlocked(anjay_unlocked_output_ctx_t *ctx, uint64_t value);
#endif // ANJAY_WITH_LWM2M11

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
int _anjay_ret_security_info_unlocked(
        anjay_unlocked_output_ctx_t *ctx,
        const avs_crypto_security_info_union_t *desc);
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */

int _anjay_get_bytes_unlocked(anjay_unlocked_input_ctx_t *ctx,
                              size_t *out_bytes_read,
                              bool *out_message_finished,
                              void *out_buf,
                              size_t buf_size);

int _anjay_get_string_unlocked(anjay_unlocked_input_ctx_t *ctx,
                               char *out_buf,
                               size_t buf_size);

int _anjay_get_i32_unlocked(anjay_unlocked_input_ctx_t *ctx, int32_t *out);

int _anjay_get_i64_unlocked(anjay_unlocked_input_ctx_t *ctx, int64_t *out);

#ifdef ANJAY_WITH_LWM2M11
int _anjay_get_u32_unlocked(anjay_unlocked_input_ctx_t *ctx, uint32_t *out);

int _anjay_get_u64_unlocked(anjay_unlocked_input_ctx_t *ctx, uint64_t *out);
#endif // ANJAY_WITH_LWM2M11

int _anjay_get_double_unlocked(anjay_unlocked_input_ctx_t *ctx, double *out);

int _anjay_get_bool_unlocked(anjay_unlocked_input_ctx_t *ctx, bool *out);

int _anjay_get_objlnk_unlocked(anjay_unlocked_input_ctx_t *ctx,
                               anjay_oid_t *out_oid,
                               anjay_iid_t *out_iid);

int _anjay_execute_get_next_arg_unlocked(anjay_unlocked_execute_ctx_t *ctx,
                                         int *out_arg,
                                         bool *out_has_value);

int _anjay_execute_get_arg_value_unlocked(anjay_unlocked_execute_ctx_t *ctx,
                                          size_t *out_bytes_read,
                                          char *out_buf,
                                          size_t buf_size);

AVS_LIST(const anjay_ssid_t)
_anjay_server_get_ssids_unlocked(anjay_unlocked_t *anjay);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_H */
