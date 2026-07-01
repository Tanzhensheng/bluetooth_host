#include "mod_ble.h"

#include "ble_link.h"
#include "mod_ble_log.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>

static ble_link_context_t g_client;
static protocol_session_t g_session;
static mod_ble_config_t g_config;
static int g_is_open = 0;

void mod_ble_config_init(mod_ble_config_t *config)
{
    if (config == NULL) {
        return;
    }

    (void)memset(config, 0, sizeof(*config));
    config->scan_timeout_ms = 5000;
    config->recv_timeout_ms = 3000;
    config->mode = MOD_BLE_MODE_FULL;
}

int mod_ble_configure(const mod_ble_config_t *config)
{
    if (config == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (g_is_open) {
        return MOD_BLE_STATUS_STATE;
    }

    g_config = *config;
    return MOD_BLE_STATUS_OK;
}

int mod_ble_open(const char *target_id)
{
    int status;

    if (g_is_open) {
        return MOD_BLE_STATUS_STATE;
    }

    g_client.config = g_config;
    if (target_id != NULL && target_id[0] != '\0') {
        (void)snprintf(g_client.config.target_id, sizeof(g_client.config.target_id), "%s", target_id);
    }

    status = ble_link_open(&g_client);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    protocol_session_init(&g_session);
    g_is_open = 1;
    return MOD_BLE_STATUS_OK;
}

int mod_ble_send(const uint8_t *data, size_t len, uint8_t prot)
{
    protocol_frame_t frame;
    uint8_t raw[MOD_BLE_MAX_HEX_DUMP_LEN];
    size_t raw_len = 0U;
    int status;

    if (!g_is_open) {
        return MOD_BLE_STATUS_STATE;
    }

    status = protocol_session_build_request(&g_session, (mod_ble_proto_t)prot, data, len, &frame);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    status = protocol_encode(&frame, raw, sizeof(raw), &raw_len);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    mod_ble_log_info("mod_ble_send pseq=%u fseq=0x%02X prot=0x%02X", frame.pseq, frame.fseq, prot);
    return ble_link_send(&g_client, raw, raw_len);
}

int mod_ble_recv(uint8_t *buf, size_t buf_size, int timeout_ms)
{
    uint8_t raw[MOD_BLE_MAX_HEX_DUMP_LEN];
    protocol_frame_t frame;
    int raw_len;
    int status;

    if (!g_is_open || buf == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    raw_len = ble_link_receive(&g_client, raw, sizeof(raw), timeout_ms);
    if (raw_len < 0) {
        return raw_len;
    }

    status = protocol_session_parse_response(&g_session, raw, (size_t)raw_len, &frame);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }
    if (buf_size < frame.data_len) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    (void)memcpy(buf, frame.data, frame.data_len);
    mod_ble_log_info("mod_ble_recv pseq=%u fseq=0x%02X data_len=%zu", frame.pseq, frame.fseq, frame.data_len);
    return (int)frame.data_len;
}

void mod_ble_close(void)
{
    if (!g_is_open) {
        return;
    }

    ble_link_close(&g_client);
    (void)memset(&g_client, 0, sizeof(g_client));
    (void)memset(&g_session, 0, sizeof(g_session));
    g_is_open = 0;
}
