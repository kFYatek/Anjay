/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "demo_args.h"
#include "demo.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <string.h>

#ifndef _WIN32
#    include <sys/ioctl.h>
#    include <unistd.h>
#endif // _WIN32

#include <avsystem/commons/avs_memory.h>

#define DEFAULT_PSK_IDENTITY "sesame"
#define DEFAULT_PSK_KEY "password"

#ifdef ANJAY_WITH_LWM2M11
#    define DEFAULT_MAX_LWM2M_VER "1.1"
#endif // ANJAY_WITH_LWM2M11

static const cmdline_args_t DEFAULT_CMDLINE_ARGS = {
    .connection_args = {
        .servers[0] = {
            .security_iid = ANJAY_ID_INVALID,
            .server_iid = ANJAY_ID_INVALID,
            .id = 1,
            .binding_mode = NULL,
#ifdef ANJAY_WITH_LWM2M11
            .retry_count = 1,
            .retry_timer = 0,
            .sequence_retry_count = 1,
            .sequence_delay_timer = 0,
#endif // ANJAY_WITH_LWM2M11
        },
#ifdef ANJAY_WITH_BOOTSTRAP
        .bootstrap_holdoff_s = 0,
        .bootstrap_timeout_s = 0,
#endif // ANJAY_WITH_BOOTSTRAP
        .lifetime = 86400,
        .security_mode = ANJAY_SECURITY_NOSEC
    },
    .location_csv = NULL,
    .location_update_frequency_s = 1,
    .inbuf_size = 4000,
    .outbuf_size = 4000,
    .msg_cache_size = 0,
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
#    if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
            && defined(AVS_COMMONS_STREAM_WITH_FILE)
    .fw_updated_marker_path = "/tmp/anjay-fw-updated",
#    endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
           // defined(AVS_COMMONS_STREAM_WITH_FILE)
    .fw_security_info = {
        .mode = (avs_net_security_mode_t) -1
    },
#endif // ANJAY_WITH_MODULE_FW_UPDATE

#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
    .advanced_fw_security_info = {
        .mode = (avs_net_security_mode_t) -1
    },
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE

#ifdef AVS_COMMONS_STREAM_WITH_FILE
#    ifdef ANJAY_WITH_ATTR_STORAGE
    .attr_storage_file = NULL,
#    endif // ANJAY_WITH_ATTR_STORAGE
#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
    .dm_persistence_file = NULL,
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
#endif     // AVS_COMMONS_STREAM_WITH_FILE
    .disable_legacy_server_initiated_bootstrap = false,
#ifdef AVS_COMMONS_STREAM_WITH_FILE
#endif // AVS_COMMONS_STREAM_WITH_FILE
#ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
    .provisioning_file = NULL,
#endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
    .tx_params = ANJAY_COAP_DEFAULT_UDP_TX_PARAMS,
    .dtls_hs_tx_params = ANJAY_DTLS_DEFAULT_UDP_HS_TX_PARAMS,
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    .fwu_tx_params_modified = false,
    .fwu_tx_params = ANJAY_COAP_DEFAULT_UDP_TX_PARAMS,
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
    .advanced_fwu_tx_params_modified = false,
    .advanced_fwu_tx_params = ANJAY_COAP_DEFAULT_UDP_TX_PARAMS,
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
#ifdef ANJAY_WITH_LWM2M11
    .lwm2m_version_config = {
        .minimum_version = ANJAY_LWM2M_VERSION_1_0,
        .maximum_version = ANJAY_LWM2M_VERSION_1_1
    },
#endif // ANJAY_WITH_LWM2M11
    .prefer_hierarchical_formats = false,
    .update_immediately_on_dm_change = false,
    .enable_self_notify = false,
    .prefer_same_socket_downloads = false,
};

static int parse_security_mode(const char *mode_string,
                               anjay_security_mode_t *out_mode) {
    if (!mode_string) {
        return -1;
    }

    static const struct {
        const char *name;
        anjay_security_mode_t value;
    } MODES[] = {
        // clang-format off
        { "psk",   ANJAY_SECURITY_PSK         },
        { "rpk",   ANJAY_SECURITY_RPK         },
        { "cert",  ANJAY_SECURITY_CERTIFICATE },
        { "nosec", ANJAY_SECURITY_NOSEC       },
        { "est",   ANJAY_SECURITY_EST         },
        // clang-format on
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(MODES); ++i) {
        if (!strcmp(mode_string, MODES[i].name)) {
            *out_mode = MODES[i].value;
            return 0;
        }
    }

    char allowed_modes[64];
    size_t offset = 0;
    for (size_t i = 0; i < AVS_ARRAY_SIZE(MODES); ++i) {
        int written =
                snprintf(allowed_modes + offset, sizeof(allowed_modes) - offset,
                         " %s", MODES[i].name);
        if (written < 0 || (size_t) written >= sizeof(allowed_modes) - offset) {
            demo_log(ERROR, "could not enumerate available security modes");
            allowed_modes[0] = '\0';
            break;
        }

        offset += (size_t) written;
    }

    demo_log(ERROR, "unrecognized security mode %s (expected one of:%s)",
             mode_string, allowed_modes);
    return -1;
}

static int parse_tls_version(const char *str,
                             avs_net_ssl_version_t *out_version) {
    assert(str);
    if (strcmp(str, "default") == 0) {
        *out_version = AVS_NET_SSL_VERSION_DEFAULT;
        return 0;
    } else if (strcmp(str, "SSLv23") == 0) {
        *out_version = AVS_NET_SSL_VERSION_SSLv2_OR_3;
        return 0;
    } else if (strcmp(str, "SSLv2") == 0 || strcmp(str, "SSLv2.0") == 0
               || strcmp(str, "2.0") == 0) {
        *out_version = AVS_NET_SSL_VERSION_SSLv2;
        return 0;
    } else if (strcmp(str, "SSLv3") == 0 || strcmp(str, "SSLv3.0") == 0
               || strcmp(str, "3.0") == 0) {
        *out_version = AVS_NET_SSL_VERSION_SSLv3;
        return 0;
    } else if (strcmp(str, "TLSv1") == 0 || strcmp(str, "TLSv1.0") == 0
               || strcmp(str, "1.0") == 0) {
        *out_version = AVS_NET_SSL_VERSION_TLSv1;
        return 0;
    } else if (strcmp(str, "TLSv1.1") == 0 || strcmp(str, "1.1") == 0) {
        *out_version = AVS_NET_SSL_VERSION_TLSv1_1;
        return 0;
    } else if (strcmp(str, "TLSv1.2") == 0 || strcmp(str, "1.2") == 0) {
        *out_version = AVS_NET_SSL_VERSION_TLSv1_2;
        return 0;
    } else if (strcmp(str, "TLSv1.3") == 0 || strcmp(str, "1.3") == 0) {
        *out_version = AVS_NET_SSL_VERSION_TLSv1_3;
        return 0;
    } else {
        demo_log(ERROR, "Invalid TLS version: %s", str);
        return -1;
    }
}

static size_t get_screen_width(void) {
#ifndef _WIN32
    struct winsize ws;
    if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) {
        return ws.ws_col;
    }
#endif // _WIN32
    // fallback to 80 columns as default
    return 80;
}

static void
print_wrapped(const char *str, size_t padding, size_t screen_width) {
    const char *str_end = str + strlen(str);
    do {
        for (size_t i = 0; i < padding; ++i) {
            putchar(' ');
        }

        const char *line_start = str;
        const char *line_end = NULL;
        bool first_word = true;
        while (str < str_end) {
            str += strspn(str, AVS_SPACES);
            str += strcspn(str, AVS_SPACES);
            if (first_word
                    || (size_t) (str - line_start) + padding < screen_width) {
                line_end = str;
                first_word = false;
            } else {
                break;
            }
        }
        str = line_end + strspn(line_end, AVS_SPACES);
        fwrite(line_start, 1, (size_t) (line_end - line_start), stdout);
        putchar('\n');
    } while (str < str_end);
}

static void print_help(const struct option *options) {
    const struct {
        int opt_val;
        const char *args;
        const char *default_value;
        const char *help;
    } HELP_INFO[] = {
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
        { 'a', "/OID/IID,SSID,ACCESS_MASK", NULL,
          "create ACL entry for specified /OID/IID and SSID" },
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
#ifdef ANJAY_WITH_BOOTSTRAP
        { 'b', "client-initiated-only", NULL,
          "treat first URI as Bootstrap Server. If the optional "
          "\"client-initiated-only\" option is specified, the legacy LwM2M "
          "1.0-style Server-Initiated bootstrap mode is not available." },
        { 'H', "SECONDS", "0",
          "number of seconds to wait before attempting Client Initiated "
          "Bootstrap." },
        { 'T', "SECONDS", "0",
          "number of seconds to keep the Bootstrap Server Account for after "
          "successful bootstrapping, or 0 for infinity." },
#endif // ANJAY_WITH_BOOTSTRAP
        { 'e', "URN", DEFAULT_CMDLINE_ARGS.endpoint_name,
          "endpoint name to use." },
        { 'h', NULL, NULL, "show this message and exit." },
#ifndef _WIN32
        { 't', NULL, NULL,
          "disables standard input. Useful for running the client as a "
          "daemon." },
#endif // _WIN32
        { 'l', "SECONDS", "86400",
          "set registration lifetime. If SECONDS <= 0, use default value and "
          "don't send lifetime in Register/Update messages." },
        { 'L', "MAX_NOTIFICATIONS", "0",
          "set limit of queued notifications in queue/offline mode. 0: "
          "unlimited; >0: keep that much newest ones" },
        { 'c', "CSV_FILE", NULL, "file to load location CSV from" },
        { 'f', "SECONDS", "1", "location update frequency in seconds" },
        { 'p', "PORT", NULL, "bind all sockets to the specified UDP port." },
        { 'i', "PSK identity (psk mode) or Public Certificate (cert mode)",
          NULL, "Both are specified as hexlified strings" },
        { 'C', "CLIENT_CERT_FILE", "$(dirname $0)/../certs/client.crt.der",
          "DER-formatted client certificate file to load. Mutually exclusive "
          "with -i" },
        { 'k', "PSK key (psk mode) or Private Certificate (cert mode)", NULL,
          "Both are specified as hexlified strings" },
        { 'K', "PRIVATE_KEY_FILE", "$(dirname $0)/../certs/client.key.der",
          "DER-formatted PKCS#8 private key complementary to the certificate "
          "specified with -C. Mutually exclusive with -k" },
        { 'P', "SERVER_PUBLIC_KEY_FILE",
          "$(dirname $0)/../certs/server.crt.der",
          "DER-formatted server public key file to load." },
        { 'q', "BINDING_MODE=UQ", NULL,
          "set the Binding Mode to use for the currently configured server. "
          "If Binding Mode is not set by this flag, client tries to derive it "
          "from URI and if it cannot, it uses the default value 'U'" },
        { 's', "MODE", NULL,
          "set security mode, one of: psk rpk cert nosec. "
          "Note: only affects coaps:// and coaps+*:// URLs" },
        { 'u', "URI", NULL,
          "server URI to use. N consecutive URIs will create N servers "
          "enumerated from 1 to N." },
        { 'D', "IID", NULL,
          "enforce particular Security Instance IID for last configured "
          "server." },
        { 'd', "IID", NULL,
          "enforce particular Server Instance IID for last configured server. "
          "Ignored if last configured server is an LwM2M Bootstrap Server." },
        { 'I', "SIZE", "4000",
          "Nonnegative integer representing maximum size of an incoming CoAP "
          "packet the client should be able to handle." },
        { 'O', "SIZE", "4000",
          "Nonnegative integer representing maximum size of a non-BLOCK CoAP "
          "packet the client should be able to send." },
        { '$', "SIZE", "0",
          "Size, in bytes, of a buffer reserved for caching sent responses to "
          "detect retransmissions. Setting it to 0 disables caching "
          "mechanism." },
        { 'N', NULL, NULL,
          "Send notifications as Confirmable messages by default" },
#ifdef ANJAY_WITH_LWM2M11
        { 'v', "VERSION", "1.0", "Lowest version of LwM2M Enabler to allow" },
        { 'V', "VERSION", DEFAULT_MAX_LWM2M_VER,
          "Highest version of LwM2M Enabler to allow" },
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
        { 'r', "RESULT", NULL,
          "If specified and nonzero, initializes the Firmware Update object in "
          "UPDATING state, and sets the result to given value after a short "
          "while" },
#    if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
            && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { 256, "PATH", DEFAULT_CMDLINE_ARGS.fw_updated_marker_path,
          "File path to use as a marker for persisting firmware update state" },
#    endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
           // defined(AVS_COMMONS_STREAM_WITH_FILE)
        { 257, "CERT_FILE", NULL,
          "Require certificate validation against specified file when "
          "downloading firmware over encrypted channels" },
        { 258, "CERT_DIR", NULL,
          "Require certificate validation against files in specified path when "
          "downloading firmware over encrypted channels; note that the TLS "
          "backend may impose specific requirements for file names and "
          "formats" },
        { 259, "PSK identity", NULL,
          "Download firmware over encrypted channels using PSK-mode encryption "
          "with the specified identity (provided as hexlified string); must be "
          "used together with --fw-psk-key" },
        { 260, "PSK key", NULL,
          "Download firmware over encrypted channels using PSK-mode encryption "
          "with the specified key (provided as hexlified string); must be used "
          "together with --fw-psk-identity" },
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#if defined(ANJAY_WITH_ATTR_STORAGE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { 261, "PERSISTENCE_FILE", NULL,
          "File to load attribute storage data from at startup, and "
          "store it at shutdown" },
#endif // defined(ANJAY_WITH_ATTR_STORAGE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
#ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
        { 'F', "PROVISIONING_FILE", NULL,
          "File where factory provisioning data is contained." },
#endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
        { 267, "ACK_RANDOM_FACTOR", "1.5",
          "Configures ACK_RANDOM_FACTOR (defined in RFC7252)" },
        { 268, "ACK_TIMEOUT", "2.0",
          "Configures ACK_TIMEOUT (defined in RFC7252) in seconds" },
        { 269, "MAX_RETRANSMIT", "4",
          "Configures MAX_RETRANSMIT (defined in RFC7252)" },
        { 270, "DTLS_HS_RETRY_WAIT_MIN", "1",
          "Configures minimum period of time to wait before sending first "
          "DTLS HS retransmission" },
        { 271, "DTLS_HS_RETRY_WAIT_MAX", "60",
          "Configures maximum period of time to wait (after last "
          "retransmission) before giving up on handshake completely" },
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
        { 272, "ACK_RANDOM_FACTOR", "1.5",
          "Configures ACK_RANDOM_FACTOR (defined in RFC7252) for firmware "
          "update" },
        { 273, "ACK_TIMEOUT", "2.0",
          "Configures ACK_TIMEOUT (defined in RFC7252) in seconds for firmware "
          "update" },
        { 274, "MAX_RETRANSMIT", "4",
          "Configures MAX_RETRANSMIT (defined in RFC7252) for firmware "
          "update" },
#endif // ANJAY_WITH_MODULE_FW_UPDATE
        { 275, NULL, NULL,
          "Sets the library to use hierarchical content formats by default for "
          "all responses." },
#ifdef ANJAY_WITH_LWM2M11
        { 276, "SNI", "server hostname",
          "Sets the Server Name Indication value for currently configured "
          "server." },
#endif // ANJAY_WITH_LWM2M11
        { 277, NULL, NULL, "Enables DTLS connection_id extension." },
        { 278, "CIPHERSUITE[,CIPHERSUITE...]", "TLS library defaults",
          "Sets the ciphersuites to be used by default for (D)TLS "
          "connections." },
#ifdef ANJAY_WITH_LWM2M11
        { 279, "RETRY_COUNT", "1",
          "Configures the number of registration retry sequences for a last "
          "server" },
        { 280, "RETRY_TIMER", "0",
          "Configures the exponential delay between registration retries" },
        { 281, "SEQUENCE_RETRY_COUNT", "1",
          "Configures the number of registration sequences" },
        { 282, "SEQUENCE_DELAY_TIMER", "86400",
          "Configures the delay between consecutive communication sequences" },
#endif // ANJAY_WITH_LWM2M11
        { 283, NULL, NULL,
          "Configures preference of re-using existing LwM2M CoAP contexts for "
          "firmware download" },
        { 284, "NSTART", "1", "Configures NSTART (defined in RFC7252)" },
#ifdef ANJAY_WITH_SEND
        { 287, NULL, NULL,
          "Enables using LwM2M Send to report state and result of firmware "
          "update" },
#endif // ANJAY_WITH_SEND
#ifdef ANJAY_WITH_LWM2M11
        { 288, "TRUST_STORE_PATH", NULL,
          "Path (file or directory) to use as the trust store for "
          "PKIX verification" },
#endif // ANJAY_WITH_LWM2M11
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { 289, "PERSISTENCE_FILE", NULL,
          "File to load Server, Security and Access Control object contents at "
          "startup, and store it at shutdown" },
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
        { 298, NULL, NULL,
          "Causes security credentials to be loaded as external security info "
          "objects instead of loading them into internal buffers" },
#endif // ANJAY_WITH_SECURITY_STRUCTURED
#ifdef ANJAY_WITH_LWM2M11
        { 299, NULL, NULL,
          "Enables rebuilding of client certificate chain based on the trust "
          "store" },
#endif // ANJAY_WITH_LWM2M11
        { 306, NULL, NULL,
          "Enable alternative logger as a showcase of extended logger "
          "feature." },
        { 307, NULL, NULL,
          "Provide identity from ASCII string (see -i parameter for more "
          "details)" },
        { 308, NULL, NULL,
          "Provide key from ASCII string (see -k parameter for more details)" },
        { 317, "VERSION", "TLS library default",
          "Minimum (D)TLS version to use." },
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
        { 319, "TIMEOUT", "30.0",
          "Time in seconds to wait for incoming response after sending a TCP "
          "request" },
        { 320, NULL, NULL,
          "Send the Update message immediately when Object Instances are "
          "created or deleted." },
        { 321, NULL, NULL,
          "Send the Notify messages as a result of a server action (e.g. "
          "Write) even to the initiating server." },
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
        { 322, "ADDITIONAL_IMG_FILE_PATH", NULL,
          "Path to additional img binary file. Used to compare with obtained "
          "through advanced firmware update procedure" },
#    if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
            && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { 323, "AFU_PERSISTENCE_FILE", NULL,
          "Path to file used to persist advanced firmware update data, "
          "if file not exists, it will be created" },
#    endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
           // defined(AVS_COMMONS_STREAM_WITH_FILE)
        { 324, "CERT_FILE", NULL,
          "Require certificate validation against specified file when "
          "downloading firmware over encrypted channels. This argument is "
          "used by Advanced Firmware Update." },
        { 325, "RESULT", NULL,
          "If specified and nonzero, initializes the Advanced Firmware Update "
          "object in "
          "UPDATING state, and sets the result to given value after a short "
          "while" },
#    ifdef ANJAY_WITH_SEND
        { 326, NULL, NULL,
          "Enables using LwM2M Send to report state and result of advanced "
          "firmware update" },
#    endif // ANJAY_WITH_SEND
        { 327, "ACK_TIMEOUT", "2.0",
          "Configures ACK_TIMEOUT (defined in RFC7252) in seconds for advanced "
          "firmware update" },
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
        { 328, NULL, NULL,
          "Enter offline mode before starting the event loop." },
    };

    const size_t screen_width = get_screen_width();

    puts("Available options:\n");
    for (size_t i = 0; options[i].name || options[i].val; ++i) {
        assert(i < AVS_ARRAY_SIZE(HELP_INFO));
        assert(HELP_INFO[i].opt_val == options[i].val);

        printf("  ");
        if (isprint(options[i].val)) {
            printf("-%c, ", options[i].val);
        }

        int chars_written = 0;
        printf("--%s%n", options[i].name, &chars_written);

        const char *args = HELP_INFO[i].args ? HELP_INFO[i].args : "";
        const char *arg_prefix = "";
        const char *arg_suffix = "";
        if (options[i].has_arg == required_argument) {
            arg_prefix = " ";
        } else if (options[i].has_arg == optional_argument) {
            arg_prefix = "[=";
            arg_suffix = "]";
        }
        printf("%s%s%s\n", arg_prefix, args, arg_suffix);

        print_wrapped(HELP_INFO[i].help, 6, screen_width);
        if (HELP_INFO[i].default_value) {
            printf("      (default: %s)\n", HELP_INFO[i].default_value);
        }
        printf("\n");
    }
}

static int parse_i32(const char *str, int32_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < INT32_MIN
            || long_value > INT32_MAX) {
        demo_log(ERROR,
                 "value out of range: expected 32-bit signed value, got %s",
                 str);
        return -1;
    }

    *out_value = (int32_t) long_value;
    return 0;
}

static int parse_u32(const char *str, uint32_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < 0
            || long_value > UINT32_MAX) {
        demo_log(ERROR,
                 "value out of range: expected 32-bit unsigned value, got %s",
                 str);
        return -1;
    }

    *out_value = (uint32_t) long_value;
    return 0;
}

static int parse_u16(const char *str, uint16_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < 0
            || long_value > UINT16_MAX) {
        demo_log(ERROR,
                 "value out of range: expected 16-bit unsigned value, got %s",
                 str);
        return -1;
    }

    *out_value = (uint16_t) long_value;
    return 0;
}

static int parse_size(const char *str, size_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < 0
#if SIZE_MAX < LONG_MAX
            || long_value > SIZE_MAX
#endif
    ) {
        demo_log(ERROR,
                 "value out of range: expected %d-bit unsigned value, got %s",
                 (int) (CHAR_BIT * sizeof(size_t)), str);
        return -1;
    }

    *out_value = (size_t) long_value;
    return 0;
}

static int parse_double(const char *str, double *out_value) {
    assert(str);
    errno = 0;
    char *endptr = NULL;
    *out_value = strtod(str, &endptr);
    if (!*str || isspace((unsigned char) *str) || errno || !endptr || *endptr) {
        return -1;
    }
    return 0;
}

static int parse_hexstring(const char *str, uint8_t **out, size_t *out_size) {
    if (!str) {
        return -1;
    }

    size_t length = strlen(str);
    if (length % 2 || !length) {
        return -1;
    }
    if (*out) {
        return -1;
    }
    *out = (uint8_t *) avs_malloc(length / 2);
    *out_size = 0;
    if (!*out) {
        return -1;
    }
    const char *curr = str;
    uint8_t *data = *out;
    while (*curr) {
        unsigned value;
        if (sscanf(curr, "%2x", &value) != 1 || (uint8_t) value != value) {
            avs_free(*out);
            return -1;
        }
        *data++ = (uint8_t) value;
        curr += 2;
    }
    *out_size = length / 2;
    return 0;
}

static void build_getopt_string(const struct option *options,
                                char *buffer,
                                size_t buffer_size) {
    const struct option *curr_opt = options;
    char *getopt_string_ptr = buffer;

    memset(buffer, 0, buffer_size);

    while (curr_opt->val != 0) {
        if (curr_opt->val > UCHAR_MAX) {
            curr_opt++;
            continue;
        }

        assert(getopt_string_ptr - buffer < (ptrdiff_t) buffer_size - 1);
        *getopt_string_ptr++ = (char) curr_opt->val;

        int colons = curr_opt->has_arg;
        assert(colons >= 0 && colons <= 2); // 2 colons signify optional arg
        while (colons-- > 0) {
            assert(getopt_string_ptr - buffer < (ptrdiff_t) buffer_size - 1);
            *getopt_string_ptr++ = ':';
        }

        ++curr_opt;
    }
}

static int clone_buffer(uint8_t **out,
                        size_t *out_size,
                        const void *src,
                        size_t src_size) {
    *out = (uint8_t *) avs_malloc(src_size);
    if (!*out) {
        return -1;
    }
    *out_size = src_size;
    memcpy(*out, src, src_size);
    return 0;
}

static int
load_buffer_from_file(uint8_t **out, size_t *out_size, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return -1;
    }
    int result = -1;
    long size;
    if (fseek(f, 0, SEEK_END)) {
        goto finish;
    }
    size = ftell(f);
    if (size < 0 || (unsigned long) size > SIZE_MAX || fseek(f, 0, SEEK_SET)) {
        goto finish;
    }
    if (!(*out_size = (size_t) size)) {
        *out = NULL;
    } else {
        if (!(*out = (uint8_t *) avs_malloc(*out_size))) {
            goto finish;
        }
        if (fread(*out, *out_size, 1, f) != 1) {
            avs_free(*out);
            *out = NULL;
            goto finish;
        }
    }
    result = 0;
finish:
    fclose(f);
    return result;
}

#ifdef ANJAY_WITH_LWM2M11
static int parse_lwm2m_version(const char *str,
                               anjay_lwm2m_version_t *out_version) {
    assert(str);
    if (strcmp(str, "1.0") == 0) {
        *out_version = ANJAY_LWM2M_VERSION_1_0;
        return 0;
    } else if (strcmp(str, "1.1") == 0) {
        *out_version = ANJAY_LWM2M_VERSION_1_1;
        return 0;
    } else {
        demo_log(ERROR, "Invalid LwM2M version: %s", str);
        return -1;
    }
}
#endif // ANJAY_WITH_LWM2M11

int demo_parse_argv(cmdline_args_t *parsed_args, int argc, char *argv[]) {
    static const char DEFAULT_CERT_FILE[] = "../certs/client.crt.der";
    static const char DEFAULT_KEY_FILE[] = "../certs/client.key.der";
    const char *last_arg0_slash = strrchr(argv[0], '/');
    size_t arg0_prefix_length =
            (size_t) (last_arg0_slash ? (last_arg0_slash - argv[0] + 1) : 0);
    bool identity_set, key_set;
    int num_servers = 0;

    const struct option options[] = {
    // clang-format off
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
        { "access-entry",                  required_argument, 0, 'a' },
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
#ifdef ANJAY_WITH_BOOTSTRAP
        { "bootstrap",                     optional_argument, 0, 'b' },
        { "bootstrap-holdoff",             required_argument, 0, 'H' },
        { "bootstrap-timeout",             required_argument, 0, 'T' },
#endif // ANJAY_WITH_BOOTSTRAP
        { "endpoint-name",                 required_argument, 0, 'e' },
        { "help",                          no_argument,       0, 'h' },
#ifndef _WIN32
        { "disable-stdin",                 no_argument,       0, 't' },
#endif // _WIN32
        { "lifetime",                      required_argument, 0, 'l' },
        { "stored-notification-limit",     required_argument, 0, 'L' },
        { "location-csv",                  required_argument, 0, 'c' },
        { "location-update-freq-s",        required_argument, 0, 'f' },
        { "port",                          required_argument, 0, 'p' },
        { "identity",                      required_argument, 0, 'i' },
        { "client-cert-file",              required_argument, 0, 'C' },
        { "key",                           required_argument, 0, 'k' },
        { "key-file",                      required_argument, 0, 'K' },
        { "server-public-key-file",        required_argument, 0, 'P' },
        { "binding",                       required_argument, 0, 'q' },
        { "security-mode",                 required_argument, 0, 's' },
        { "server-uri",                    required_argument, 0, 'u' },
        { "security-iid",                  required_argument, 0, 'D' },
        { "server-iid",                    required_argument, 0, 'd' },
        { "inbuf-size",                    required_argument, 0, 'I' },
        { "outbuf-size",                   required_argument, 0, 'O' },
        { "cache-size",                    required_argument, 0, '$' },
        { "confirmable-notifications",     no_argument,       0, 'N' },
#ifdef ANJAY_WITH_LWM2M11
        { "minimum-version",               required_argument, 0, 'v' },
        { "maximum-version",               required_argument, 0, 'V' },
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
        { "delayed-upgrade-result",        required_argument, 0, 'r' },
#   if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { "fw-updated-marker-path",        required_argument, 0, 256 },
#   endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { "fw-cert-file",                  required_argument, 0, 257 },
        { "fw-cert-path",                  required_argument, 0, 258 },
        { "fw-psk-identity",               required_argument, 0, 259 },
        { "fw-psk-key",                    required_argument, 0, 260 },
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#if defined(ANJAY_WITH_ATTR_STORAGE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { "attribute-storage-persistence-file", required_argument, 0, 261 },
#endif // defined(ANJAY_WITH_ATTR_STORAGE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
#ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
        { "factory-provisioning-file",     required_argument, 0, 'F' },
#endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
        { "ack-random-factor",             required_argument, 0, 267 },
        { "ack-timeout",                   required_argument, 0, 268 },
        { "max-retransmit",                required_argument, 0, 269 },
        { "dtls-hs-retry-wait-min",        required_argument, 0, 270 },
        { "dtls-hs-retry-wait-max",        required_argument, 0, 271 },
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
        { "fwu-ack-random-factor",         required_argument, 0, 272 },
        { "fwu-ack-timeout",               required_argument, 0, 273 },
        { "fwu-max-retransmit",            required_argument, 0, 274 },
#endif // ANJAY_WITH_MODULE_FW_UPDATE
        { "prefer-hierarchical-formats",   no_argument,       0, 275 },
#ifdef ANJAY_WITH_LWM2M11
        { "sni",                           required_argument, 0, 276 },
#endif // ANJAY_WITH_LWM2M11
        { "use-connection-id",             no_argument,       0, 277 },
        { "ciphersuites",                  required_argument, 0, 278 },
#ifdef ANJAY_WITH_LWM2M11
        { "retry-count",                   required_argument, 0, 279 },
        { "retry-timer",                   required_argument, 0, 280 },
        { "sequence-retry-count",          required_argument, 0, 281 },
        { "sequence-delay-timer",          required_argument, 0, 282 },
#endif // ANJAY_WITH_LWM2M11
        { "prefer-same-socket-downloads",  no_argument,       0, 283 },
        { "nstart",                        required_argument, 0, 284 },
#if defined(ANJAY_WITH_SEND) && defined(ANJAY_WITH_MODULE_FW_UPDATE)
        { "fw-update-use-send",            no_argument,       0, 287 },
#endif // defined(ANJAY_WITH_SEND) && defined(ANJAY_WITH_MODULE_FW_UPDATE)
#ifdef ANJAY_WITH_LWM2M11
        { "pkix-trust-store",              required_argument, 0, 288 },
#endif // ANJAY_WITH_LWM2M11
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { "dm-persistence-file",           required_argument, 0, 289 },
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
        { "use-external-security-info",    no_argument,       0, 298 },
#endif // ANJAY_WITH_SECURITY_STRUCTURED
#ifdef ANJAY_WITH_LWM2M11
        { "rebuild-client-cert-chain",     no_argument,       0, 299 },
#endif // ANJAY_WITH_LWM2M11
        { "alternative-logger",            no_argument,       0, 306 },
        { "identity-as-string",            required_argument, 0, 307 },
        { "key-as-string",                 required_argument, 0, 308 },
        { "tls-version",                   required_argument, 0, 317 },
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
        { "tcp-request-timeout",           required_argument, 0, 319 },
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
        { "update-immediately-on-dm-change", no_argument,     0, 320 },
        { "enable-self-notify",              no_argument,     0, 321 },
#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
        { "afu-original-img-file-path",    required_argument, 0, 322 },
#   if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { "afu-marker-path",               required_argument, 0, 323 },
#   endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        { "afu-cert-file",                 required_argument, 0, 324 },
       { "delayed-afu-result",            required_argument, 0, 325 },
#   if defined(ANJAY_WITH_SEND)
       { "afu-use-send",                  no_argument,       0, 326 },
#   endif // defined(ANJAY_WITH_SEND)
       { "afu-ack-timeout",               required_argument, 0, 327 },
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
        { "start-offline",                   no_argument,     0, 328 },
        { 0, 0, 0, 0 }
        // clang-format on
    };

    int retval = -1;

    *parsed_args = DEFAULT_CMDLINE_ARGS;

    char *default_cert_path =
            (char *) avs_malloc(arg0_prefix_length + sizeof(DEFAULT_CERT_FILE));
    char *default_key_path =
            (char *) avs_malloc(arg0_prefix_length + sizeof(DEFAULT_KEY_FILE));
    const char *cert_path = default_cert_path;
    const char *key_path = default_key_path;
    const char *server_public_key_path = NULL;
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
    bool use_external_security_info = false;
#endif // ANJAY_WITH_SECURITY_STRUCTURED

    if (!default_cert_path || !default_key_path) {
        demo_log(ERROR, "Out of memory");
        goto finish;
    }

    memcpy(default_cert_path, argv[0], arg0_prefix_length);
    strcpy(default_cert_path + arg0_prefix_length, DEFAULT_CERT_FILE);

    memcpy(default_key_path, argv[0], arg0_prefix_length);
    strcpy(default_key_path + arg0_prefix_length, DEFAULT_KEY_FILE);

    char getopt_str[3 * AVS_ARRAY_SIZE(options)];
    build_getopt_string(options, getopt_str, sizeof(getopt_str));

    while (true) {
        int option_index = 0;

        switch (getopt_long(argc, argv, getopt_str, options, &option_index)) {
        case '?':
            demo_log(ERROR, "unrecognized cmdline argument: %s",
                     argv[option_index]);
            goto finish;
        case -1:
            if (optind >= argc) {
                goto process;
            }
            demo_log(ERROR, "unrecognized free argument: %s", argv[optind]);
            goto finish;
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
        case 'a': {
            uint16_t oid;
            uint16_t iid;
            uint16_t ssid;
            uint16_t mask;
            if (sscanf(optarg, "/%" SCNu16 "/%" SCNu16 ",%" SCNu16 ",%" SCNu16,
                       &oid, &iid, &ssid, &mask)
                    != 4) {
                demo_log(ERROR, "insufficient arguments");
                goto finish;
            }
            AVS_LIST(access_entry_t) entry =
                    AVS_LIST_NEW_ELEMENT(access_entry_t);
            if (!entry) {
                goto finish;
            }
            entry->oid = (anjay_oid_t) oid;
            entry->iid = (anjay_iid_t) iid;
            entry->ssid = (anjay_ssid_t) ssid;
            entry->mask = (anjay_access_mask_t) mask;
            AVS_LIST_INSERT(&parsed_args->access_entries, entry);
            break;
        }
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
#ifdef ANJAY_WITH_BOOTSTRAP
        case 'b': {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            parsed_args->connection_args.servers[idx].is_bootstrap = true;
            if (optarg && *optarg) {
                if (strcmp(optarg, "client-initiated-only") == 0) {
                    parsed_args->disable_legacy_server_initiated_bootstrap =
                            true;
                } else {
                    demo_log(ERROR,
                             "Invalid bootstrap optional argument: \"%s\"; "
                             "available options: client-initiated-only",
                             optarg);
                    goto finish;
                }
            }
            break;
        }
        case 'H':
            if (parse_i32(optarg,
                          &parsed_args->connection_args.bootstrap_holdoff_s)) {
                goto finish;
            }
            break;
        case 'T':
            if (parse_i32(optarg,
                          &parsed_args->connection_args.bootstrap_timeout_s)) {
                goto finish;
            }
            break;
#endif // ANJAY_WITH_BOOTSTRAP
        case 'e':
            parsed_args->endpoint_name = optarg;
            break;
        case 'h':
            print_help(options);
            goto finish;
#ifndef _WIN32
        case 't':
            parsed_args->disable_stdin = true;
            break;
#endif // _WIN32
        case 'l':
            if (parse_i32(optarg, &parsed_args->connection_args.lifetime)) {
                goto finish;
            }
            break;
        case 'L':
            if (parse_size(optarg, &parsed_args->stored_notification_limit)) {
                goto finish;
            }
            break;
        case 'c':
            parsed_args->location_csv = optarg;
            break;
        case 'f': {
            long freq;
            if (demo_parse_long(optarg, &freq) || freq <= 0
                    || freq > INT32_MAX) {
                demo_log(ERROR, "invalid location update frequency: %s",
                         optarg);
                goto finish;
            }

            parsed_args->location_update_frequency_s = (time_t) freq;
            break;
        }
        case 'p': {
            long port;
            if (demo_parse_long(optarg, &port) || port <= 0
                    || port > UINT16_MAX) {
                demo_log(ERROR, "invalid UDP port number: %s", optarg);
                goto finish;
            }

            parsed_args->udp_listen_port = (uint16_t) port;
            break;
        }
        case 'i':
            if (parse_hexstring(optarg,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity_size)) {
                demo_log(ERROR, "Invalid identity");
                goto finish;
            }
            break;
        case 'C':
            cert_path = optarg;
            break;
        case 'k':
            if (parse_hexstring(
                        optarg,
                        &parsed_args->connection_args.private_cert_or_psk_key,
                        &parsed_args->connection_args
                                 .private_cert_or_psk_key_size)) {
                demo_log(ERROR, "Invalid key");
                goto finish;
            }
            break;
        case 'K':
            key_path = optarg;
            break;
        case 'P':
            server_public_key_path = optarg;
            break;
        case 'q': {
            if (num_servers == 0) {
                demo_log(ERROR, "Undefined server. Use --server-uri/-u first");
                goto finish;
            }
            int idx = num_servers - 1;
            if (parsed_args->connection_args.servers[idx].binding_mode
                    != NULL) {
                demo_log(ERROR,
                         "Binding mode already defined for the current server");
                goto finish;
            }
            parsed_args->connection_args.servers[idx].binding_mode = optarg;
            break;
        }
        case 'D': {
            if (num_servers == 0) {
                demo_log(ERROR, "Undefined server. Use --server-uri/-u first");
                goto finish;
            }
            int idx = num_servers - 1;
            if (parsed_args->connection_args.servers[idx].security_iid
                    != ANJAY_ID_INVALID) {
                demo_log(ERROR, "Security IID already defined");
                goto finish;
            }
            if (parse_u16(optarg,
                          &parsed_args->connection_args.servers[idx]
                                   .security_iid)) {
                goto finish;
            }
            break;
        }
        case 's':
            if (parse_security_mode(
                        optarg, &parsed_args->connection_args.security_mode)) {
                goto finish;
            }
            break;
        case 'd': {
            if (num_servers == 0) {
                demo_log(ERROR, "Undefined server. Use --server-uri/-u first");
                goto finish;
            }
            int idx = num_servers - 1;
            if (parsed_args->connection_args.servers[idx].server_iid
                    != ANJAY_ID_INVALID) {
                demo_log(ERROR, "Server IID already defined");
                goto finish;
            }
            if (parse_u16(optarg,
                          &parsed_args->connection_args.servers[idx]
                                   .server_iid)) {
                goto finish;
            }
            break;
        }
        case 'u': {
            AVS_ASSERT(num_servers < MAX_SERVERS, "Too many servers");
            const server_entry_t *prev_entry = NULL;
            if (num_servers > 0) {
                prev_entry =
                        &parsed_args->connection_args.servers[num_servers - 1];
            }
            server_entry_t *entry =
                    &parsed_args->connection_args.servers[num_servers++];
            if (prev_entry) {
                memcpy(entry, prev_entry, sizeof(*prev_entry));
                entry->security_iid = ANJAY_ID_INVALID;
                entry->server_iid = ANJAY_ID_INVALID;
                entry->binding_mode = NULL;
                entry->is_bootstrap = false;
            }

            entry->uri = optarg;

            break;
        }
        case 'I':
            if (parse_i32(optarg, &parsed_args->inbuf_size)
                    || parsed_args->inbuf_size <= 0) {
                goto finish;
            }
            break;
        case 'O':
            if (parse_i32(optarg, &parsed_args->outbuf_size)
                    || parsed_args->outbuf_size <= 0) {
                goto finish;
            }
            break;
        case '$':
            if (parse_i32(optarg, &parsed_args->msg_cache_size)
                    || parsed_args->msg_cache_size < 0) {
                goto finish;
            }
            break;
        case 'N':
            parsed_args->confirmable_notifications = true;
            break;
#ifdef ANJAY_WITH_LWM2M11
        case 'v':
            if (parse_lwm2m_version(
                        optarg,
                        &parsed_args->lwm2m_version_config.minimum_version)) {
                goto finish;
            }
            break;
        case 'V':
            if (parse_lwm2m_version(
                        optarg,
                        &parsed_args->lwm2m_version_config.maximum_version)) {
                goto finish;
            }
            break;
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
        case 'r': {
            int result;
            if (parse_i32(optarg, &result)
                    || result < (int) ANJAY_FW_UPDATE_RESULT_INITIAL
                    || result > (int) ANJAY_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL) {
                demo_log(ERROR, "invalid update result value: %s", optarg);
                goto finish;
            }
            parsed_args->fw_update_delayed_result =
                    (anjay_fw_update_result_t) result;
            break;
        }
        case 256:
            parsed_args->fw_updated_marker_path = optarg;
            break;
        case 257: {
            if (parsed_args->fw_security_info.mode
                    != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }

            const avs_net_certificate_info_t cert_info = {
                .server_cert_validation = true,
                .trusted_certs =
                        avs_crypto_certificate_chain_info_from_file(optarg)
            };
            parsed_args->fw_security_info =
                    avs_net_security_info_from_certificates(cert_info);
            break;
        }
        case 258: {
            if (parsed_args->fw_security_info.mode
                    != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }
            const avs_net_certificate_info_t cert_info = {
                .server_cert_validation = true,
                .trusted_certs =
                        avs_crypto_certificate_chain_info_from_path(optarg)
            };
            parsed_args->fw_security_info =
                    avs_net_security_info_from_certificates(cert_info);
            break;
        }
        case 259: {
            if (parsed_args->fw_security_info.mode != AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.mode
                                   != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }
            if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.data.psk.identity.desc
                                       .source
                                   != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
                demo_log(ERROR, "--fw-psk-identity specified more than once");
                goto finish;
            }
            uint8_t *identity_buf = NULL;
            size_t identity_size = 0;
            if (parse_hexstring(optarg, &identity_buf, &identity_size)) {
                demo_log(ERROR, "Invalid PSK identity for firmware upgrade");
                goto finish;
            }
            parsed_args->fw_security_info.mode = AVS_NET_SECURITY_PSK;
            parsed_args->fw_security_info.data.psk.identity =
                    avs_crypto_psk_identity_info_from_buffer(identity_buf,
                                                             identity_size);
            break;
        }
        case 260: {
            if (parsed_args->fw_security_info.mode != AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.mode
                                   != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }
            if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.data.psk.key.desc.source
                                   != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
                demo_log(ERROR, "--fw-psk-key specified more than once");
                goto finish;
            }
            uint8_t *psk_buf = NULL;
            size_t psk_size = 0;
            if (parse_hexstring(optarg, &psk_buf, &psk_size)) {
                demo_log(ERROR, "Invalid pre-shared key for firmware upgrade");
                goto finish;
            }
            parsed_args->fw_security_info.mode = AVS_NET_SECURITY_PSK;
            parsed_args->fw_security_info.data.psk.key =
                    avs_crypto_psk_key_info_from_buffer(psk_buf, psk_size);
            break;
        }
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#if defined(ANJAY_WITH_ATTR_STORAGE) && defined(AVS_COMMONS_STREAM_WITH_FILE)
        case 261:
            parsed_args->attr_storage_file = optarg;
            break;
#endif // defined(ANJAY_WITH_ATTR_STORAGE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
#ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
        case 'F':
            if (!optarg || strlen(optarg) < 1) {
                goto finish;
            }
            parsed_args->provisioning_file = optarg;
            break;
#endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
        case 267:
            if (parse_double(optarg,
                             &parsed_args->tx_params.ack_random_factor)) {
                demo_log(ERROR, "Expected ACK_RANDOM_FACTOR to be a floating "
                                "point number");
                goto finish;
            }
            break;
        case 268: {
            double ack_timeout_s;
            if (parse_double(optarg, &ack_timeout_s)) {
                demo_log(ERROR,
                         "Expected ACK_TIMEOUT to be a floating point number");
                goto finish;
            }
            parsed_args->tx_params.ack_timeout =
                    avs_time_duration_from_fscalar(ack_timeout_s, AVS_TIME_S);
            break;
        }
        case 269: {
            int32_t max_retransmit;
            if (parse_i32(optarg, &max_retransmit) || max_retransmit < 0) {
                demo_log(ERROR, "Expected MAX_RETRANSMIT to be an unsigned "
                                "integer");
                goto finish;
            }
            parsed_args->tx_params.max_retransmit = (unsigned) max_retransmit;
            break;
        }
        case 270: {
            double min_wait_s;
            if (parse_double(optarg, &min_wait_s) || min_wait_s <= 0) {
                demo_log(ERROR, "Expected DTLS_HS_RETRY_WAIT_MIN > 0");
                goto finish;
            }
            parsed_args->dtls_hs_tx_params.min =
                    avs_time_duration_from_fscalar(min_wait_s, AVS_TIME_S);
            break;
        }
        case 271: {
            double max_wait_s;
            if (parse_double(optarg, &max_wait_s) || max_wait_s <= 0) {
                demo_log(ERROR, "Expected DTLS_HS_RETRY_WAIT_MAX > 0");
                goto finish;
            }
            parsed_args->dtls_hs_tx_params.max =
                    avs_time_duration_from_fscalar(max_wait_s, AVS_TIME_S);
            break;
        }
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
        case 272:
            if (parse_double(optarg,
                             &parsed_args->fwu_tx_params.ack_random_factor)) {
                demo_log(ERROR, "Expected ACK_RANDOM_FACTOR to be a floating "
                                "point number");
                goto finish;
            }
            parsed_args->fwu_tx_params_modified = true;
            break;
        case 273: {
            double ack_timeout_s;
            if (parse_double(optarg, &ack_timeout_s)) {
                demo_log(ERROR,
                         "Expected ACK_TIMEOUT to be a floating point number");
                goto finish;
            }
            parsed_args->fwu_tx_params.ack_timeout =
                    avs_time_duration_from_fscalar(ack_timeout_s, AVS_TIME_S);
            parsed_args->fwu_tx_params_modified = true;
            break;
        }
        case 274: {
            int32_t max_retransmit;
            if (parse_i32(optarg, &max_retransmit) || max_retransmit < 0) {
                demo_log(ERROR, "Expected MAX_RETRANSMIT to be an unsigned "
                                "integer");
                goto finish;
            }
            parsed_args->fwu_tx_params.max_retransmit =
                    (unsigned) max_retransmit;
            parsed_args->fwu_tx_params_modified = true;
            break;
        }
#endif // ANJAY_WITH_MODULE_FW_UPDATE
        case 275:
            parsed_args->prefer_hierarchical_formats = true;
            break;
#ifdef ANJAY_WITH_LWM2M11
        case 276: {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            parsed_args->connection_args.servers[idx].sni = optarg;
            break;
        }
#endif // ANJAY_WITH_LWM2M11
        case 277:
            parsed_args->use_connection_id = true;
            break;
        case 278: {
            char *saveptr = NULL;
            char *str = optarg;
            const char *token;
            while ((token = avs_strtok(str, ",", &saveptr))) {
                uint32_t *reallocated = (uint32_t *) avs_realloc(
                        parsed_args->default_ciphersuites,
                        sizeof(*parsed_args->default_ciphersuites)
                                * ++parsed_args->default_ciphersuites_count);
                if (!reallocated) {
                    demo_log(ERROR, "Out of memory");
                    goto finish;
                }
                parsed_args->default_ciphersuites = reallocated;
                if (parse_u32(token,
                              &parsed_args->default_ciphersuites
                                       [parsed_args->default_ciphersuites_count
                                        - 1])) {
                    demo_log(ERROR, "Invalid ciphersuite ID: %s", token);
                    goto finish;
                }
                str = NULL;
            }
            break;
        }
#ifdef ANJAY_WITH_LWM2M11
        case 279: {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            if (parse_u32(optarg, &parsed_args->connection_args.servers[idx]
                                           .retry_count)) {
                demo_log(ERROR, "Invalid Retry Count value: %s", optarg);
                goto finish;
            }
            break;
        }
        case 280: {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            if (parse_u32(optarg, &parsed_args->connection_args.servers[idx]
                                           .retry_timer)) {
                demo_log(ERROR, "Invalid Retry Timer value: %s", optarg);
                goto finish;
            }
            break;
        }
        case 281: {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            if (parse_u32(optarg, &parsed_args->connection_args.servers[idx]
                                           .sequence_retry_count)) {
                demo_log(ERROR, "Invalid Sequence Retry Count value: %s",
                         optarg);
                goto finish;
            }
            break;
        }
        case 282: {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            if (parse_u32(optarg, &parsed_args->connection_args.servers[idx]
                                           .sequence_delay_timer)) {
                demo_log(ERROR, "Invalid Sequence Delay Timer value: %s",
                         optarg);
                goto finish;
            }
            break;
        }
#endif // ANJAY_WITH_LWM2M11
        case 283:
            parsed_args->prefer_same_socket_downloads = true;
            break;
        case 284:
            if (parse_size(optarg, &parsed_args->tx_params.nstart)) {
                demo_log(ERROR,
                         "Invalid NSTART value, expected non-negative integer, "
                         "got %s",
                         optarg);
                goto finish;
            }
            break;
#ifdef ANJAY_WITH_SEND
        case 287:
            parsed_args->fw_update_use_send = true;
            break;
#endif // ANJAY_WITH_SEND
#ifdef ANJAY_WITH_LWM2M11
        case 288:
            parsed_args->pkix_trust_store = optarg;
            break;
#endif // ANJAY_WITH_LWM2M11
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
        case 289:
            parsed_args->dm_persistence_file = optarg;
            break;
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
        case 298:
            use_external_security_info = true;
            break;
#endif // ANJAY_WITH_SECURITY_STRUCTURED
#ifdef ANJAY_WITH_LWM2M11
        case 299:
            parsed_args->rebuild_client_cert_chain = true;
            break;
#endif // ANJAY_WITH_LWM2M11
        case 306:
            parsed_args->alternative_logger = true;
            break;
        case 307: {
            const size_t identity_length = optarg ? strlen(optarg) : 0;
            if (parsed_args->connection_args.public_cert_or_psk_identity != NULL
                    || identity_length == 0) {
                demo_log(ERROR, "Invalid identity, either identity was set "
                                "twice or empty parameter was passed");
                goto finish;
            }
            if (clone_buffer(&parsed_args->connection_args
                                      .public_cert_or_psk_identity,
                             &parsed_args->connection_args
                                      .public_cert_or_psk_identity_size,
                             optarg, identity_length)) {
                retval = -ENOMEM;
                demo_log(ERROR,
                         "Error copying identity string, out of memory?");
                goto finish;
            }
            break;
        }
        case 308: {
            const size_t key_length = optarg ? strlen(optarg) : 0;
            if (parsed_args->connection_args.private_cert_or_psk_key != NULL
                    || key_length == 0) {
                demo_log(ERROR, "Invalid key, either key was set "
                                "twice or empty parameter was passed");
                goto finish;
            }
            if (clone_buffer(
                        &parsed_args->connection_args.private_cert_or_psk_key,
                        &parsed_args->connection_args
                                 .private_cert_or_psk_key_size,
                        optarg, key_length)) {
                retval = -ENOMEM;
                demo_log(ERROR, "Error copying key string, out of memory?");
                goto finish;
            }
            break;
        }
        case 317:
            if (parse_tls_version(optarg, &parsed_args->dtls_version)) {
                goto finish;
            }
            break;
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
        case 319: {
            double tcp_request_timeout;
            if (parse_double(optarg, &tcp_request_timeout)) {
                demo_log(ERROR, "Expected TCP request timeout to be a floating "
                                "point number");
                goto finish;
            }
            parsed_args->tcp_request_timeout =
                    avs_time_duration_from_fscalar(tcp_request_timeout,
                                                   AVS_TIME_S);
            break;
        }
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
        case 320:
            parsed_args->update_immediately_on_dm_change = true;
            break;
        case 321:
            parsed_args->enable_self_notify = true;
            break;
#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
        case 322:
            parsed_args->original_img_file_path = optarg;
            break;
#    if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
            && defined(AVS_COMMONS_STREAM_WITH_FILE)
        case 323:
            parsed_args->advanced_fw_updated_marker_path = optarg;
            break;
#    endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
           // defined(AVS_COMMONS_STREAM_WITH_FILE)
        case 324: {

            const avs_net_certificate_info_t cert_info = {
                .server_cert_validation = true,
                .trusted_certs =
                        avs_crypto_certificate_chain_info_from_file(optarg)
            };
            parsed_args->advanced_fw_security_info =
                    avs_net_security_info_from_certificates(cert_info);
            break;
        }
        case 325: {
            int result;
            if (parse_i32(optarg, &result)
                    || result < (int) ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL
                    || result > (int) ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL) {
                demo_log(ERROR, "invalid update result value: %s", optarg);
                goto finish;
            }
            parsed_args->advanced_fw_update_delayed_result =
                    (anjay_advanced_fw_update_result_t) result;
            break;
        }
#    ifdef ANJAY_WITH_SEND
        case 326:
            parsed_args->advanced_fw_update_use_send = true;
            break;
#    endif // ANJAY_WITH_SEND
        case 327: {
            double ack_timeout_s;
            if (parse_double(optarg, &ack_timeout_s)) {
                demo_log(ERROR,
                         "Expected ACK_TIMEOUT to be a floating point number");
                goto finish;
            }
            parsed_args->advanced_fwu_tx_params.ack_timeout =
                    avs_time_duration_from_fscalar(ack_timeout_s, AVS_TIME_S);
            parsed_args->advanced_fwu_tx_params_modified = true;
            break;
        }
#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
        case 328:
            parsed_args->start_offline = true;
            break;
        case 0:
            goto process;
        }
    }
process:
    retval = 0;
    if (!parsed_args->endpoint_name) {
        demo_log(ERROR,
                 "Endpoint name not specified, please use the -e option");
        retval = -1;
    }
    if (num_servers == 0
#ifdef AVS_COMMONS_STREAM_WITH_FILE
#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
            && !(parsed_args->dm_persistence_file
#        ifndef _WIN32
                 && !access(parsed_args->dm_persistence_file, R_OK)
#        endif // _WIN32
                         )
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
#    ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
            && !parsed_args->provisioning_file
#    endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
#endif     // AVS_COMMONS_STREAM_WITH_FILE
    ) {
        demo_log(ERROR, "At least one LwM2M Server URI needs to be specified, "
                        "please use the -u option");
        retval = -1;
    }
    for (int i = 0; i < num_servers; ++i) {
        server_entry_t *entry = &parsed_args->connection_args.servers[i];
        entry->id = (anjay_ssid_t) (i + 1);
        if (entry->security_iid == ANJAY_ID_INVALID) {
            entry->security_iid = (anjay_iid_t) entry->id;
        }
        if (entry->server_iid == ANJAY_ID_INVALID) {
            entry->server_iid = (anjay_iid_t) entry->id;
        }
    }
    identity_set =
            !!parsed_args->connection_args.public_cert_or_psk_identity_size;
    key_set = !!parsed_args->connection_args.private_cert_or_psk_key_size;
    if ((identity_set && (cert_path != default_cert_path))
            || (key_set && (key_path != default_key_path))) {
        demo_log(ERROR, "Certificate information cannot be loaded both from "
                        "file and immediate hex data at the same time");
        parsed_args->connection_args.security_mode = ANJAY_SECURITY_NOSEC;
        retval = -1;
    }
    if (parsed_args->connection_args.security_mode == ANJAY_SECURITY_PSK) {
        if (!identity_set
                && clone_buffer(&parsed_args->connection_args
                                         .public_cert_or_psk_identity,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity_size,
                                DEFAULT_PSK_IDENTITY,
                                sizeof(DEFAULT_PSK_IDENTITY) - 1)) {
            retval = -1;
        }
        if (!key_set
                && clone_buffer(&parsed_args->connection_args
                                         .private_cert_or_psk_key,
                                &parsed_args->connection_args
                                         .private_cert_or_psk_key_size,
                                DEFAULT_PSK_KEY, sizeof(DEFAULT_PSK_KEY) - 1)) {
            retval = -1;
        }
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
        if (use_external_security_info) {
            // NOTE: psk_identity and psk_key take priority in
            // security_object_reload()
            parsed_args->connection_args
                    .psk_identity = avs_crypto_psk_identity_info_from_buffer(
                    parsed_args->connection_args.public_cert_or_psk_identity,
                    parsed_args->connection_args
                            .public_cert_or_psk_identity_size);
            parsed_args->connection_args
                    .psk_key = avs_crypto_psk_key_info_from_buffer(
                    parsed_args->connection_args.private_cert_or_psk_key,
                    parsed_args->connection_args.private_cert_or_psk_key_size);
        }
#endif // ANJAY_WITH_SECURITY_STRUCTURED
    } else if (parsed_args->connection_args.security_mode
                       == ANJAY_SECURITY_CERTIFICATE
               || parsed_args->connection_args.security_mode
                          == ANJAY_SECURITY_EST) {
        if (identity_set ^ key_set) {
            demo_log(ERROR, "Setting public cert but not private cert (and "
                            "other way around) makes little sense");
            retval = -1;
        } else if (!identity_set) {
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
            if (use_external_security_info) {
                parsed_args->connection_args.public_cert =
                        avs_crypto_certificate_chain_info_from_file(cert_path);
                parsed_args->connection_args.private_key =
                        avs_crypto_private_key_info_from_file(key_path, NULL);
            } else
#endif // ANJAY_WITH_SECURITY_STRUCTURED
            {
                if (load_buffer_from_file(
                            &parsed_args->connection_args
                                     .public_cert_or_psk_identity,
                            &parsed_args->connection_args
                                     .public_cert_or_psk_identity_size,
                            cert_path)) {
                    demo_log(ERROR, "Could not load certificate from %s",
                             cert_path);
                    retval = -1;
                }
                if (load_buffer_from_file(
                            &parsed_args->connection_args
                                     .private_cert_or_psk_key,
                            &parsed_args->connection_args
                                     .private_cert_or_psk_key_size,
                            key_path)) {
                    demo_log(ERROR, "Could not load private key from %s",
                             key_path);
                    retval = -1;
                }
            }
        }
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
        else if (use_external_security_info) {
            // NOTE: public_cert and private_key take priority in
            // security_object_reload()
            parsed_args->connection_args.public_cert =
                    avs_crypto_certificate_chain_info_from_buffer(
                            parsed_args->connection_args
                                    .public_cert_or_psk_identity,
                            parsed_args->connection_args
                                    .public_cert_or_psk_identity_size);
            parsed_args->connection_args
                    .private_key = avs_crypto_private_key_info_from_buffer(
                    parsed_args->connection_args.private_cert_or_psk_key,
                    parsed_args->connection_args.private_cert_or_psk_key_size,
                    NULL);
        }
#endif // ANJAY_WITH_SECURITY_STRUCTURED
        if (server_public_key_path
                && load_buffer_from_file(
                           &parsed_args->connection_args.server_public_key,
                           &parsed_args->connection_args.server_public_key_size,
                           server_public_key_path)) {
            demo_log(ERROR, "Could not load server public key from %s",
                     server_public_key_path);
            retval = -1;
        }
    }
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
            && (parsed_args->fw_security_info.data.psk.identity.desc.source
                        == AVS_CRYPTO_DATA_SOURCE_EMPTY
                || parsed_args->fw_security_info.data.psk.key.desc.source
                           == AVS_CRYPTO_DATA_SOURCE_EMPTY)) {
        demo_log(ERROR, "Both identity and key must be provided when using PSK "
                        "for firmware upgrade security");
        retval = -1;
    }
#endif // ANJAY_WITH_MODULE_FW_UPDATE
finish:
    if (retval) {
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
        AVS_LIST_CLEAR(&parsed_args->access_entries);
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
        avs_free(parsed_args->default_ciphersuites);
        parsed_args->default_ciphersuites = NULL;
    }
    avs_free(default_cert_path);
    avs_free(default_key_path);
    return retval;
}
