/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_COAP_DOWNLOAD

#    ifndef ANJAY_WITH_DOWNLOADER
#        error "ANJAY_WITH_COAP_DOWNLOAD requires ANJAY_WITH_DOWNLOADER to be enabled"
#    endif // ANJAY_WITH_DOWNLOADER

#    include <inttypes.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_utils.h>

#    include <avsystem/coap/avs_coap_config.h>
#    include <avsystem/coap/coap.h>

#    include <avsystem/commons/avs_shared_buffer.h>

#    define ANJAY_DOWNLOADER_INTERNALS

#    include "anjay_private.h"

VISIBILITY_SOURCE_BEGIN

AVS_STATIC_ASSERT(offsetof(anjay_etag_t, value)
                          == offsetof(avs_coap_etag_t, bytes),
                  coap_etag_layout_compatible);
AVS_STATIC_ASSERT(AVS_ALIGNOF(anjay_etag_t) == AVS_ALIGNOF(avs_coap_etag_t),
                  coap_etag_alignment_compatible);

typedef struct {
    anjay_download_ctx_common_t common;

    anjay_socket_transport_t transport;
    anjay_url_t uri;
    size_t bytes_downloaded;
    size_t initial_block_size;
    avs_coap_etag_t etag;

    avs_net_socket_t *socket;
    avs_net_resolved_endpoint_t preferred_endpoint;
    char dtls_session_buffer[ANJAY_DTLS_SESSION_BUFFER_SIZE];

    avs_coap_exchange_id_t exchange_id;
#    ifdef WITH_AVS_COAP_UDP
    avs_coap_udp_tx_params_t tx_params;
#    endif // WITH_AVS_COAP_UDP
    avs_coap_ctx_t *coap;

    avs_sched_handle_t job_start;
    bool aborting;
    bool reconnecting;
} anjay_coap_download_ctx_t;

typedef struct {
    avs_coap_ctx_t *coap_ctx;
    avs_net_socket_t *socket;
} cleanup_coap_context_args_t;

static void cleanup_coap_context_unlocked(anjay_unlocked_t *anjay,
                                          cleanup_coap_context_args_t args) {
    _anjay_coap_ctx_cleanup(anjay, &args.coap_ctx);
#    ifndef ANJAY_TEST
    _anjay_socket_cleanup(anjay, &args.socket);
#    endif // ANJAY_TEST
}

static void cleanup_coap_context(avs_sched_t *sched, const void *args) {
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    cleanup_coap_context_unlocked(anjay,
                                  *(const cleanup_coap_context_args_t *) args);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static void cleanup_coap_transfer(AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    avs_sched_del(&ctx->job_start);
    _anjay_url_cleanup(&ctx->uri);

    if (ctx->common.same_socket_download) {
        // nothing more to cleanup here - both CoAP ctx and socket are
        // maintained by the upper layers
        ctx->aborting = true;
        avs_coap_exchange_cancel(ctx->coap, ctx->exchange_id);
        AVS_LIST_DELETE(ctx_ptr);
        return;
    }
    anjay_unlocked_t *anjay = _anjay_downloader_get_anjay(ctx->common.dl);

    const cleanup_coap_context_args_t args = {
        .coap_ctx = ctx->coap,
        .socket = ctx->socket
    };
    if (ctx->coap) {
        ctx->aborting = true;
        /**
         * HACK: this is necessary, because if the download is canceled
         * externally, cleanup_coap_context() would be called after "ctx_ptr" is
         * freed. The problem is: cleanup_coap_context() leads to exchange
         * cancelation, which calls handle_coap_response, and that'd use already
         * freed memory (i.e. from "ctx_ptr"). It's also non-trivial to move the
         * AVS_LIST_DELETE(ctx_ptr) to cleanup_coap_context().
         */
        avs_coap_exchange_cancel(ctx->coap, ctx->exchange_id);
        /**
         * HACK: this is necessary, because CoAP context may be destroyed while
         * handling a response, and when the control returns, it may access some
         * of its internal fields.
         */
        if (!anjay->sched
                || AVS_SCHED_NOW(anjay->sched, NULL, cleanup_coap_context,
                                 &args, sizeof(args))) {
            cleanup_coap_context_unlocked(NULL, args);
        }
    }
    AVS_LIST_DELETE(ctx_ptr);
}

static int read_etag(const avs_coap_response_header_t *hdr,
                     avs_coap_etag_t *out_etag) {
    switch (avs_coap_options_get_etag(&hdr->options, out_etag)) {
    case 0:
        break;
    case AVS_COAP_OPTION_MISSING:
        dl_log(TRACE, _("no ETag option"));
        return 0;
    default:
        dl_log(DEBUG, _("invalid ETag option size"));
        return -1;
    }

    dl_log(TRACE, _("ETag: ") "%s", AVS_COAP_ETAG_HEX(out_etag));
    return 0;
}

static inline bool etag_matches(const avs_coap_etag_t *a,
                                const avs_coap_etag_t *b) {
    return a->size == b->size && !memcmp(a->bytes, b->bytes, a->size);
}

static void abort_download_transfer(anjay_coap_download_ctx_t *dl_ctx,
                                    anjay_download_status_t status) {
    if (dl_ctx->aborting) {
        return;
    }
    // avoid all kinds of situations in which abort_download_transfer() may be
    // called more than once which would lead to use-after-free.
    dl_ctx->aborting = true;

    avs_coap_exchange_cancel(dl_ctx->coap, dl_ctx->exchange_id);
    assert(!avs_coap_exchange_id_valid(dl_ctx->exchange_id));

    AVS_LIST(anjay_download_ctx_t) *dl_ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(dl_ctx->common.dl,
                                                 dl_ctx->common.id);
    if (dl_ctx_ptr) {
        _anjay_downloader_abort_transfer(dl_ctx_ptr, status);
    }
}

static void
handle_coap_response(avs_coap_ctx_t *ctx,
                     avs_coap_exchange_id_t id,
                     avs_coap_client_request_state_t result,
                     const avs_coap_client_async_response_t *response,
                     avs_error_t err,
                     void *arg) {
    (void) ctx;
    anjay_coap_download_ctx_t *dl_ctx = (anjay_coap_download_ctx_t *) arg;

    assert(dl_ctx->exchange_id.value == id.value);
    (void) id;
    if (result != AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT) {
        // The exchange is being finished one way or another, so let's set the
        // exchange_id field so that it can be used to check if there is an
        // ongoing exchange or not (it is checked in suspend_coap_transfer()
        // and reconnect_coap_transfer()).
        dl_ctx->exchange_id = AVS_COAP_EXCHANGE_ID_INVALID;
    }

    switch (result) {
    case AVS_COAP_CLIENT_REQUEST_OK:
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT: {
        const uint8_t code = response->header.code;
        if (code != AVS_COAP_CODE_CONTENT) {
            dl_log(DEBUG,
                   _("server responded with ") "%s" _(" (expected ") "%s" _(
                           ")"),
                   AVS_COAP_CODE_STRING(code),
                   AVS_COAP_CODE_STRING(AVS_COAP_CODE_CONTENT));
            abort_download_transfer(
                    dl_ctx, _anjay_download_status_invalid_response(code));
            return;
        }
        avs_coap_etag_t etag;
        if (read_etag(&response->header, &etag)) {
            dl_log(DEBUG, _("could not parse CoAP response"));
            abort_download_transfer(dl_ctx,
                                    _anjay_download_status_failed(
                                            avs_errno(AVS_EPROTO)));
            return;
        }
        // NOTE: avs_coap normally performs ETag validation for blockwise
        // transfers. However, if we resumed the download from persistence
        // information, avs_coap wouldn't know about the ETag used before, and
        // would blindly accept any ETag.
        if (dl_ctx->etag.size == 0) {
            dl_ctx->etag = etag;
        } else if (!etag_matches(&dl_ctx->etag, &etag)) {
            dl_log(DEBUG, _("remote resource expired, aborting download"));
            abort_download_transfer(dl_ctx, _anjay_download_status_expired());
            return;
        }
        assert(dl_ctx->bytes_downloaded == response->payload_offset);
        if (avs_is_err((err = _anjay_downloader_call_on_next_block(
                                &dl_ctx->common,
                                (const uint8_t *) response->payload,
                                response->payload_size,
                                etag.size > 0 ? (const anjay_etag_t *) &etag
                                              : NULL)))) {
            abort_download_transfer(dl_ctx, _anjay_download_status_failed(err));
            return;
        }
        if (dl_ctx->bytes_downloaded == response->payload_offset) {
            dl_ctx->bytes_downloaded += response->payload_size;
        }
        if (result == AVS_COAP_CLIENT_REQUEST_OK) {
            dl_log(INFO, _("transfer id = ") "%" PRIuPTR _(" finished"),
                   dl_ctx->common.id);
            abort_download_transfer(dl_ctx, _anjay_download_status_success());
        } else {
            dl_log(TRACE,
                   _("transfer id = ") "%" PRIuPTR _(": ") "%lu" _(
                           " B downloaded"),
                   dl_ctx->common.id, (unsigned long) dl_ctx->bytes_downloaded);
        }
        break;
    }
    case AVS_COAP_CLIENT_REQUEST_FAIL: {
        dl_log(DEBUG, _("download failed: ") "%s", AVS_COAP_STRERROR(err));
        if (err.category == AVS_COAP_ERR_CATEGORY
                && err.code == AVS_COAP_ERR_ETAG_MISMATCH) {
            abort_download_transfer(dl_ctx, _anjay_download_status_expired());
        } else {
            abort_download_transfer(dl_ctx, _anjay_download_status_failed(err));
        }
        break;
    }
    case AVS_COAP_CLIENT_REQUEST_CANCEL:
        dl_log(DEBUG, _("download request canceled"));
        if (!dl_ctx->reconnecting) {
            abort_download_transfer(dl_ctx, _anjay_download_status_aborted());
        }
        break;
    }
}

static void handle_coap_message(AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    // NOTE: The return value is ignored as there is not a lot we can do with
    // it.
    (void) avs_coap_async_handle_incoming_packet(
            ((anjay_coap_download_ctx_t *) *ctx_ptr)->coap, NULL, NULL);
}

static avs_net_socket_t *get_coap_socket(anjay_download_ctx_t *ctx) {
    return ((anjay_coap_download_ctx_t *) ctx)->socket;
}

static anjay_socket_transport_t
get_coap_socket_transport(anjay_download_ctx_t *ctx) {
    return ((anjay_coap_download_ctx_t *) ctx)->transport;
}

#    ifdef ANJAY_TEST
#        include "tests/core/socket_mock.h"
#    endif // ANJAY_TEST

static void start_download_job(avs_sched_t *sched, const void *id_ptr) {
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    uintptr_t id = *(const uintptr_t *) id_ptr;
    AVS_LIST(anjay_download_ctx_t) *dl_ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!dl_ctx_ptr) {
        dl_log(DEBUG, _("download id = ") "%" PRIuPTR _(" expired"), id);
    } else {
        anjay_coap_download_ctx_t *ctx =
                (anjay_coap_download_ctx_t *) *dl_ctx_ptr;
        ctx->reconnecting = false;

        avs_error_t err;
        avs_coap_options_t options;
        const uint8_t code = AVS_COAP_CODE_GET;
        if (avs_is_err((err = avs_coap_options_dynamic_init(&options)))) {
            dl_log(ERROR,
                   _("download id = ") "%" PRIuPTR _(
                           " cannot start: out of memory"),
                   id);
            goto end;
        }

        AVS_LIST(const anjay_string_t) elem;
        AVS_LIST_FOREACH(elem, ctx->uri.uri_path) {
            if (avs_is_err((err = avs_coap_options_add_string(
                                    &options, AVS_COAP_OPTION_URI_PATH,
                                    elem->c_str)))) {
                goto end;
            }
        }
        AVS_LIST_FOREACH(elem, ctx->uri.uri_query) {
            if (avs_is_err((err = avs_coap_options_add_string(
                                    &options, AVS_COAP_OPTION_URI_QUERY,
                                    elem->c_str)))) {
                goto end;
            }
        }

        assert(!avs_coap_exchange_id_valid(ctx->exchange_id));
        (void) (avs_is_err((err = avs_coap_client_send_async_request(
                                    ctx->coap, &ctx->exchange_id,
                                    &(avs_coap_request_header_t) {
                                        .code = code,
                                        .options = options
                                    },
                                    NULL, NULL, handle_coap_response,
                                    (void *) ctx)))
                || avs_is_err((
                           err = avs_coap_client_set_next_response_payload_offset(
                                   ctx->coap, ctx->exchange_id,
                                   ctx->bytes_downloaded))));

    end:
        avs_coap_options_cleanup(&options);

        if (avs_is_err(err)) {
            _anjay_downloader_abort_transfer(
                    dl_ctx_ptr, _anjay_download_status_failed(err));
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static avs_error_t reset_coap_ctx(anjay_coap_download_ctx_t *ctx) {
    assert(!ctx->common.same_socket_download);
    anjay_unlocked_t *anjay = _anjay_downloader_get_anjay(ctx->common.dl);

    _anjay_coap_ctx_cleanup(anjay, &ctx->coap);
    assert(!avs_coap_exchange_id_valid(ctx->exchange_id));

    switch (ctx->transport) {
#    ifdef WITH_AVS_COAP_UDP
    case ANJAY_SOCKET_TRANSPORT_UDP:
        // NOTE: we set udp_response_cache to NULL, because it should never be
        // necessary. It's used to cache responses generated by us whenever we
        // handle an incoming request, and contexts used for downloads don't
        // expect receiving any requests that would need handling.
        if ((ctx->coap = avs_coap_udp_ctx_create(
                     _anjay_get_coap_sched(anjay), &ctx->tx_params,
                     anjay->in_shared_buffer, anjay->out_shared_buffer, NULL,
                     anjay->prng_ctx.ctx))) {
            avs_coap_set_exchange_max_time(ctx->coap,
                                           anjay->udp_exchange_timeout);
        }
        break;
#    endif // WITH_AVS_COAP_UDP

#    ifdef WITH_AVS_COAP_TCP
    case ANJAY_SOCKET_TRANSPORT_TCP:
        if ((ctx->coap = avs_coap_tcp_ctx_create(
                     _anjay_get_coap_sched(anjay), anjay->in_shared_buffer,
                     anjay->out_shared_buffer, anjay->coap_tcp_max_options_size,
                     anjay->coap_tcp_request_timeout, anjay->prng_ctx.ctx))) {
            avs_coap_set_exchange_max_time(ctx->coap,
                                           anjay->tcp_exchange_timeout);
        }
        break;
#    endif // WITH_AVS_COAP_TCP
    default:
        dl_log(ERROR,
               _("anjay_coap_download_ctx_t is compatible only with "
                 "ANJAY_SOCKET_TRANSPORT_UDP and "
                 "ANJAY_SOCKET_TRANSPORT_TCP (if they are compiled-in)"));
        return avs_errno(AVS_EPROTONOSUPPORT);
    }

    if (!ctx->coap) {
        dl_log(ERROR, _("could not create CoAP context"));
        return avs_errno(AVS_ENOMEM);
    }

    avs_error_t err = avs_coap_ctx_set_socket(ctx->coap, ctx->socket);
    if (avs_is_err(err)) {
        anjay_log(ERROR, _("could not assign socket to CoAP context"));
        _anjay_coap_ctx_cleanup(anjay, &ctx->coap);
    }

    return err;
}

static void suspend_coap_transfer(anjay_download_ctx_t *ctx_) {
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) ctx_;
    dl_log(INFO, _("suspending download ") "%" PRIuPTR, ctx->common.id);
    ctx->reconnecting = true;
    avs_sched_del(&ctx->job_start);
    if (avs_coap_exchange_id_valid(ctx->exchange_id)) {
        assert(ctx->coap);
        avs_coap_exchange_cancel(ctx->coap, ctx->exchange_id);
        assert(!avs_coap_exchange_id_valid(ctx->exchange_id));
    }
    if (ctx->common.same_socket_download) {
        return;
    }
    avs_error_t err = avs_net_socket_shutdown(ctx->socket);
    // not calling close because that might clean up remote hostname and
    // port fields that will be necessary for reconnection
    if (_anjay_socket_is_online(ctx->socket)) {
        // avs_net_socket_shutdown() failed - suspending the transfer is not
        // supported, let's abort it instead
        abort_download_transfer(
                ctx,
                _anjay_download_status_failed(
                        avs_is_err(err) ? err : avs_errno(AVS_UNKNOWN_ERROR)));
    }
}

static avs_error_t sched_download_resumption(anjay_coap_download_ctx_t *ctx) {
    anjay_unlocked_t *anjay = _anjay_downloader_get_anjay(ctx->common.dl);
    if (AVS_SCHED_NOW(anjay->sched, &ctx->job_start, start_download_job,
                      &ctx->common.id, sizeof(ctx->common.id))) {
        dl_log(WARNING,
               _("could not schedule resumption for download id "
                 "= ") "%" PRIuPTR,
               ctx->common.id);
        return avs_errno(AVS_ENOMEM);
    }
    dl_log(INFO, _("scheduling download ") "%" PRIuPTR _(" resumption"),
           ctx->common.id);
    return AVS_OK;
}

static avs_error_t
reconnect_coap_transfer(AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    (void) ctx_ptr;
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    ctx->reconnecting = true;

    if (ctx->common.same_socket_download) {
        // Cancel the exchange and schedule the download to let
        // the Registration be sent even if NSTART=1.
        avs_coap_exchange_cancel(ctx->coap, ctx->exchange_id);
        assert(!avs_coap_exchange_id_valid(ctx->exchange_id));
        return sched_download_resumption(ctx);
    }
    char hostname[ANJAY_MAX_URL_HOSTNAME_SIZE];
    char port[ANJAY_MAX_URL_PORT_SIZE];

    avs_error_t err;
    if (avs_is_err((err = avs_net_socket_get_remote_hostname(
                            ctx->socket, hostname, sizeof(hostname))))
            || avs_is_err((err = avs_net_socket_get_remote_port(
                                   ctx->socket, port, sizeof(port))))
            || ((void) avs_net_socket_shutdown(ctx->socket), 0)
            || ((void) avs_net_socket_close(ctx->socket), 0)
            || avs_is_err((err = avs_net_socket_connect(ctx->socket, hostname,
                                                        port)))) {
        dl_log(WARNING,
               _("could not reconnect socket for download id = ") "%" PRIuPTR,
               ctx->common.id);
        return err;
    } else {
        // A new DTLS session requires resetting the CoAP context.
        // If we manage to resume the session, we can simply continue sending
        // retransmissions as if nothing happened.
        if (!_anjay_was_session_resumed(ctx->socket)
                && avs_is_err((err = reset_coap_ctx(ctx)))) {
            return err;
        }
        if (!avs_coap_exchange_id_valid(ctx->exchange_id)) {
            return sched_download_resumption(ctx);
        }
    }
    return AVS_OK;
}

static avs_error_t set_next_coap_block_offset(anjay_download_ctx_t *ctx_,
                                              size_t next_block_offset) {
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) ctx_;
    avs_error_t err = AVS_OK;
    if (avs_coap_exchange_id_valid(ctx->exchange_id)) {
        err = avs_coap_client_set_next_response_payload_offset(
                ctx->coap, ctx->exchange_id, next_block_offset);
    }
    if (avs_is_ok(err)) {
        ctx->bytes_downloaded = next_block_offset;
    }
    return err;
}

avs_error_t
_anjay_downloader_coap_ctx_new(anjay_downloader_t *dl,
                               AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                               const anjay_download_config_t *cfg,
                               uintptr_t id,
                               avs_coap_ctx_t *forced_coap_ctx,
                               avs_net_socket_t *forced_coap_socket) {
    anjay_unlocked_t *anjay = _anjay_downloader_get_anjay(dl);
    assert(!*out_dl_ctx);
    AVS_LIST(anjay_coap_download_ctx_t) ctx =
            AVS_LIST_NEW_ELEMENT(anjay_coap_download_ctx_t);
    if (!ctx) {
        dl_log(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }
    if (forced_coap_ctx) {
        ctx->common.same_socket_download = true;
        ctx->coap = forced_coap_ctx;
        ctx->socket = forced_coap_socket;
    }

    avs_net_ssl_configuration_t ssl_config;
    avs_error_t err = AVS_OK;
    static const anjay_download_ctx_vtable_t VTABLE = {
        .get_socket = get_coap_socket,
        .get_socket_transport = get_coap_socket_transport,
        .handle_packet = handle_coap_message,
        .cleanup = cleanup_coap_transfer,
        .suspend = suspend_coap_transfer,
        .reconnect = reconnect_coap_transfer,
        .set_next_block_offset = set_next_coap_block_offset
    };
    ctx->common.vtable = &VTABLE;

    const anjay_transport_info_t *transport_info =
            _anjay_transport_info_by_uri_scheme(cfg->url);
    if (!transport_info || _anjay_url_parse(cfg->url, &ctx->uri)) {
        dl_log(ERROR, _("invalid URL: ") "%s", cfg->url);
        err = avs_errno(AVS_EINVAL);
        goto error;
    }
    ctx->transport = transport_info->transport;

    if (cfg->etag && cfg->etag->size > sizeof(ctx->etag.bytes)) {
        dl_log(ERROR, _("ETag too long"));
        err = avs_errno(AVS_EPROTO);
        goto error;
    }

    if (!cfg->on_next_block || !cfg->on_download_finished) {
        dl_log(ERROR, _("invalid download config: handlers not set up"));
        err = avs_errno(AVS_EINVAL);
        goto error;
    }

    if (!ctx->common.same_socket_download) {
        ssl_config = (avs_net_ssl_configuration_t) {
            .version = anjay->dtls_version,
            .security = cfg->security_config.security_info,
            .session_resumption_buffer = ctx->dtls_session_buffer,
            .session_resumption_buffer_size = sizeof(ctx->dtls_session_buffer),
            .ciphersuites = cfg->security_config.tls_ciphersuites.num_ids
                                    ? cfg->security_config.tls_ciphersuites
                                    : anjay->default_tls_ciphersuites,
            .backend_configuration = anjay->socket_config,
            .prng_ctx = anjay->prng_ctx.ctx
        };
        ssl_config.backend_configuration.reuse_addr = 1;
        ssl_config.backend_configuration.preferred_endpoint =
                &ctx->preferred_endpoint;

        if (!transport_info->socket_type) {
            dl_log(ERROR,
                   _("URI scheme ") "%s" _(
                           " uses a non-IP transport, which is not "
                           "supported for downloads"),
                   transport_info->uri_scheme);
            err = avs_errno(AVS_EPROTONOSUPPORT);
            goto error;
        }

        assert(transport_info->security != ANJAY_TRANSPORT_SECURITY_UNDEFINED);

        // Downloader sockets MUST NOT reuse the same local port as LwM2M
        // sockets. If they do, and the client attempts to download anything
        // from the same host:port as is used by an LwM2M server, we will get
        // two sockets with identical local/remote host/port tuples. Depending
        // on the socket implementation, we may not be able to create such
        // socket, packets might get duplicated between these "identical"
        // sockets, or we may get some kind of load-balancing behavior. In the
        // last case, the client would randomly handle or ignore LwM2M requests
        // and CoAP download responses.
        switch (*transport_info->socket_type) {
        case AVS_NET_TCP_SOCKET:
            err = avs_net_tcp_socket_create(&ctx->socket,
                                            &ssl_config.backend_configuration);
            break;
        case AVS_NET_UDP_SOCKET:
            err = avs_net_udp_socket_create(&ctx->socket,
                                            &ssl_config.backend_configuration);
            break;
        case AVS_NET_SSL_SOCKET:
            err = avs_net_ssl_socket_create(&ctx->socket, &ssl_config);
            break;
        case AVS_NET_DTLS_SOCKET:
            err = avs_net_dtls_socket_create(&ctx->socket, &ssl_config);
            break;
        default:
            err = avs_errno(AVS_EPROTONOSUPPORT);
            break;
        }
        if (avs_is_err(err)) {
            dl_log(ERROR, _("could not create CoAP socket"));
        } else if (cfg->security_config.dane_tlsa_record
                   && avs_is_err((
                              err = avs_net_socket_set_opt(
                                      ctx->socket,
                                      AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY,
                                      (avs_net_socket_opt_value_t) {
                                          .dane_tlsa_array = {
                                              .array_ptr =
                                                      cfg->security_config
                                                              .dane_tlsa_record,
                                              .array_element_count = 1
                                          }
                                      })))) {
            anjay_log(ERROR, _("could not configure DANE TLSA record"));
            _anjay_socket_cleanup(anjay, &ctx->socket);
        } else if (avs_is_err((err = avs_net_socket_connect(ctx->socket,
                                                            ctx->uri.host,
                                                            ctx->uri.port)))) {
            dl_log(ERROR, _("could not connect CoAP socket"));
            _anjay_socket_cleanup(anjay, &ctx->socket);
        }
        if (!ctx->socket) {
            assert(avs_is_err(err));
            dl_log(ERROR, _("could not create CoAP socket"));
            goto error;
        }
    }

    ctx->common.dl = dl;
    ctx->common.id = id;
    ctx->common.on_next_block = cfg->on_next_block;
    ctx->common.on_download_finished = cfg->on_download_finished;
    ctx->common.user_data = cfg->user_data;
    ctx->bytes_downloaded = cfg->start_offset;

    if (cfg->etag) {
        ctx->etag.size = cfg->etag->size;
        memcpy(ctx->etag.bytes, cfg->etag->value, ctx->etag.size);
    }

#    ifdef WITH_AVS_COAP_UDP
    if (!cfg->coap_tx_params) {
        ctx->tx_params = anjay->udp_tx_params;
    } else {
        const char *error_string = NULL;
        if (avs_coap_udp_tx_params_valid(cfg->coap_tx_params, &error_string)) {
            ctx->tx_params = *cfg->coap_tx_params;
        } else {
            dl_log(ERROR, _("invalid tx_params: ") "%s", error_string);
            goto error;
        }
    }
#    endif // WITH_AVS_COAP_UDP

    if (!ctx->common.same_socket_download
            && avs_is_err((err = reset_coap_ctx(ctx)))) {
        goto error;
    }

    if (AVS_SCHED_NOW(anjay->sched, &ctx->job_start, start_download_job,
                      &ctx->common.id, sizeof(ctx->common.id))) {
        dl_log(ERROR, _("could not schedule download job"));
        err = avs_errno(AVS_ENOMEM);
        goto error;
    }

    *out_dl_ctx = (AVS_LIST(anjay_download_ctx_t)) ctx;
    return AVS_OK;
error:
    cleanup_coap_transfer((AVS_LIST(anjay_download_ctx_t) *) &ctx);
    return err;
}

#    ifdef ANJAY_TEST
#        include "tests/core/downloader/downloader.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_COAP_DOWNLOAD
