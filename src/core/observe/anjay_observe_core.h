/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_OBSERVE_CORE_H
#define ANJAY_OBSERVE_CORE_H

#include <avsystem/commons/avs_persistence.h>
#include <avsystem/commons/avs_sorted_set.h>

#include "../anjay_servers_private.h"
#include "../coap/anjay_msg_details.h"
#include "../io/anjay_batch_builder.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_observation_struct anjay_observation_t;
typedef struct anjay_observe_connection_entry_struct
        anjay_observe_connection_entry_t;

typedef enum {
    NOTIFY_QUEUE_UNLIMITED,
    NOTIFY_QUEUE_DROP_OLDEST
} notify_queue_limit_mode_t;

typedef struct {
    AVS_LIST(anjay_observe_connection_entry_t) connection_entries;
    bool confirmable_notifications;

    notify_queue_limit_mode_t notify_queue_limit_mode;
    size_t notify_queue_limit;
} anjay_observe_state_t;

typedef struct {
    anjay_observation_t *const ref;
    anjay_msg_details_t details;
    avs_coap_notify_reliability_hint_t reliability_hint;
    avs_time_real_t timestamp;

    // Array size is ref->paths_count for "normal" entry, or 0 for error entry
    // (determined based on is_error_value()). values[i] is a value
    // corresponding to ref->paths[i]. Note that each values[i] element might
    // contain multiple entries itself if ref->paths[i] is hierarchical (e.g.
    // Object Instance).
    anjay_batch_t *values[];
} anjay_observation_value_t;

#ifdef ANJAY_WITH_OBSERVE

void _anjay_observe_init(anjay_observe_state_t *observe,
                         bool confirmable_notifications,
                         size_t stored_notification_limit);

void _anjay_observe_cleanup(anjay_observe_state_t *observe);

void _anjay_observe_gc(anjay_unlocked_t *anjay);

int _anjay_observe_handle(anjay_connection_ref_t ref,
                          const anjay_request_t *request);

#    ifdef ANJAY_WITH_LWM2M11
int _anjay_observe_composite_handle(anjay_connection_ref_t ref,
                                    AVS_LIST(anjay_uri_path_t) paths,
                                    const anjay_request_t *request);
#    endif // ANJAY_WITH_LWM2M11

void _anjay_observe_interrupt(anjay_connection_ref_t ref);

void _anjay_observe_invalidate(anjay_connection_ref_t ref);

bool _anjay_observe_confirmable_in_delivery(anjay_connection_ref_t ref);

#    ifndef ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE
bool _anjay_observe_needs_flushing(anjay_connection_ref_t ref);
#    endif // ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE

int _anjay_observe_sched_flush(anjay_connection_ref_t ref);

int _anjay_observe_notify(anjay_unlocked_t *anjay,
                          const anjay_uri_path_t *path,
                          anjay_ssid_t ssid,
                          bool invert_ssid_match);

#    ifdef ANJAY_WITH_OBSERVATION_STATUS
anjay_resource_observation_status_t
_anjay_observe_status(anjay_unlocked_t *anjay,
                      anjay_oid_t oid,
                      anjay_iid_t iid,
                      anjay_rid_t rid);
#    endif // ANJAY_WITH_OBSERVATION_STATUS

#else // ANJAY_WITH_OBSERVE

#    define _anjay_observe_init(...) ((void) 0)
#    define _anjay_observe_cleanup(...) ((void) 0)
#    define _anjay_observe_gc(...) ((void) 0)
#    define _anjay_observe_interrupt(...) ((void) 0)
#    define _anjay_observe_invalidate(...) ((void) 0)
#    define _anjay_observe_confirmable_in_delivery(...) false
#    define _anjay_observe_needs_flushing(...) false
#    define _anjay_observe_sched_flush(...) 0

#    ifdef ANJAY_WITH_OBSERVATION_STATUS
#        define _anjay_observe_status(...)         \
            ((anjay_resource_observation_status) { \
                .is_observed = false,              \
                .min_period = -1                   \
            })
#    endif // ANJAY_WITH_OBSERVATION_STATUS

#endif // ANJAY_WITH_OBSERVE

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_OBSERVE_CORE_H */
