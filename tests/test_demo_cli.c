#include "demo_cli.h"

#include <stdio.h>
#include <string.h>

static int assert_true(int condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "ASSERT FAILED: %s\n", message);
        return 1;
    }

    return 0;
}

static int test_parse_discovery_args_and_payload(void)
{
    char *argv[] = {
        "mod_ble_demo",
        "--target", "AA:BB:CC:DD:EE:FF",
        "--service-uuid", "12345678-1234-5678-1234-56789abcdef0",
        "--write-char-uuid", "11111111-2222-3333-4444-555555555555",
        "--notify-char-uuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        "--discover-only",
        "--scan-timeout-ms", "4500",
        "01", "02", "03"
    };
    demo_cli_options_t options;
    int status;

    demo_cli_options_init(&options);
    status = demo_cli_parse((int)(sizeof(argv) / sizeof(argv[0])), argv, &options);
    if (assert_true(status == MOD_BLE_STATUS_OK, "parse should succeed") != 0) {
        return 1;
    }
    if (assert_true(strcmp(options.config.target_id, "AA:BB:CC:DD:EE:FF") == 0, "target should parse") != 0) {
        return 1;
    }
    if (assert_true(options.config.mode == MOD_BLE_MODE_DISCOVER_ONLY, "discover-only flag should parse") != 0) {
        return 1;
    }
    if (assert_true(options.config.scan_timeout_ms == 4500, "scan timeout should parse") != 0) {
        return 1;
    }
    if (assert_true(strcmp(options.config.gatt.service_uuid, "12345678-1234-5678-1234-56789abcdef0") == 0, "service uuid should parse") != 0) {
        return 1;
    }
    if (assert_true(strcmp(options.config.gatt.write_char_uuid, "11111111-2222-3333-4444-555555555555") == 0, "write uuid should parse") != 0) {
        return 1;
    }
    if (assert_true(strcmp(options.config.gatt.notify_char_uuid, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee") == 0, "notify uuid should parse") != 0) {
        return 1;
    }
    if (assert_true(options.payload_len == 3U, "payload length should parse") != 0) {
        return 1;
    }
    if (assert_true(options.payload[0] == 0x01U && options.payload[1] == 0x02U && options.payload[2] == 0x03U, "payload bytes should parse") != 0) {
        return 1;
    }

    return 0;
}

static int test_parse_defaults_without_payload(void)
{
    char *argv[] = {
        "mod_ble_demo",
        "--target", "demo-target"
    };
    demo_cli_options_t options;
    int status;

    demo_cli_options_init(&options);
    status = demo_cli_parse((int)(sizeof(argv) / sizeof(argv[0])), argv, &options);
    if (assert_true(status == MOD_BLE_STATUS_OK, "minimal parse should succeed") != 0) {
        return 1;
    }
    if (assert_true(options.config.mode == MOD_BLE_MODE_FULL, "default mode should be full") != 0) {
        return 1;
    }
    if (assert_true(options.config.scan_timeout_ms == 15000, "default scan timeout should be 15000") != 0) {
        return 1;
    }
    if (assert_true(options.config.recv_timeout_ms == 3000, "default recv timeout should be 3000") != 0) {
        return 1;
    }
    if (assert_true(options.payload_len == 0U, "payload should default to empty") != 0) {
        return 1;
    }

    return 0;
}

int main(void)
{
    int failed = 0;

    failed |= test_parse_discovery_args_and_payload();
    failed |= test_parse_defaults_without_payload();

    if (failed != 0) {
        return 1;
    }

    (void)fprintf(stdout, "test_demo_cli: PASS\n");
    return 0;
}
