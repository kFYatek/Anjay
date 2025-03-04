/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_notify.h>

#include "coap/anjay_content_format.h"

#include "anjay_access_utils_private.h"
#include "anjay_core.h"
#include "anjay_servers_utils.h"
#include "observe/anjay_observe_core.h"

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_WITH_OBSERVE
static int observe_notify(anjay_unlocked_t *anjay,
                          anjay_ssid_t origin_ssid,
                          anjay_notify_queue_t queue) {
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (it->instance_set_changes.instance_set_changed) {
            _anjay_update_ret(&ret,
                              _anjay_observe_notify(anjay,
                                                    &MAKE_OBJECT_PATH(it->oid),
                                                    origin_ssid, true));
        } else {
            AVS_LIST(anjay_notify_queue_resource_entry_t) it2;
            AVS_LIST_FOREACH(it2, it->resources_changed) {
                _anjay_update_ret(&ret,
                                  _anjay_observe_notify(
                                          anjay,
                                          &MAKE_RESOURCE_PATH(it->oid, it2->iid,
                                                              it2->rid),
                                          origin_ssid, true));
            }
        }
    }
    return ret;
}
#else // ANJAY_WITH_OBSERVE
#    define observe_notify(anjay, origin_ssid, queue) (0)
#endif // ANJAY_WITH_OBSERVE

static int
security_modified_notify(anjay_unlocked_t *anjay,
                         anjay_notify_queue_object_entry_t *security) {
    int ret = 0;
    int32_t last_iid = -1;
    AVS_LIST(anjay_notify_queue_resource_entry_t) it;
    AVS_LIST_FOREACH(it, security->resources_changed) {
        if (it->iid != last_iid) {
            _anjay_update_ret(&ret,
                              _anjay_schedule_socket_update(anjay, it->iid));
            last_iid = it->iid;
        }
    }
    // NOTE: If anjay->update_immediately_on_dm_change is true,
    // then this will be called from anjay_notify_perform_impl() itself
    if (!anjay->update_immediately_on_dm_change
            && security->instance_set_changes.instance_set_changed) {
        _anjay_update_ret(&ret, _anjay_schedule_reload_servers(anjay));
    }
    return ret;
}

static int server_modified_notify(anjay_unlocked_t *anjay,
                                  anjay_notify_queue_object_entry_t *server) {
    int ret = 0;
    if (server->instance_set_changes.instance_set_changed) {
        // NOTE: If anjay->update_immediately_on_dm_change is true,
        // then this will be called from anjay_notify_perform_impl() itself
        if (!anjay->update_immediately_on_dm_change) {
            _anjay_update_ret(&ret, _anjay_schedule_reload_servers(anjay));
        }
#ifdef ANJAY_WITH_SEND
        // servers may have been removed from data model
        // if so, abort their Send requests as well
        _anjay_update_ret(
                &ret, _anjay_send_sched_retry_deferred(anjay, ANJAY_SSID_ANY));
#endif // ANJAY_WITH_SEND
    } else {
        AVS_LIST(anjay_notify_queue_resource_entry_t) it;
        AVS_LIST_FOREACH(it, server->resources_changed) {
            if (it->rid != ANJAY_DM_RID_SERVER_BINDING
                    && it->rid != ANJAY_DM_RID_SERVER_LIFETIME
#ifdef ANJAY_WITH_LWM2M11
                    && it->rid != ANJAY_DM_RID_SERVER_PREFERRED_TRANSPORT
#    ifdef ANJAY_WITH_SEND
                    && it->rid != ANJAY_DM_RID_SERVER_MUTE_SEND
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
            ) {
                continue;
            }
            const anjay_uri_path_t path =
                    MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, it->iid,
                                       ANJAY_DM_RID_SERVER_SSID);
            int64_t ssid;
            if (_anjay_dm_read_resource_i64(anjay, &path, &ssid) || ssid <= 0
                    || ssid >= UINT16_MAX) {
                _anjay_update_ret(&ret, -1);
#ifdef ANJAY_WITH_SEND
            } else if (it->rid == ANJAY_DM_RID_SERVER_MUTE_SEND) {
                _anjay_update_ret(&ret, _anjay_send_sched_retry_deferred(
                                                anjay, (anjay_ssid_t) ssid));
                continue;
#endif // ANJAY_WITH_SEND
            } else if (_anjay_servers_find_active(anjay, (anjay_ssid_t) ssid)) {
                _anjay_update_ret(&ret,
                                  _anjay_schedule_registration_update_unlocked(
                                          anjay, (anjay_ssid_t) ssid));
            }
        }
    }
    return ret;
}

static int anjay_notify_perform_impl(anjay_unlocked_t *anjay,
                                     anjay_ssid_t origin_ssid,
                                     anjay_notify_queue_t *queue_ptr,
                                     bool server_notify) {
    if (!queue_ptr || !*queue_ptr) {
        return 0;
    }
    bool instances_modified = false;
    int ret = 0;
    _anjay_update_ret(&ret, _anjay_sync_access_control(anjay, origin_ssid,
                                                       queue_ptr));
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, *queue_ptr) {
        if (it->instance_set_changes.instance_set_changed) {
            instances_modified = true;
        }
        if (it->oid == ANJAY_DM_OID_SECURITY) {
            _anjay_update_ret(&ret, security_modified_notify(anjay, it));
        } else if (server_notify && it->oid == ANJAY_DM_OID_SERVER) {
            _anjay_update_ret(&ret, server_modified_notify(anjay, it));
        }
    }
    if (instances_modified && anjay->update_immediately_on_dm_change) {
        _anjay_update_ret(&ret, _anjay_schedule_reload_servers(anjay));
    }
    _anjay_update_ret(&ret, observe_notify(anjay,
                                           anjay->enable_self_notify
                                                   ? ANJAY_SSID_BOOTSTRAP
                                                   : origin_ssid,
                                           *queue_ptr));
#ifdef ANJAY_WITH_ATTR_STORAGE
    _anjay_update_ret(&ret, _anjay_attr_storage_notify(anjay, *queue_ptr));
#endif // ANJAY_WITH_ATTR_STORAGE
    return ret;
}

int _anjay_notify_perform(anjay_unlocked_t *anjay,
                          anjay_ssid_t origin_ssid,
                          anjay_notify_queue_t *queue_ptr) {
    return anjay_notify_perform_impl(anjay, origin_ssid, queue_ptr, true);
}

int _anjay_notify_perform_without_servers(anjay_unlocked_t *anjay,
                                          anjay_ssid_t origin_ssid,
                                          anjay_notify_queue_t *queue_ptr) {
    return anjay_notify_perform_impl(anjay, origin_ssid, queue_ptr, false);
}

int _anjay_notify_flush(anjay_unlocked_t *anjay,
                        anjay_ssid_t origin_ssid,
                        anjay_notify_queue_t *queue_ptr) {
    int result = _anjay_notify_perform(anjay, origin_ssid, queue_ptr);
    _anjay_notify_clear_queue(queue_ptr);
    return result;
}

static AVS_LIST(anjay_notify_queue_object_entry_t) *
find_or_create_object_entry(anjay_notify_queue_t *out_queue, anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *it;
    AVS_LIST_FOREACH_PTR(it, out_queue) {
        if ((*it)->oid == oid) {
            return it;
        } else if ((*it)->oid > oid) {
            break;
        }
    }
    if (AVS_LIST_INSERT_NEW(anjay_notify_queue_object_entry_t, it)) {
        (*it)->oid = oid;
        return it;
    } else {
        return NULL;
    }
}

static int add_entry_to_iid_set(AVS_LIST(anjay_iid_t) *iid_set_ptr,
                                anjay_iid_t iid) {
    AVS_LIST_ITERATE_PTR(iid_set_ptr) {
        if (**iid_set_ptr == iid) {
            return 0;
        } else if (**iid_set_ptr > iid) {
            break;
        }
    }
    if (AVS_LIST_INSERT_NEW(anjay_iid_t, iid_set_ptr)) {
        **iid_set_ptr = iid;
        return 0;
    } else {
        return -1;
    }
}

static void remove_entry_from_iid_set(AVS_LIST(anjay_iid_t) *iid_set_ptr,
                                      anjay_iid_t iid) {
    AVS_LIST_ITERATE_PTR(iid_set_ptr) {
        if (**iid_set_ptr >= iid) {
            if (**iid_set_ptr == iid) {
                AVS_LIST_DELETE(iid_set_ptr);
            }
            return;
        }
    }
}

static void delete_notify_queue_object_entry_if_empty(
        AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr) {
    if (!entry_ptr || !*entry_ptr) {
        return;
    }
    if ((*entry_ptr)->instance_set_changes.instance_set_changed
            || (*entry_ptr)->resources_changed) {
        // entry not empty
        return;
    }
    assert(!(*entry_ptr)->instance_set_changes.known_added_iids);
    AVS_LIST_DELETE(entry_ptr);
}

int _anjay_notify_queue_instance_created(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    if (add_entry_to_iid_set(
                &(*entry_ptr)->instance_set_changes.known_added_iids, iid)) {
        anjay_log(ERROR, _("out of memory"));
        delete_notify_queue_object_entry_if_empty(entry_ptr);
        return -1;
    }
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

int _anjay_notify_queue_instance_removed(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    remove_entry_from_iid_set(
            &(*entry_ptr)->instance_set_changes.known_added_iids, iid);
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

int _anjay_notify_queue_instance_set_unknown_change(
        anjay_notify_queue_t *out_queue, anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

static int
compare_resource_entries(const anjay_notify_queue_resource_entry_t *left,
                         const anjay_notify_queue_resource_entry_t *right) {
    int result = left->iid - right->iid;
    if (!result) {
        result = left->rid - right->rid;
    }
    return result;
}

int _anjay_notify_queue_resource_change(anjay_notify_queue_t *out_queue,
                                        anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *obj_entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!obj_entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    anjay_notify_queue_resource_entry_t new_entry = {
        .iid = iid,
        .rid = rid
    };
    AVS_LIST(anjay_notify_queue_resource_entry_t) *res_entry_ptr;
    AVS_LIST_FOREACH_PTR(res_entry_ptr, &(*obj_entry_ptr)->resources_changed) {
        int compare = compare_resource_entries(*res_entry_ptr, &new_entry);
        if (compare == 0) {
            return 0;
        } else if (compare > 0) {
            break;
        }
    }
    if (!AVS_LIST_INSERT_NEW(anjay_notify_queue_resource_entry_t,
                             res_entry_ptr)) {
        anjay_log(ERROR, _("out of memory"));
        if (!(*obj_entry_ptr)->instance_set_changes.instance_set_changed
                && !(*obj_entry_ptr)->resources_changed) {
            AVS_LIST_DELETE(obj_entry_ptr);
        }
        return -1;
    }
    **res_entry_ptr = new_entry;
    return 0;
}

void _anjay_notify_clear_queue(anjay_notify_queue_t *out_queue) {
    AVS_LIST_CLEAR(out_queue) {
        AVS_LIST_CLEAR(&(*out_queue)->instance_set_changes.known_added_iids);
        AVS_LIST_CLEAR(&(*out_queue)->resources_changed);
    }
}

static void notify_clb(avs_sched_t *sched, const void *dummy) {
    (void) dummy;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    _anjay_notify_flush(anjay, ANJAY_SSID_BOOTSTRAP,
                        &anjay->scheduled_notify.queue);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int reschedule_notify(anjay_unlocked_t *anjay) {
    if (anjay->scheduled_notify.handle) {
        return 0;
    }
    return AVS_SCHED_NOW(anjay->sched, &anjay->scheduled_notify.handle,
                         notify_clb, NULL, 0);
}

int _anjay_notify_instance_created(anjay_unlocked_t *anjay,
                                   anjay_oid_t oid,
                                   anjay_iid_t iid) {
    int retval;
    (void) ((retval = _anjay_notify_queue_instance_created(
                     &anjay->scheduled_notify.queue, oid, iid))
            || (retval = reschedule_notify(anjay)));
    return retval;
}

int _anjay_notify_changed_unlocked(anjay_unlocked_t *anjay,
                                   anjay_oid_t oid,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid) {
    int retval;
    (void) ((retval = _anjay_notify_queue_resource_change(
                     &anjay->scheduled_notify.queue, oid, iid, rid))
            || (retval = reschedule_notify(anjay)));
    return retval;
}

int anjay_notify_changed(anjay_t *anjay_locked,
                         anjay_oid_t oid,
                         anjay_iid_t iid,
                         anjay_rid_t rid) {
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    retval = _anjay_notify_changed_unlocked(anjay, oid, iid, rid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

int _anjay_notify_instances_changed_unlocked(anjay_unlocked_t *anjay,
                                             anjay_oid_t oid) {
    int retval;
    (void) ((retval = _anjay_notify_queue_instance_set_unknown_change(
                     &anjay->scheduled_notify.queue, oid))
            || (retval = reschedule_notify(anjay)));
    return retval;
}

int anjay_notify_instances_changed(anjay_t *anjay_locked, anjay_oid_t oid) {
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    retval = _anjay_notify_instances_changed_unlocked(anjay, oid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

#ifdef ANJAY_WITH_OBSERVATION_STATUS
anjay_resource_observation_status_t
anjay_resource_observation_status(anjay_t *anjay_locked,
                                  anjay_oid_t oid,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid) {
    anjay_resource_observation_status_t retval = {
        .is_observed = false,
        .min_period = 0,
        .max_eval_period = ANJAY_ATTRIB_INTEGER_NONE,
#    if (ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER > 0)
        .servers_number = 0
#    endif //(ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER > 0)
    };
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    if (oid != ANJAY_ID_INVALID && iid != ANJAY_ID_INVALID
            && rid != ANJAY_ID_INVALID) {
        if (oid == ANJAY_DM_OID_SECURITY
                && _anjay_servers_find_active_by_security_iid(anjay, iid)) {
            // All resources in active Security instances are always considered
            // observed, as server connections need to be refreshed if they
            // changed; compare with _anjay_notify_perform()
            retval.is_observed = true;
        } else if (oid == ANJAY_DM_OID_SERVER
                   && (rid == ANJAY_DM_RID_SERVER_LIFETIME
                       || rid == ANJAY_DM_RID_SERVER_BINDING
#    ifdef ANJAY_WITH_LWM2M11
                       || rid == ANJAY_DM_RID_SERVER_PREFERRED_TRANSPORT
#    endif // ANJAY_WITH_LWM2M11
                       )) {
            // Lifetime and Binding in Server Object are always considered
            // observed, as server connections need to be refreshed if they
            // changed; compare with _anjay_notify_perform()
            retval.is_observed = true;
        } else {
            // Note: some modules may also depend on resource notifications,
            // particularly Firmware Update depends on notifications on /5/0/3,
            // but it also implements that object and generates relevant
            // notifications internally, so there's no need to check that here.
            retval = _anjay_observe_status(anjay, oid, iid, rid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}
#endif // ANJAY_WITH_OBSERVATION_STATUS
