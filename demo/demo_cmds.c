/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "demo_cmds.h"
#include "demo.h"
#include "demo_utils.h"
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
#    include "firmware_update.h"
#endif // ANJAY_WITH_MODULE_FW_UPDATE

#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
#    include "advanced_firmware_update.h"
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE

#include <ctype.h>
#include <inttypes.h>
#include <string.h>

#include <anjay/attr_storage.h>
#include <anjay/ipso_objects.h>
#include <anjay/security.h>

#ifdef ANJAY_WITH_SEND
#    include <anjay/lwm2m_send.h>
#endif // ANJAY_WITH_SEND

#include <avsystem/commons/avs_memory.h>

#define MAX_SEND_RESOURCES 32

static int parse_ssid(const char *text, anjay_ssid_t *out_ssid) {
    unsigned id;
    if (sscanf(text, "%u", &id) < 1 || id > UINT16_MAX) {
        return -1;
    }
    *out_ssid = (uint16_t) id;
    return 0;
}

static void cmd_send_register(anjay_demo_t *demo, const char *args_string) {
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }

    if (anjay_schedule_register(demo->anjay, ssid)) {
        demo_log(ERROR, "could not schedule registration");
    } else if (ssid == ANJAY_SSID_ANY) {
        demo_log(INFO, "registration scheduled for all servers");
    } else {
        demo_log(INFO, "registration scheduled for server %" PRIu16, ssid);
    }
}

static void cmd_send_update(anjay_demo_t *demo, const char *args_string) {
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }

    if (anjay_schedule_registration_update(demo->anjay, ssid)) {
        demo_log(ERROR, "could not schedule registration update");
    } else if (ssid == ANJAY_SSID_ANY) {
        demo_log(INFO, "registration update scheduled for all servers");
    } else {
        demo_log(INFO, "registration update scheduled for server %" PRIu16,
                 ssid);
    }
}

static void cmd_reconnect_server(anjay_demo_t *demo, const char *args_string) {
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }

    if (anjay_server_schedule_reconnect(demo->anjay, ssid)) {
        demo_log(ERROR, "could not enable server with SSID %" PRIu16, ssid);
        return;
    }
}

static int parse_transports(const char *text,
                            anjay_transport_set_t *out_transport_set) {
    char *text_copy = avs_strdup(text);
    if (!text_copy) {
        demo_log(ERROR, "Out of memory");
        return -1;
    }
    *out_transport_set = ANJAY_TRANSPORT_SET_ALL;
    bool found = false;
    bool error = false;
    char *text_arg = text_copy;
    char *saveptr = NULL;
    const char *token = NULL;
    while ((token = avs_strtok(text_arg, AVS_SPACES ",", &saveptr))) {
        text_arg = NULL;
        if (!found) {
            memset(out_transport_set, 0, sizeof(*out_transport_set));
            found = true;
        }
        if (strcmp(token, "ip") == 0) {
            out_transport_set->udp = true;
            out_transport_set->tcp = true;
        } else if (strcmp(token, "udp") == 0) {
            out_transport_set->udp = true;
        } else if (strcmp(token, "tcp") == 0) {
            out_transport_set->tcp = true;
        } else {
            demo_log(ERROR, "Unrecognized transport: %s", token);
            error = true;
        }
    }
    avs_free(text_copy);
    return error ? -1 : 0;
}

static void cmd_reconnect(anjay_demo_t *demo, const char *args_string) {
    anjay_transport_set_t transport_set;
    if (!parse_transports(args_string, &transport_set)) {
        if (anjay_transport_schedule_reconnect(demo->anjay, transport_set)) {
            demo_log(ERROR, "could not schedule reconnect");
        } else {
            demo_log(INFO, "reconnect scheduled");
        }
    }
}

#ifdef ANJAY_WITH_MODULE_FW_UPDATE
static void cmd_set_fw_package_path(anjay_demo_t *demo,
                                    const char *args_string) {
    const char *path = args_string;
    while (isspace(*path)) {
        ++path;
    }

    firmware_update_set_package_path(&demo->fw_update, path);
}
#endif // ANJAY_WITH_MODULE_FW_UPDATE

#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
static void cmd_set_afu_package_path(anjay_demo_t *demo,
                                     const char *args_string) {
    const char *path = args_string;
    while (isspace(*path)) {
        ++path;
    }

    /* This allows setting package path only for first (APP) image */
    advanced_fw_update_logic_t *fw_logic_app =
            demo->advanced_fw_update_logic_table;
    assert(fw_logic_app->iid == FW_UPDATE_IID_APP);
    advanced_firmware_update_set_package_path(fw_logic_app, path);
}

static void cmd_get_afu_deadline(anjay_demo_t *demo, const char *args_string) {
    (void) args_string;
    int64_t update_deadline_timestamp = 0;
    avs_time_real_to_scalar(&update_deadline_timestamp, AVS_TIME_S,
                            anjay_advanced_fw_update_get_deadline(
                                    demo->anjay, FW_UPDATE_IID_APP));
    printf("AFU_APP_UPDATE_DEADLINE==%" PRId64 "\n", update_deadline_timestamp);
}

static void cmd_set_afu_result(anjay_demo_t *demo, const char *args_string) {
    int result;
    if (sscanf(args_string, " %d", &result) != 1) {
        demo_log(ERROR, "Advanced Firmware Update result not specified");
        return;
    }
    if (anjay_advanced_fw_update_set_state_and_result(
                demo->anjay,
                FW_UPDATE_IID_APP,
                (anjay_advanced_fw_update_state_t)
                        ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                (anjay_advanced_fw_update_result_t) result)) {
        demo_log(ERROR,
                 "Advanced Firmware Update result set for APP image at runtime "
                 "failed.");
    }
}
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE

static void cmd_open_location_csv(anjay_demo_t *demo, const char *args_string) {
    const anjay_dm_object_def_t **location_obj =
            demo_find_object(demo, DEMO_OID_LOCATION);
    if (!location_obj) {
        demo_log(ERROR, "Location object not registered");
        return;
    }

    char *filename = (char *) avs_malloc(strlen(args_string) + 1);
    if (!filename) {
        demo_log(ERROR, "Out of memory");
        return;
    }
    filename[0] = '\0';
    unsigned long frequency_s = 1;
    sscanf(args_string, "%s %lu", filename, &frequency_s);
    if (!location_open_csv(location_obj, filename, (time_t) frequency_s)) {
        demo_log(INFO, "Successfully opened CSV file");
    }
    avs_free(filename);
}

static size_t count_servers(const server_connection_args_t *args) {
    size_t num_servers = 0;
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, args) {
        ++num_servers;
    }
    return num_servers;
}

static int add_server(anjay_demo_t *demo, const char *uri) {
    size_t num_servers = count_servers(demo->connection_args);
    if (num_servers >= MAX_SERVERS) {
        demo_log(ERROR, "Maximum number of servers reached");
        return -1;
    }
    size_t uri_size = strlen(uri) + 1;
    AVS_LIST(anjay_demo_string_t) copied_uri =
            (AVS_LIST(anjay_demo_string_t)) AVS_LIST_NEW_BUFFER(uri_size);
    if (!copied_uri) {
        demo_log(ERROR, "Out of memory");
        return -1;
    }
    memcpy(copied_uri->data, uri, uri_size);
    AVS_LIST_INSERT(&demo->allocated_strings, copied_uri);

    server_entry_t *entry = &demo->connection_args->servers[num_servers];
    *entry = demo->connection_args->servers[num_servers - 1];
    entry->id = (anjay_ssid_t) (num_servers + 1);
    entry->uri = copied_uri->data;
    entry->security_iid = (anjay_iid_t) entry->id;
    entry->server_iid = (anjay_iid_t) entry->id;
    demo_log(INFO, "Added new server, ID == %d", (int) (num_servers + 1));
    return 0;
}

static void cmd_add_server(anjay_demo_t *demo, const char *args_string) {
    const char *uri = args_string;
    while (isspace(*uri)) {
        ++uri;
    }

    if (add_server(demo, uri)) {
        return;
    }
    demo_reload_servers(demo);
}

static void cmd_trim_servers(anjay_demo_t *demo, const char *args_string) {
    size_t num_servers = count_servers(demo->connection_args);
    unsigned number;
    if (sscanf(args_string, "%u", &number) != 1 || number > num_servers) {
        demo_log(ERROR, "Invalid servers number: %s", args_string);
        return;
    }

    for (size_t i = number; i < num_servers; ++i) {
        demo->connection_args->servers[i].uri = NULL;
    }
    demo_reload_servers(demo);
}

static void cmd_socket_count(anjay_demo_t *demo, const char *args_string) {
    (void) args_string;
    printf("SOCKET_COUNT==%lu\n",
           (unsigned long) AVS_LIST_SIZE(anjay_get_sockets(demo->anjay)));
}

static void cmd_get_port(anjay_demo_t *demo, const char *args_string) {
    int index;
    if (sscanf(args_string, "%d", &index) != 1) {
        demo_log(ERROR, "Invalid index: %s", args_string);
        return;
    }

    AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(demo->anjay);
    int num_sockets = (int) AVS_LIST_SIZE(sockets);
    if (index < 0) {
        index = num_sockets + index;
    }
    if (index < 0 || index >= num_sockets) {
        demo_log(ERROR, "Index out of range: %d; num_sockets == %d", index,
                 num_sockets);
    }
    char port[16] = "0";
    AVS_LIST(avs_net_socket_t *const) socket =
            AVS_LIST_NTH(sockets, (size_t) index);
    if (socket && *socket) {
        avs_net_socket_get_local_port(*socket, port, sizeof(port));
    }
    printf("PORT==%s\n", port);
}

static void cmd_get_transport(anjay_demo_t *demo, const char *args_string) {
    int index;
    if (sscanf(args_string, "%d", &index) != 1) {
        demo_log(ERROR, "Invalid index: %s", args_string);
        return;
    }

    AVS_LIST(const anjay_socket_entry_t) entries =
            anjay_get_socket_entries(demo->anjay);
    int num_sockets = (int) AVS_LIST_SIZE(entries);
    if (index < 0) {
        index = num_sockets + index;
    }
    if (index < 0 || index >= num_sockets) {
        demo_log(ERROR, "Index out of range: %d; num_sockets == %d", index,
                 num_sockets);
        return;
    }
    AVS_LIST(const anjay_socket_entry_t) entry =
            AVS_LIST_NTH(entries, (size_t) index);
    switch (entry->transport) {
    case ANJAY_SOCKET_TRANSPORT_UDP:
        puts("TRANSPORT==UDP");
        break;
    case ANJAY_SOCKET_TRANSPORT_TCP:
        puts("TRANSPORT==TCP");
        break;
    default:
        printf("TRANSPORT==%d\n", (int) entry->transport);
    }
}

static void cmd_non_lwm2m_socket_count(anjay_demo_t *demo,
                                       const char *args_string) {
    (void) args_string;
    AVS_LIST(const anjay_socket_entry_t) entry =
            anjay_get_socket_entries(demo->anjay);
    unsigned long non_lwm2m_sockets = 0;
    AVS_LIST_ITERATE(entry) {
        if (entry->ssid == ANJAY_SSID_ANY
                && entry->transport != ANJAY_SOCKET_TRANSPORT_SMS) {
            ++non_lwm2m_sockets;
        }
    }
    printf("NON_LWM2M_SOCKET_COUNT==%lu\n", non_lwm2m_sockets);
}

static void cmd_enter_offline(anjay_demo_t *demo, const char *args_string) {
    anjay_transport_set_t transport_set;
    if (!parse_transports(args_string, &transport_set)) {
        int result = anjay_transport_enter_offline(demo->anjay, transport_set);
        demo_log(INFO, "anjay_transport_enter_offline(), result == %d", result);
    }
}

static void cmd_exit_offline(anjay_demo_t *demo, const char *args_string) {
    anjay_transport_set_t transport_set;
    if (!parse_transports(args_string, &transport_set)) {
        int result = anjay_transport_exit_offline(demo->anjay, transport_set);
        demo_log(INFO, "anjay_transport_exit_offline(), result == %d", result);
    }
}

static void cmd_notify(anjay_demo_t *demo, const char *args_string) {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_rid_t rid;
    if (sscanf(args_string, " /%" SCNu16 "/%" SCNu16 "/%" SCNu16, &oid, &iid,
               &rid)
            == 3) {
        (void) anjay_notify_changed(demo->anjay, oid, iid, rid);
    } else if (sscanf(args_string, " /%" SCNu16, &oid) == 1) {
        (void) anjay_notify_instances_changed(demo->anjay, oid);
    } else {
        demo_log(WARNING, "notify usage:\n"
                          "1. notify /OID\n"
                          "2. notify /OID/IID/RID");
        return;
    }
}

#ifdef ANJAY_WITH_SEND
static void send_finished_handler(anjay_t *anjay,
                                  anjay_ssid_t ssid,
                                  const anjay_send_batch_t *batch,
                                  int result,
                                  void *data) {
    (void) anjay;
    (void) ssid;
    (void) batch;
    (void) data;
    demo_log(INFO, "SEND FINISHED HANDLER: %d", result);
}

typedef anjay_send_result_t
anjay_send_func_t(anjay_t *anjay,
                  anjay_ssid_t ssid,
                  const anjay_send_batch_t *data,
                  anjay_send_finished_handler_t *finished_handler,
                  void *finished_handler_data);

static void print_send_usage(const char *command) {
    demo_log(WARNING, "%s usage: %s SSID [/OID/IID/RID [...]]", command,
             command);
}

static void cmd_send_impl(anjay_demo_t *demo,
                          const char *command,
                          anjay_send_func_t *send_func,
                          const char *args_string) {
    anjay_ssid_t ssid;
    if (sscanf(args_string, " %" SCNu16, &ssid) != 1) {
        print_send_usage(command);
        return;
    }
    args_string++;

    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    if (!builder) {
        demo_log(ERROR, "Out of memory");
        return;
    }

    anjay_send_resource_path_t paths[MAX_SEND_RESOURCES];
    size_t paths_count = 0;

    while ((args_string = strchr(args_string, ' '))) {
        if (paths_count == MAX_SEND_RESOURCES) {
            demo_log(ERROR, "Max. %d resources allowed in Send",
                     MAX_SEND_RESOURCES);
            anjay_send_batch_builder_cleanup(&builder);
        }

        args_string++;
        anjay_oid_t oid;
        anjay_iid_t iid;
        anjay_rid_t rid;
        if (sscanf(args_string, "/%" SCNu16 "/%" SCNu16 "/%" SCNu16, &oid, &iid,
                   &rid)
                != 3) {
            print_send_usage(command);
            anjay_send_batch_builder_cleanup(&builder);
            return;
        }

        paths[paths_count++] = (anjay_send_resource_path_t) { oid, iid, rid };
    }

    int result = 0;
    if (paths_count == 1) {
        result = anjay_send_batch_data_add_current(
                builder, demo->anjay, paths[0].oid, paths[0].iid, paths[0].rid);
    } else if (paths_count) {
        result =
                anjay_send_batch_data_add_current_multiple(builder, demo->anjay,
                                                           paths, paths_count);
    }

    if (result) {
        demo_log(ERROR, "Error during reading values from data model");
        anjay_send_batch_builder_cleanup(&builder);
        return;
    }

    anjay_send_batch_t *data = anjay_send_batch_builder_compile(&builder);
    if (!data) {
        demo_log(ERROR, "Out of memory");
        anjay_send_batch_builder_cleanup(&builder);
        return;
    }

    const anjay_send_result_t send_result =
            send_func(demo->anjay, ssid, data, send_finished_handler, NULL);
    if (send_result) {
        demo_log(ERROR, "cannot perform LwM2M Send, result: %d",
                 (int) send_result);
    }

    anjay_send_batch_release(&data);
}

static void cmd_send(anjay_demo_t *demo, const char *args_string) {
    cmd_send_impl(demo, "send", anjay_send, args_string);
}

static void cmd_send_deferrable(anjay_demo_t *demo, const char *args_string) {
    cmd_send_impl(demo, "send_deferrable", anjay_send_deferrable, args_string);
}
#endif // ANJAY_WITH_SEND

static void cmd_unregister_object(anjay_demo_t *demo, const char *args_string) {
    int oid;
    if (sscanf(args_string, "%d", &oid) != 1 || oid < 0 || oid > UINT16_MAX) {
        demo_log(ERROR, "Invalid OID: %s", args_string);
        return;
    }

    AVS_LIST(anjay_demo_object_t) *object_entry_ptr;
    AVS_LIST_FOREACH_PTR(object_entry_ptr, &demo->objects) {
        if ((*(*object_entry_ptr)->obj_ptr)->oid == oid) {
            if (anjay_unregister_object(demo->anjay,
                                        (*object_entry_ptr)->obj_ptr)) {
                demo_log(ERROR, "Could not unregister object %d", oid);
                return;
            }
            return;
        }
    }

    demo_log(ERROR, "No such object to unregister: %d", oid);
}

static void cmd_reregister_object(anjay_demo_t *demo, const char *args_string) {
    int oid;
    if (sscanf(args_string, "%d", &oid) != 1 || oid < 0 || oid > UINT16_MAX) {
        demo_log(ERROR, "Invalid OID: %s", args_string);
        return;
    }

    AVS_LIST(anjay_demo_object_t) *object_entry_ptr;
    AVS_LIST_FOREACH_PTR(object_entry_ptr, &demo->objects) {
        if ((*(*object_entry_ptr)->obj_ptr)->oid == oid) {
            if (anjay_register_object(demo->anjay,
                                      (*object_entry_ptr)->obj_ptr)) {
                demo_log(ERROR, "Could not re-register object %d", oid);
                return;
            }
            return;
        }
    }

    demo_log(ERROR, "No such object to register: %d", oid);
}

typedef struct {
    size_t skip_at;
    size_t skip_to;
} demo_download_skip_def_t;

typedef struct {
    anjay_download_handle_t handle;
    FILE *f;
    AVS_LIST(demo_download_skip_def_t) skips;
    size_t current_offset;
} demo_download_user_data_t;

static void demo_download_user_data_destroy(demo_download_user_data_t *data) {
    if (data) {
        if (data->f) {
            fclose(data->f);
        }
        AVS_LIST_CLEAR(&data->skips);
        avs_free(data);
    }
}

static avs_error_t dl_write_next_block_new(anjay_t *anjay,
                                           const uint8_t *data,
                                           size_t data_size,
                                           const anjay_etag_t *etag,
                                           void *user_data_) {
    demo_download_user_data_t *user_data =
            (demo_download_user_data_t *) user_data_;
    (void) etag;

    size_t to_write = data_size;
    if (user_data->skips
            && user_data->skips->skip_at
                           <= user_data->current_offset + data_size) {
        to_write = user_data->skips->skip_at - user_data->current_offset;
        user_data->current_offset = user_data->skips->skip_to;
        AVS_LIST_DELETE(&user_data->skips);
        avs_error_t err =
                anjay_download_set_next_block_offset(anjay, user_data->handle,
                                                     user_data->current_offset);
        if (avs_is_err(err)) {
            demo_log(ERROR, "anjay_download_set_next_block_offset() failed");
            return err;
        }
    } else {
        user_data->current_offset += to_write;
    }

    if (fwrite(data, to_write, 1, user_data->f) != 1) {
        demo_log(ERROR, "fwrite() failed");
        return avs_errno(AVS_UNKNOWN_ERROR);
    }

    return AVS_OK;
}

static void dl_finished_new(anjay_t *anjay,
                            anjay_download_status_t status,
                            void *user_data) {
    (void) anjay;
    demo_download_user_data_destroy((demo_download_user_data_t *) user_data);
    demo_log(INFO, "download finished, result == %d", (int) status.result);
}

static void cmd_download(anjay_demo_t *demo, const char *args_string) {
    char url[256];
    char target_file[256];
    char psk_identity[256] = "";
    char psk_key[256] = "";

    if (sscanf(args_string, "%255s %255s %255s %255s", url, target_file,
               psk_identity, psk_key)
            < 2) {
        demo_log(ERROR, "invalid URL or target file in: %s", args_string);
        return;
    }

    demo_download_user_data_t *user_data =
            (demo_download_user_data_t *) avs_calloc(
                    1, sizeof(demo_download_user_data_t));
    if (!user_data || !(user_data->f = fopen(target_file, "wb"))) {
        demo_log(ERROR, "could not open file: %s", target_file);
        demo_download_user_data_destroy(user_data);
        return;
    }

    avs_net_psk_info_t psk = {
        .key = avs_crypto_psk_key_info_from_buffer(psk_key, strlen(psk_key)),
        .identity = avs_crypto_psk_identity_info_from_buffer(
                psk_identity, strlen(psk_identity))
    };
    anjay_download_config_t cfg = {
        .url = url,
        .on_next_block = dl_write_next_block_new,
        .on_download_finished = dl_finished_new,
        .user_data = user_data,
        .security_config = {
            .security_info = avs_net_security_info_from_psk(psk)
        }
    };

    if (avs_is_err(anjay_download(demo->anjay, &cfg, &user_data->handle))) {
        demo_log(ERROR, "could not schedule download");
        demo_download_user_data_destroy(user_data);
    }
}

static void cmd_download_blocks_impl(anjay_demo_t *demo, char *args_string) {
    char url[256];
    char target_file[256];
    int offsets_offset;

    if (sscanf(args_string, "%255s %255s %n", url, target_file, &offsets_offset)
            < 2) {
        demo_log(ERROR, "invalid URL or target file in: %s", args_string);
        return;
    }

    demo_download_user_data_t *user_data =
            (demo_download_user_data_t *) avs_calloc(
                    1, sizeof(demo_download_user_data_t));
    if (!user_data || !(user_data->f = fopen(target_file, "wb"))) {
        demo_log(ERROR, "could not open file: %s", target_file);
        demo_download_user_data_destroy(user_data);
        return;
    }

    anjay_download_config_t cfg = {
        .url = url,
        .on_next_block = dl_write_next_block_new,
        .on_download_finished = dl_finished_new,
        .user_data = user_data
    };

    long last_end_offset = -1;
    AVS_LIST(demo_download_skip_def_t) last_skip = NULL;

    char *offsets_text = &args_string[offsets_offset];
    char *saveptr = NULL;
    const char *token = NULL;
    while ((token = avs_strtok(offsets_text, AVS_SPACES, &saveptr))) {
        offsets_text = NULL;
        char *endptr;
        long start_offset = strtol(token, &endptr, 0);
        if (start_offset <= last_end_offset
                || (endptr && *endptr && *endptr != '-')) {
            goto parse_error;
        }
        long end_offset = LONG_MAX;
        if (endptr && endptr[0] == '-' && endptr[1]) {
            end_offset = strtol(&endptr[1], &endptr, 0);
            if (end_offset <= start_offset || (endptr && *endptr)) {
                goto parse_error;
            }
        }

        if (last_skip) {
            last_skip->skip_to = (size_t) start_offset;
        } else {
            cfg.start_offset = (size_t) start_offset;
            user_data->current_offset = cfg.start_offset;
        }
        if (end_offset < LONG_MAX) {
            AVS_LIST(demo_download_skip_def_t) skip =
                    AVS_LIST_NEW_ELEMENT(demo_download_skip_def_t);
            if (!skip) {
                demo_log(ERROR, "out of memory");
                demo_download_user_data_destroy(user_data);
                return;
            }
            skip->skip_at = (size_t) end_offset;
            skip->skip_to = SIZE_MAX;
            AVS_LIST_APPEND(&last_skip, skip);
            if (!user_data->skips) {
                user_data->skips = skip;
            } else {
                AVS_LIST_ADVANCE(&last_skip);
            }
            assert(last_skip == skip);
        }

        last_end_offset = end_offset;
    }

    if (avs_is_err(anjay_download(demo->anjay, &cfg, &user_data->handle))) {
        demo_log(ERROR, "could not schedule download");
        demo_download_user_data_destroy(user_data);
    }
    return;
parse_error:
    demo_log(ERROR, "Invalid block definition: %s", token);
    demo_download_user_data_destroy(user_data);
}

static void cmd_download_blocks(anjay_demo_t *demo, const char *args_string) {
    char *args_string_copy = avs_strdup(args_string);
    if (!args_string_copy) {
        demo_log(ERROR, "out of memory");
        return;
    }
    cmd_download_blocks_impl(demo, args_string_copy);
    avs_free(args_string_copy);
}

#ifdef ANJAY_WITH_ATTR_STORAGE
static void cmd_set_attrs(anjay_demo_t *demo, const char *args_string) {
    char *path = (char *) avs_malloc(strlen(args_string) + 1);
    if (!path) {
        demo_log(ERROR, "Out of memory");
        return;
    }
    int path_len = 0;
    const char *args = NULL, *pmin = NULL, *pmax = NULL, *lt = NULL, *gt = NULL,
               *st = NULL, *epmin = NULL, *epmax = NULL;
    anjay_dm_r_attributes_t attrs;
    int ssid;

    if (sscanf(args_string, "%s %d%n", path, &ssid, &path_len) != 2) {
        goto error;
    }

    if (ssid < 0 || UINT16_MAX <= ssid) {
        demo_log(ERROR, "invalid SSID: expected 0 <= ssid < 65535, got %d",
                 ssid);
        goto error;
    }

    args = args_string + path_len;
    attrs = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    pmin = strstr(args, "pmin=");
    pmax = strstr(args, "pmax=");
    epmin = strstr(args, "epmin=");
    epmax = strstr(args, "epmax=");
    lt = strstr(args, "lt=");
    gt = strstr(args, "gt=");
    st = strstr(args, "st=");
    if (pmin) {
        (void) sscanf(pmin, "pmin=%" PRId32, &attrs.common.min_period);
    }
    if (pmax) {
        (void) sscanf(pmax, "pmax=%" PRId32, &attrs.common.max_period);
    }
    if (epmin) {
        (void) sscanf(epmin, "epmin=%" PRId32, &attrs.common.min_eval_period);
    }
    if (epmax) {
        (void) sscanf(epmax, "epmax=%" PRId32, &attrs.common.max_eval_period);
    }
    if (lt) {
        (void) sscanf(lt, "lt=%lf", &attrs.less_than);
    }
    if (gt) {
        (void) sscanf(gt, "gt=%lf", &attrs.greater_than);
    }
    if (st) {
        (void) sscanf(st, "st=%lf", &attrs.step);
    }

    int oid, iid, rid;
#    ifdef ANJAY_WITH_LWM2M11
    int riid;
#    endif // ANJAY_WITH_LWM2M11
    switch (sscanf(path,
                   "/%d/%d/%d"
#    ifdef ANJAY_WITH_LWM2M11
                   "/%d"
#    endif // ANJAY_WITH_LWM2M11
                   ,
                   &oid, &iid, &rid
#    ifdef ANJAY_WITH_LWM2M11
                   ,
                   &riid
#    endif // ANJAY_WITH_LWM2M11
                   )) {
#    ifdef ANJAY_WITH_LWM2M11
    case 4:
        if (anjay_attr_storage_set_resource_instance_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    (anjay_iid_t) iid, (anjay_rid_t) rid, (anjay_riid_t) riid,
                    &attrs)) {
            demo_log(ERROR, "failed to set resource instance level attributes");
        }
        goto finish;
#    endif // ANJAY_WITH_LWM2M11
    case 3:
        if (anjay_attr_storage_set_resource_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    (anjay_iid_t) iid, (anjay_rid_t) rid, &attrs)) {
            demo_log(ERROR, "failed to set resource level attributes");
        }
        goto finish;
    case 2:
        if (anjay_attr_storage_set_instance_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    (anjay_iid_t) iid, &attrs.common)) {
            demo_log(ERROR, "failed to set instance level attributes");
        }
        goto finish;
    case 1:
        if (anjay_attr_storage_set_object_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    &attrs.common)) {
            demo_log(ERROR, "failed to set object level attributes");
        }
        goto finish;
    }
error:
    demo_log(ERROR, "bad syntax - see help");
finish:
    avs_free(path);
}
#endif // ANJAY_WITH_ATTR_STORAGE

static void cmd_disable_server(anjay_demo_t *demo, const char *args_string) {
    unsigned ssid;
    int timeout_s;
    if (sscanf(args_string, "%u %d", &ssid, &timeout_s) < 2
            || ssid > UINT16_MAX) {
        demo_log(ERROR, "invalid arguments");
        return;
    }

    avs_time_duration_t timeout = AVS_TIME_DURATION_INVALID;
    if (timeout_s >= 0) {
        timeout = avs_time_duration_from_scalar(timeout_s, AVS_TIME_S);
    }

    if (anjay_disable_server_with_timeout(demo->anjay, (anjay_ssid_t) ssid,
                                          timeout)) {
        demo_log(ERROR, "could not disable server with SSID %u", ssid);
        return;
    }
}

static void cmd_enable_server(anjay_demo_t *demo, const char *args_string) {
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }

    if (anjay_enable_server(demo->anjay, ssid)) {
        demo_log(ERROR, "could not enable server with SSID %" PRIu16, ssid);
        return;
    }
}

static void cmd_all_connections_failed(anjay_demo_t *demo,
                                       const char *unused_args) {
    (void) unused_args;
    printf("ALL_CONNECTIONS_FAILED==%d\n",
           (int) anjay_all_connections_failed(demo->anjay));
}

static void cmd_schedule_update_on_exit(anjay_demo_t *demo,
                                        const char *unused_args) {
    (void) unused_args;
    demo->schedule_update_on_exit = true;
}

#ifdef ANJAY_WITH_LWM2M11
static void cmd_set_queue_mode_preference(anjay_demo_t *demo,
                                          const char *args_string) {
    while (isspace(*args_string)) {
        ++args_string;
    }
    anjay_queue_mode_preference_t value;
    if (avs_strcasecmp(args_string, "FORCE_QUEUE_MODE") == 0) {
        value = ANJAY_FORCE_QUEUE_MODE;
    } else if (avs_strcasecmp(args_string, "PREFER_QUEUE_MODE") == 0) {
        value = ANJAY_PREFER_QUEUE_MODE;
    } else if (avs_strcasecmp(args_string, "PREFER_ONLINE_MODE") == 0) {
        value = ANJAY_PREFER_ONLINE_MODE;
    } else if (avs_strcasecmp(args_string, "FORCE_ONLINE_MODE") == 0) {
        value = ANJAY_FORCE_ONLINE_MODE;
    } else {
        demo_log(ERROR, "Invaild queue mode preference; supported values: "
                        "FORCE_QUEUE_MODE, PREFER_QUEUE_MODE, "
                        "PREFER_ONLINE_MODE, FORCE_ONLINE_MODE");
        return;
    }
    anjay_set_queue_mode_preference(demo->anjay, value);
}
#endif // ANJAY_WITH_LWM2M11

#ifdef ANJAY_WITH_OBSERVATION_STATUS
static void cmd_observation_status(anjay_demo_t *demo,
                                   const char *args_string) {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_rid_t rid;
    if (sscanf(args_string, " /%" SCNu16 "/%" SCNu16 "/%" SCNu16, &oid, &iid,
               &rid)
            != 3) {
        demo_log(WARNING,
                 "observation-status usage: observation_status /OID/IID/RID");
        return;
    }
    anjay_resource_observation_status_t status =
            anjay_resource_observation_status(demo->anjay, oid, iid, rid);
    demo_log(INFO,
             "anjay_resource_observation_status, is_observed == %s, "
             "min_period == %" PRId32 ", max_eval_period == %" PRId32,
             status.is_observed ? "true" : "false", status.min_period,
             status.max_eval_period);
#    if (ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER > 0)
    if (status.servers_number > 0) {
        char *ssid_list =
                (char *) avs_calloc((AVS_UINT_STR_BUF_SIZE(anjay_ssid_t) + 2)
                                            * status.servers_number,
                                    1);
        if (ssid_list) {
            for (uint16_t i = 0; i < status.servers_number; i++) {
                char ssid_string[AVS_UINT_STR_BUF_SIZE(anjay_ssid_t) + 2];
                sprintf(ssid_string, " %" PRIu16 ",", status.servers[i]);
                strcat(ssid_list, ssid_string);
            }
            ssid_list[strlen(ssid_list) - 1] = '\0'; // remove trailing comma
            demo_log(INFO, "SSIDs of servers observing given path:%s",
                     ssid_list);
            avs_free(ssid_list);
        }
    }
#    endif //(ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER > 0)
}
#endif // ANJAY_WITH_OBSERVATION_STATUS

static void cmd_badc_write(anjay_demo_t *demo, const char *args_string) {
    anjay_iid_t iid;
    anjay_riid_t riid;
    int length;
    if (sscanf(args_string, " %" SCNu16 " %" SCNu16 " %n", &iid, &riid, &length)
            < 2) {
        demo_log(ERROR, "invalid format");
        return;
    }
    binary_app_data_container_write(demo->anjay, demo_find_object(demo, 19),
                                    iid, riid, &args_string[length]);
}

static void cmd_advance_time(anjay_demo_t *demo, const char *args_string) {
    (void) demo;
    double delta_s = 0;
    if (sscanf(args_string, " %lf", &delta_s) != 1) {
        demo_log(ERROR,
                 "bad time format, expected seconds as floating point number");
        return;
    }
    demo_advance_time(avs_time_duration_from_fscalar(delta_s, AVS_TIME_S));
}

static void cmd_set_event_log_data(anjay_demo_t *demo,
                                   const char *args_string) {
    const anjay_dm_object_def_t **obj_def =
            demo_find_object(demo, DEMO_OID_EVENT_LOG);
    if (!obj_def) {
        demo_log(ERROR, "failed to find Event Log object");
    }
    size_t data_size = strlen(args_string);
    const char *data_ptr = args_string;
    if (data_size) {
        // Discard the space character
        ++data_ptr;
        --data_size;
    }

    if (event_log_write_data(demo->anjay, obj_def, data_ptr, data_size)) {
        demo_log(ERROR, "failed to write Event Log data");
    }
}

#ifdef ANJAY_WITH_MODULE_FW_UPDATE
static void cmd_set_fw_update_result(anjay_demo_t *demo,
                                     const char *args_string) {
    int result;
    if (sscanf(args_string, " %d", &result) != 1) {
        demo_log(ERROR, "Firmware Update result not specified");
        return;
    }
    anjay_fw_update_set_result(demo->anjay, (anjay_fw_update_result_t) result);
}

#endif // ANJAY_WITH_MODULE_FW_UPDATE

static void cmd_ongoing_registration_exists(anjay_demo_t *demo,
                                            const char *args_string) {
    (void) args_string;
    printf("ONGOING_REGISTRATION==%s\n",
           anjay_ongoing_registration_exists(demo->anjay) ? "true" : "false");
}

static void cmd_set_lifetime(anjay_demo_t *demo, const char *args_string) {
    anjay_iid_t iid;
    int32_t lifetime;
    if (sscanf(args_string, "%" SCNu16 " %" SCNd32, &iid, &lifetime) != 2) {
        demo_log(ERROR, "The command requires both Instance ID and Lifetime");
        return;
    }
    if (anjay_server_object_set_lifetime(demo->anjay, iid, lifetime)) {
        demo_log(ERROR, "Could not set server lifetime to the desired value");
    }
}

static void cmd_registration_expiration_time(anjay_demo_t *demo,
                                             const char *args_string) {
    anjay_ssid_t ssid;
    if (parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }

    demo_log(INFO, "REGISTRATION_EXPIRATION_TIME=%s",
             AVS_TIME_DURATION_AS_STRING(
                     anjay_registration_expiration_time(demo->anjay, ssid)
                             .since_real_epoch));
}

static void cmd_next_lifecycle_operation(anjay_demo_t *demo,
                                         const char *args_string) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    anjay_transport_set_t transport_set;
    if (!*args_string || !parse_ssid(args_string, &ssid)) {
        result = anjay_next_planned_lifecycle_operation(demo->anjay, ssid);
    } else if (!parse_transports(args_string, &transport_set)) {
        result =
                anjay_transport_next_planned_lifecycle_operation(demo->anjay,
                                                                 transport_set);
    } else {
        return;
    }

    demo_log(INFO, "NEXT_LIFECYCLE_OPERATION=%s",
             AVS_TIME_DURATION_AS_STRING(result.since_real_epoch));
}

static void cmd_next_planned_notify(anjay_demo_t *demo,
                                    const char *args_string) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    anjay_transport_set_t transport_set;
    if (!*args_string || !parse_ssid(args_string, &ssid)) {
        result = anjay_next_planned_notify_trigger(demo->anjay, ssid);
    } else if (!parse_transports(args_string, &transport_set)) {
        result = anjay_transport_next_planned_notify_trigger(demo->anjay,
                                                             transport_set);
    } else {
        return;
    }

    demo_log(INFO, "NEXT_PLANNED_NOTIFY=%s",
             AVS_TIME_DURATION_AS_STRING(result.since_real_epoch));
}

static void cmd_next_planned_pmax_notify(anjay_demo_t *demo,
                                         const char *args_string) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    anjay_transport_set_t transport_set;
    if (!*args_string || !parse_ssid(args_string, &ssid)) {
        result = anjay_next_planned_pmax_notify_trigger(demo->anjay, ssid);
    } else if (!parse_transports(args_string, &transport_set)) {
        result =
                anjay_transport_next_planned_pmax_notify_trigger(demo->anjay,
                                                                 transport_set);
    } else {
        return;
    }

    demo_log(INFO, "NEXT_PLANNED_PMAX_NOTIFY=%s",
             AVS_TIME_DURATION_AS_STRING(result.since_real_epoch));
}

static void cmd_has_unsent_notifications(anjay_demo_t *demo,
                                         const char *args_string) {
    bool result = false;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    anjay_transport_set_t transport_set;
    if (!*args_string || !parse_ssid(args_string, &ssid)) {
        result = anjay_has_unsent_notifications(demo->anjay, ssid);
    } else if (!parse_transports(args_string, &transport_set)) {
        result = anjay_transport_has_unsent_notifications(demo->anjay,
                                                          transport_set);
    } else {
        return;
    }

    demo_log(INFO, "HAS_UNSENT_NOTIFICATIONS=%s", result ? "true" : "false");
}

#ifdef ANJAY_WITH_MODULE_IPSO_OBJECTS
static void cmd_temperature_add_instance(anjay_demo_t *demo,
                                         const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    temperature_add_instance(demo->anjay, iid);
}

static void cmd_temperature_remove_instance(anjay_demo_t *demo,
                                            const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    temperature_remove_instance(demo->anjay, iid);
}

static void cmd_accelerometer_add_instance(anjay_demo_t *demo,
                                           const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    accelerometer_add_instance(demo->anjay, iid);
}

static void cmd_accelerometer_remove_instance(anjay_demo_t *demo,
                                              const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    accelerometer_remove_instance(demo->anjay, iid);
}

static void cmd_push_button_add_instance(anjay_demo_t *demo,
                                         const char *args_string) {
    anjay_iid_t iid;
    char application_type[40];
    if (sscanf(args_string, " %" SCNu16 " %39s", &iid, application_type) < 2) {
        demo_log(ERROR, "invalid format");
        return;
    }

    anjay_ipso_button_instance_add(demo->anjay, iid, application_type);
}

static void cmd_push_button_remove_instance(anjay_demo_t *demo,
                                            const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    anjay_ipso_button_instance_remove(demo->anjay, iid);
}

static void cmd_push_button_press(anjay_demo_t *demo, const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    anjay_ipso_button_update(demo->anjay, iid, true);
}

static void cmd_push_button_release(anjay_demo_t *demo,
                                    const char *args_string) {
    anjay_iid_t iid;
    if (sscanf(args_string, " %" SCNu16, &iid) < 1) {
        demo_log(ERROR, "invalid format");
        return;
    }

    anjay_ipso_button_update(demo->anjay, iid, false);
}
#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS

static void cmd_set_tx_params(anjay_demo_t *demo, const char *args_string) {
    avs_coap_udp_tx_params_t tx_params;
    double ack_timeout_s;
    char transport_str[16];
    if (sscanf(args_string, " %15s %lf %lf %u %lu", transport_str,
               &ack_timeout_s, &tx_params.ack_random_factor,
               &tx_params.max_retransmit, &tx_params.nstart)
            < 5) {
        demo_log(ERROR, "invalid format");
        return;
    }

    tx_params.ack_timeout =
            avs_time_duration_from_fscalar(ack_timeout_s, AVS_TIME_S);

    anjay_transport_set_t transport_set;
    if (parse_transports(transport_str, &transport_set)) {
        return;
    }

    anjay_update_transport_tx_params(demo->anjay, transport_set, &tx_params);
}

static void cmd_set_coap_exchange_timeout(anjay_demo_t *demo,
                                          const char *args_string) {
    double exchange_timeout_s;
    char transport_str[16];
    if (sscanf(args_string, " %15s %lf", transport_str, &exchange_timeout_s)
            < 2) {
        demo_log(ERROR, "invalid format");
        return;
    }

    avs_time_duration_t exchange_timeout =
            avs_time_duration_from_fscalar(exchange_timeout_s, AVS_TIME_S);

    anjay_transport_set_t transport_set;
    if (parse_transports(transport_str, &transport_set)) {
        return;
    }

    anjay_update_coap_exchange_timeout(demo->anjay, transport_set,
                                       exchange_timeout);
}

static void cmd_set_dtls_timeouts(anjay_demo_t *demo, const char *args_string) {
    double min_timeout_s, max_timeout_s;
    if (sscanf(args_string, "%lf %lf", &min_timeout_s, &max_timeout_s) < 2) {
        demo_log(ERROR, "invalid format");
        return;
    }

    avs_net_dtls_handshake_timeouts_t dtls_handshake_timeouts = {
        .min = avs_time_duration_from_fscalar(min_timeout_s, AVS_TIME_S),
        .max = avs_time_duration_from_fscalar(max_timeout_s, AVS_TIME_S)
    };

    anjay_update_dtls_handshake_timeouts(demo->anjay, dtls_handshake_timeouts);
}

#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
static void cmd_last_registration_time(anjay_demo_t *demo,
                                       const char *args_string) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;

    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }
    avs_error_t err =
            anjay_get_server_last_registration_time(demo->anjay, ssid, &result);

    if (!avs_is_err(err)) {
        demo_log(INFO, "LAST_REGISTRATION_TIME=%s",
                 AVS_TIME_DURATION_AS_STRING(result.since_real_epoch));
    } else {
        demo_log(INFO, "Failed to get last registration time");
    }
}

static void cmd_next_update_time(anjay_demo_t *demo, const char *args_string) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;

    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }
    avs_error_t err =
            anjay_get_server_next_update_time(demo->anjay, ssid, &result);

    if (!avs_is_err(err)) {
        demo_log(INFO, "NEXT_UPDATE_TIME=%s",
                 AVS_TIME_DURATION_AS_STRING(result.since_real_epoch));
    } else {
        demo_log(INFO, "Failed to get next update time");
    }
}

static void cmd_last_communication_time(anjay_demo_t *demo,
                                        const char *args_string) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    anjay_ssid_t ssid = ANJAY_SSID_ANY;

    if (*args_string && parse_ssid(args_string, &ssid)) {
        demo_log(ERROR, "invalid Short Server ID: %s", args_string);
        return;
    }
    avs_error_t err = anjay_get_server_last_communication_time(demo->anjay,
                                                               ssid, &result);
    if (!avs_is_err(err)) {
        demo_log(INFO, "LAST_COMMUNICATION_TIME=%s",
                 AVS_TIME_DURATION_AS_STRING(result.since_real_epoch));
    } else {
        demo_log(INFO, "Failed to get last communication time");
    }
}
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API

static void cmd_help(anjay_demo_t *demo, const char *args_string);

struct cmd_handler_def {
    const char *cmd_name;
    size_t cmd_name_length;
    void (*handler)(anjay_demo_t *, const char *);
    const char *help_args;
    const char *help_descr;
};

#define CMD_HANDLER(name, args, func, help) \
    { (name), sizeof(name) - 1, (func), (args), (help) }
static const struct cmd_handler_def COMMAND_HANDLERS[] = {
    // clang-format off
    CMD_HANDLER("send-register", "[ssid=0]",
                cmd_send_register, "Sends Register messages to LwM2M servers"),
    CMD_HANDLER("send-update", "[ssid=0]",
                cmd_send_update, "Sends Update messages to LwM2M servers"),
    CMD_HANDLER("reconnect-server", "ssid", cmd_reconnect_server,
                "Reconnects a server with given SSID"),
    CMD_HANDLER("reconnect", "[transports...]", cmd_reconnect,
                "Reconnects to LwM2M servers and sends Update messages"),
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    CMD_HANDLER("set-fw-package-path", "", cmd_set_fw_package_path,
                "Sets the path where the firmware package will be saved when "
                "Write /5/0/0 is performed"),
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
    CMD_HANDLER("set-afu-package-path", "", cmd_set_afu_package_path,
                "Sets the path where the firmware package will be saved when "
                "Write /" AVS_QUOTE_MACRO(ANJAY_ADVANCED_FW_UPDATE_OID) "/0/0 is performed. Only applied to instance 0."),
    CMD_HANDLER("get-afu-deadline", "", cmd_get_afu_deadline,
                "Gets the Advanced Firmware Update deadline (only for main APP "
                "image)"),
    CMD_HANDLER("set-afu-result", "RESULT", cmd_set_afu_result,
                "Attempts to set Advanced Firmware Update Result of instance "
                "/" AVS_QUOTE_MACRO(ANJAY_ADVANCED_FW_UPDATE_OID) "/0 (APP) at runtime"),
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
    CMD_HANDLER("open-location-csv", "filename frequency=1",
                cmd_open_location_csv,
                "Opens a CSV file and starts using it for location information"),
    CMD_HANDLER("add-server", "uri",
                cmd_add_server, "Adds another LwM2M Server to connect to"),
    CMD_HANDLER("trim-servers", "number",
                cmd_trim_servers,
                "Remove LwM2M Servers with specified ID and higher from the "
                "set of servers provided on the command line, and reload the "
                "Server Accounts. Note that any changes to the Security and "
                "Server objects performed by the Bootstrap Server will be "
                "discarded."),
    CMD_HANDLER("socket-count", "", cmd_socket_count,
                "Display number of sockets currently listening"),
    CMD_HANDLER("get-port", "index", cmd_get_port,
                "Display listening port number of a socket with the specified "
                "index (also supports Python-like negative indices)"),
    CMD_HANDLER("non-lwm2m-socket-count", "", cmd_non_lwm2m_socket_count,
                "Display number of sockets currently listening that are not "
                "affiliated to any LwM2M server connetion"),
    CMD_HANDLER("get-transport", "index", cmd_get_transport,
                "Display transport used by a socket with the specified index "
                "(also supports Python-like negative indices)"),
    CMD_HANDLER("enter-offline", "[transports...]", cmd_enter_offline,
                "Enters Offline mode"),
    CMD_HANDLER("exit-offline", "[transports...]", cmd_exit_offline,
                "Exits Offline mode"),
    CMD_HANDLER("notify", "", cmd_notify,
                "Executes anjay_notify_* on a specified path"),
#ifdef ANJAY_WITH_SEND
    CMD_HANDLER("send_deferrable", "SSID [/OID/IID/RID [...]]",
                cmd_send_deferrable,
                "Executes anjay_send_deferrable on a specified path"),
    CMD_HANDLER("send", "SSID [/OID/IID/RID [...]]", cmd_send,
                "Executes anjay_send on a specified path"),
#endif // ANJAY_WITH_SEND
    CMD_HANDLER("unregister-object", "oid", cmd_unregister_object,
                "Unregister an LwM2M Object"),
    CMD_HANDLER("reregister-object", "oid", cmd_reregister_object,
                "Re-register a previously unregistered LwM2M Object"),
    CMD_HANDLER("download-blocks",
                "url target_file [offset1-offset2 [offset3-[offset4 [...]]]]",
                cmd_download_blocks,
                "Download portions of a given URL to target_file."),
    CMD_HANDLER("download", "url target_file [psk_identity psk_key]",
                cmd_download,
                "Download a file from given URL to target_file."),
#ifdef ANJAY_WITH_ATTR_STORAGE
#        define SUPPORTED_ATTRS "pmin,pmax,lt,gt,st,epmin,epmax"
    CMD_HANDLER("set-attrs", "", cmd_set_attrs, "Syntax [/a [/b [/c [/d] ] ] ] "
                "ssid [" SUPPORTED_ATTRS "] - e.g. /a/b 1 pmin=3,pmax=4"),
#    undef SUPPORTED_ATTRS
#endif // ANJAY_WITH_ATTR_STORAGE
    CMD_HANDLER("disable-server", "ssid reactivate_timeout", cmd_disable_server,
                "Disables a server with given SSID for a given time "
                "(use -1 to disable idefinitely)."),
    CMD_HANDLER("enable-server", "ssid", cmd_enable_server,
                "Enables a server with given SSID."),
    CMD_HANDLER("get-all-connections-failed", "", cmd_all_connections_failed,
                "Returns the result of anjay_all_connections_failed()"),
    CMD_HANDLER("schedule-update-on-exit", "", cmd_schedule_update_on_exit,
                "Ensure Registration Update is scheduled for immediate "
                "execution at the point of calling anjay_delete()"),
#ifdef ANJAY_WITH_LWM2M11
    CMD_HANDLER("set-queue-mode-preference", "PREFERENCE",
                cmd_set_queue_mode_preference,
                "Sets queue mode preference; one of: FORCE_QUEUE_MODE, "
                "PREFER_QUEUE_MODE, PREFER_ONLINE_MODE, FORCE_ONLINE_MODE"),
#endif // ANJAY_WITH_LWM2M11
    CMD_HANDLER("set-lifetime", "IID LIFETIME", cmd_set_lifetime,
                "Sets the lifetime for the specified Server Instance ID"),
    CMD_HANDLER("advance-time", "", cmd_advance_time,
                "Advances real and monotonic clock readings by specified "
                "number of seconds"),
#ifdef ANJAY_WITH_OBSERVATION_STATUS
    CMD_HANDLER("observation-status", "/OID/IID/RID", cmd_observation_status,
                "Queries the observation status of a given Resource"),
#endif // ANJAY_WITH_OBSERVATION_STATUS
    CMD_HANDLER("badc-write", "IID RIID value", cmd_badc_write,
                "Writes new value to Binary App Data Container object"),
    CMD_HANDLER("set-event-log-data", "data", cmd_set_event_log_data,
                "Sets LogData resource in Log Event object"),
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    CMD_HANDLER("set-fw-update-result", "RESULT", cmd_set_fw_update_result,
                "Attempts to set Firmware Update Result at runtime"),
#endif // ANJAY_WITH_MODULE_FW_UPDATE
    CMD_HANDLER("ongoing-registration-exists", "",
                cmd_ongoing_registration_exists,
                "Display information about ongoing registrations"),
#ifdef ANJAY_WITH_MODULE_IPSO_OBJECTS
    CMD_HANDLER("temperature-add-instance", "IID",
                cmd_temperature_add_instance,
                "Adds a new instance of the fake Temperature object. Maximal "
                "IID of such instance is 16."),
    CMD_HANDLER("temperature-remove-instance", "IID",
                cmd_temperature_remove_instance,
                "Removes instance of the fake Temperature object"),
    CMD_HANDLER("accelerometer-add-instance", "IID",
                cmd_accelerometer_add_instance,
                "Adds a new instance of the fake Accelerometer object. Maximal "
                "IID of such instance is 16"),
    CMD_HANDLER("accelerometer-remove-instance", "IID",
                cmd_accelerometer_remove_instance,
                "Removes instance of the fake Accelerometer object"),
    CMD_HANDLER("push-button-add-instance", "IID application_type",
                cmd_push_button_add_instance,
                "Adds new instance of the fake Push Button object. Maximal "
                "IID of such instance is 16. The initial value of the \"Application type\" "
                "string will be set to application_type."),
    CMD_HANDLER("push-button-remove-instance", "IID",
                cmd_push_button_remove_instance,
                "Removes the selected instance of the fake Push Button object"),
    CMD_HANDLER("push-button-press", "IID",
                cmd_push_button_press,
                "Presses the selected instance of the fake Push Button object"),
    CMD_HANDLER("push-button-release", "IID",
                cmd_push_button_release,
                "Releases the selected instance of the fake Push Button object."),
#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS
    CMD_HANDLER("registration-expiration-time", "SSID",
                cmd_registration_expiration_time,
                "Displays time when registration with a given server expires"),
    CMD_HANDLER("next-lifecycle-operation", "[SSID|transports...]",
                cmd_next_lifecycle_operation,
                "Displays time when next lifecycle operation is scheduled for "
                "any server (if no arguments specified), a given server (if "
                "numeric SSID argument given) or a given set of transports "
                "(if transport names given)"),
    CMD_HANDLER("next-planned-notify", "[SSID|transports...]",
                cmd_next_planned_notify,
                "Displays time when next planned notification trigger is "
                "scheduled for any server (if no arguments specified), a given "
                "server (if numeric SSID argument given) or a given set of "
                "transports (if transport names given)"),
    CMD_HANDLER("next-planned-pmax-notify", "[SSID|transports...]",
                cmd_next_planned_pmax_notify,
                "Displays time when next planned notification trigger based on "
                "the Maximum Period attribute is scheduled for any server (if "
                "no arguments specified), a given server (if numeric SSID "
                "argument given) or a given set of transports (if transport "
                "names given)"),
    CMD_HANDLER("has-unsent-notifications", "[SSID|transports...]",
                cmd_has_unsent_notifications,
                "Checks whether there are some notifications which have been "
                "postponed to be sent later for any server (if no arguments "
                "specified), a given server (if numeric SSID argument given) "
                "or a given set of transports (if transport names given)"),
    CMD_HANDLER("set-tx-param", "transport ack_timeout ack_random_factor "
                "max_retransmit nstart", cmd_set_tx_params,
                "Sets transmission parameters for a given transport. Available "
                "transports are sms, udp and nidd."),
    CMD_HANDLER("set-coap-exchange-timeout", "transport timeout",
                cmd_set_coap_exchange_timeout,
                "Sets maximal length of the CoAP exchange."),
    CMD_HANDLER("set-dtls-handshake-timeout", "min max", cmd_set_dtls_timeouts,
                "Sets DTLS handshake timeouts for all of the used DTLS sockets."),
#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    CMD_HANDLER("last-registration-time", "[SSID]",
                cmd_last_registration_time,
                "Displays time of the last registration operation with any "
                "server (if no argument specified) or a given server (if "
                "numeric SSID argument given)."),
    CMD_HANDLER("next-update-time", "[SSID]",
                cmd_next_update_time,
                "Displays time when next update operation is scheduled for "
                "any server (if no argument specified) or a given server (if "
                "numeric SSID argument given)."),
    CMD_HANDLER("last-communication-time", "[SSID]",
                cmd_last_communication_time,
                "Displays time of the last communication with any server (if "
                "no argument specified) or a given server (if numeric SSID "
                "argument given)."),
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    CMD_HANDLER("help", "", cmd_help, "Prints this message")
    // clang-format on
};
#undef CMD_HANDLER

static void print_line_with_indent(const char *line, const char *end) {
    static const int INDENT = 5;
    static const int SCREEN_WIDTH = 80;
    const int MAX_LINE_LENGTH = SCREEN_WIDTH - INDENT - 1;
    if (end - line > MAX_LINE_LENGTH) {
        const char *prev = line;
        const char *last = line;
        while (last && (last - line) <= MAX_LINE_LENGTH) {
            prev = last;
            last = strchr(last + 1, ' ');
        }
        if (prev == line) {
            prev = last;
        }
        if (prev && prev != end) {
            print_line_with_indent(line, prev);
            print_line_with_indent(prev + 1, end);
            return;
        }
    }
    for (int i = 0; i < INDENT; ++i) {
        putchar(' ');
    }
    fwrite(line, 1, (size_t) (end - line), stdout);
    putchar('\n');
}

static void print_with_indent(const char *text) {
    while (*text) {
        const char *end = strchr(text, '\n');
        if (!end) {
            end = text + strlen(text);
        }
        print_line_with_indent(text, end);
        text = end;
        if (*text) {
            ++text;
        }
    }
}

static void cmd_help(anjay_demo_t *demo, const char *args_string) {
    (void) demo;
    (void) args_string;

    puts("---");
    puts("LwM2M Demo client");
    puts("Available commands:");
    for (size_t idx = 0; idx < AVS_ARRAY_SIZE(COMMAND_HANDLERS); ++idx) {
        const struct cmd_handler_def *cmd = &COMMAND_HANDLERS[idx];
        printf("\n%s %s\n", cmd->cmd_name, cmd->help_args);
        print_with_indent(cmd->help_descr);
    }
    puts("---");
}

static void handle_command(avs_sched_t *sched, const void *invocation_) {
    (void) sched;
    const demo_command_invocation_t *invocation =
            (const demo_command_invocation_t *) invocation_;
    if (invocation->cmd[0]) {
        const struct cmd_handler_def *cmd = NULL;

        for (size_t idx = 0; idx < AVS_ARRAY_SIZE(COMMAND_HANDLERS); ++idx) {
            const struct cmd_handler_def *candidate_cmd =
                    &COMMAND_HANDLERS[idx];

            if (strncmp(invocation->cmd, candidate_cmd->cmd_name,
                        candidate_cmd->cmd_name_length)
                    == 0) {
                cmd = candidate_cmd;
                break;
            }
        }

        if (cmd) {
            demo_log(INFO, "command: %s", invocation->cmd);
            cmd->handler(invocation->demo,
                         invocation->cmd + cmd->cmd_name_length);
        } else {
            demo_log(ERROR, "unrecognized command: %s", invocation->cmd);
        }
    }

    fprintf(stdout, "(DEMO)>");
    fflush(stdout);
}

void demo_command_dispatch(const demo_command_invocation_t *invocation) {
    if (AVS_SCHED_NOW(anjay_get_scheduler(invocation->demo->anjay), NULL,
                      handle_command, invocation,
                      offsetof(demo_command_invocation_t, cmd)
                              + strlen(invocation->cmd) + 1)) {
        demo_log(ERROR, "Could not schedule handle_command");
    }
}
