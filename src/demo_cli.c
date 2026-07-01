#include "demo_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int demo_cli_parse_hex_payload(int argc, char **argv, int start_index, demo_cli_options_t *options)
{
    int i;

    for (i = start_index; i < argc; ++i) {
        char *end = NULL;
        unsigned long value;

        if (options->payload_len >= MOD_BLE_MAX_FRAME_DATA_LEN) {
            return MOD_BLE_STATUS_INVALID_ARG;
        }

        value = strtoul(argv[i], &end, 16);
        if (end == argv[i] || *end != '\0' || value > 0xFFUL) {
            return MOD_BLE_STATUS_INVALID_ARG;
        }

        options->payload[options->payload_len++] = (uint8_t)value;
    }

    return MOD_BLE_STATUS_OK;
}

static int demo_cli_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (strlen(src) >= dst_size) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    (void)snprintf(dst, dst_size, "%s", src);
    return MOD_BLE_STATUS_OK;
}

void demo_cli_options_init(demo_cli_options_t *options)
{
    if (options == NULL) {
        return;
    }

    (void)memset(options, 0, sizeof(*options));
    options->config.scan_timeout_ms = 15000;
    options->config.recv_timeout_ms = 3000;
    options->config.mode = MOD_BLE_MODE_FULL;
}

int demo_cli_parse(int argc, char **argv, demo_cli_options_t *options)
{
    int i;
    int status;

    if (argc <= 0 || argv == NULL || options == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--target") == 0 && (i + 1) < argc) {
            status = demo_cli_copy_string(options->config.target_id, sizeof(options->config.target_id), argv[++i]);
            if (status != MOD_BLE_STATUS_OK) {
                return status;
            }
        } else if (strcmp(argv[i], "--service-uuid") == 0 && (i + 1) < argc) {
            status = demo_cli_copy_string(options->config.gatt.service_uuid, sizeof(options->config.gatt.service_uuid), argv[++i]);
            if (status != MOD_BLE_STATUS_OK) {
                return status;
            }
        } else if (strcmp(argv[i], "--write-char-uuid") == 0 && (i + 1) < argc) {
            status = demo_cli_copy_string(options->config.gatt.write_char_uuid, sizeof(options->config.gatt.write_char_uuid), argv[++i]);
            if (status != MOD_BLE_STATUS_OK) {
                return status;
            }
        } else if (strcmp(argv[i], "--notify-char-uuid") == 0 && (i + 1) < argc) {
            status = demo_cli_copy_string(options->config.gatt.notify_char_uuid, sizeof(options->config.gatt.notify_char_uuid), argv[++i]);
            if (status != MOD_BLE_STATUS_OK) {
                return status;
            }
        } else if (strcmp(argv[i], "--scan-timeout-ms") == 0 && (i + 1) < argc) {
            options->config.scan_timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--recv-timeout-ms") == 0 && (i + 1) < argc) {
            options->config.recv_timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--discover-only") == 0) {
            options->config.mode = MOD_BLE_MODE_DISCOVER_ONLY;
        } else if (strcmp(argv[i], "--scan-only") == 0) {
            options->config.mode = MOD_BLE_MODE_SCAN_ONLY;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            options->config.verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return MOD_BLE_STATUS_UNSUPPORTED;
        } else {
            return demo_cli_parse_hex_payload(argc, argv, i, options);
        }
    }

    if (options->config.target_id[0] == '\0') {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (options->config.scan_timeout_ms <= 0 || options->config.recv_timeout_ms <= 0) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    return MOD_BLE_STATUS_OK;
}

void demo_cli_print_usage(const char *program_name)
{
    const char *name = (program_name == NULL) ? "mod_ble_demo" : program_name;

    (void)fprintf(stdout,
        "Usage: %s --target <name-or-mac> [--discover-only|--scan-only] \\\n"
        "  [--service-uuid <uuid>] [--write-char-uuid <uuid>] [--notify-char-uuid <uuid>] \\\n"
        "  [--scan-timeout-ms <ms>] [--recv-timeout-ms <ms>] [--verbose] [payload hex ...]\n",
        name);
}
