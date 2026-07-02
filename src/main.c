#include "mod_ble.h"
#include "mod_ble_log.h"
#include "demo_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t fill_default_payload(uint8_t *buf, size_t buf_size)
{
    static const uint8_t sample[] = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'B', 'L', 'E'};

    if (buf_size < sizeof(sample)) {
        return 0U;
    }

    (void)memcpy(buf, sample, sizeof(sample));
    return sizeof(sample);
}

int main(int argc, char **argv)
{
    demo_cli_options_t options;
    uint8_t rx_buf[MOD_BLE_MAX_FRAME_DATA_LEN];
    int status;
    int rx_len;

    demo_cli_options_init(&options);
    status = demo_cli_parse(argc, argv, &options);
    if (status == MOD_BLE_STATUS_UNSUPPORTED) {
        demo_cli_print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (status != MOD_BLE_STATUS_OK) {
        mod_ble_log_error("invalid arguments");
        demo_cli_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (options.payload_len == 0U) {
        options.payload_len = fill_default_payload(options.payload, sizeof(options.payload));
    }

    if (options.config.target_id[0] == '\0') {
        mod_ble_log_error("target is required");
        return EXIT_FAILURE;
    }

    status = mod_ble_configure(&options.config);
    if (status != MOD_BLE_STATUS_OK) {
        mod_ble_log_error("mod_ble_configure failed: %d", status);
        return EXIT_FAILURE;
    }

    status = mod_ble_open(options.config.target_id);
    if (status != MOD_BLE_STATUS_OK) {
        mod_ble_log_error("mod_ble_open failed: %d", status);
        return EXIT_FAILURE;
    }

    if (options.config.mode != MOD_BLE_MODE_FULL) {
        mod_ble_log_info("demo completed in non-full mode");
        mod_ble_close();
        return EXIT_SUCCESS;
    }

    rx_len = mod_ble_run_flow(options.payload, options.payload_len, MOD_BLE_PROTO_NEAR_FIELD, rx_buf, sizeof(rx_buf));
    if (rx_len < 0) {
        mod_ble_log_error("mod_ble_run_flow failed: %d", rx_len);
        mod_ble_close();
        return EXIT_FAILURE;
    }

    mod_ble_log_info("demo completed with %d bytes of payload", rx_len);
    if (rx_len > 0) {
        (void)fprintf(stdout, "[PAYLOAD] ");
        for (int i = 0; i < rx_len; ++i) {
            (void)fprintf(stdout, "%02X ", rx_buf[i]);
        }
        (void)fprintf(stdout, "\n");
    }
    mod_ble_close();
    return EXIT_SUCCESS;
}
