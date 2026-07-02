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

enum {
    MOD_BLE_DIR_HOST = 0,
    MOD_BLE_DIR_TERMINAL = 1,
    MOD_BLE_PRM_ACK = 0,
    MOD_BLE_PRM_DATA = 1,
    MOD_BLE_FUN_ACK = 0,
    MOD_BLE_FUN_DATA = 1,
    MOD_BLE_FUN_REPAIR = 2,
    MOD_BLE_FRAME_FIRST = 0x80,
    MOD_BLE_REPAIR_SEQ = 1
};

static uint8_t mod_ble_make_control(uint8_t dir, uint8_t prm, uint8_t fun)
{
    return (uint8_t)(((dir & 0x01U) << 7U) | ((prm & 0x01U) << 6U) | (fun & 0x0FU));
}

static uint8_t mod_ble_control_dir(uint8_t control)
{
    return (uint8_t)((control >> 7U) & 0x01U);
}

static uint8_t mod_ble_control_prm(uint8_t control)
{
    return (uint8_t)((control >> 6U) & 0x01U);
}

static uint8_t mod_ble_control_fun(uint8_t control)
{
    return (uint8_t)(control & 0x0FU);
}

static unsigned mod_ble_frame_index(const protocol_frame_t *frame)
{
    if ((frame->fseq & MOD_BLE_FRAME_FIRST) != 0U) {
        return 0U;
    }
    return (unsigned)(frame->fseq & 0x3FU);
}

static unsigned mod_ble_frame_total(const protocol_frame_t *frame)
{
    if ((frame->fseq & MOD_BLE_FRAME_FIRST) == 0U) {
        return 0U;
    }
    return (unsigned)(frame->fseq & 0x3FU) + 1U;
}

static int mod_ble_send_frame(const protocol_frame_t *frame)
{
    uint8_t raw[MOD_BLE_MAX_HEX_DUMP_LEN];
    size_t raw_len = 0U;
    int status;

    status = protocol_encode(frame, raw, sizeof(raw), &raw_len);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }
    return ble_link_send(&g_client, raw, raw_len);
}

static int mod_ble_recv_frame(protocol_frame_t *frame, int timeout_ms)
{
    uint8_t raw[MOD_BLE_MAX_HEX_DUMP_LEN];
    int raw_len;

    raw_len = ble_link_receive(&g_client, raw, sizeof(raw), timeout_ms);
    if (raw_len < 0) {
        return raw_len;
    }
    return protocol_session_parse_response(&g_session, raw, (size_t)raw_len, frame);
}

static int mod_ble_send_ack_for_frame(const protocol_frame_t *request, int nack)
{
    protocol_frame_t ack;

    if (request == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    (void)memset(&ack, 0, sizeof(ack));
    ack.control = mod_ble_make_control(MOD_BLE_DIR_HOST, MOD_BLE_PRM_ACK, nack ? MOD_BLE_FUN_DATA : MOD_BLE_FUN_ACK);
    ack.pseq = request->pseq;
    ack.fseq = MOD_BLE_FRAME_FIRST;
    ack.prot = request->prot;
    mod_ble_log_info("host ack pseq=%u nack=%d", ack.pseq, nack ? 1 : 0);
    return mod_ble_send_frame(&ack);
}

static int mod_ble_send_repair_request(uint8_t prot, uint8_t missing_seq)
{
    protocol_frame_t frame;

    (void)memset(&frame, 0, sizeof(frame));
    frame.control = mod_ble_make_control(MOD_BLE_DIR_HOST, MOD_BLE_PRM_DATA, MOD_BLE_FUN_REPAIR);
    frame.pseq = g_session.next_pseq++;
    frame.fseq = MOD_BLE_FRAME_FIRST;
    frame.prot = prot;
    frame.data[0] = missing_seq;
    frame.data_len = 1U;
    mod_ble_log_info("host repair request missing_seq=%u pseq=%u", missing_seq, frame.pseq);
    return mod_ble_send_frame(&frame);
}

static int mod_ble_copy_reassembled_payload(const uint8_t frame_data[][MOD_BLE_MAX_FRAME_DATA_LEN],
    const size_t *frame_len,
    unsigned total_frames,
    uint8_t *reply_buf,
    size_t reply_buf_size)
{
    unsigned i;
    size_t offset = 0U;

    if (frame_data == NULL || frame_len == NULL || reply_buf == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    for (i = 0U; i < total_frames; ++i) {
        if (offset + frame_len[i] > reply_buf_size) {
            return MOD_BLE_STATUS_INVALID_ARG;
        }
        if (frame_len[i] > 0U) {
            (void)memcpy(&reply_buf[offset], frame_data[i], frame_len[i]);
            offset += frame_len[i];
        }
    }
    return (int)offset;
}

static int mod_ble_all_frames_received(const uint8_t *received, unsigned total_frames)
{
    unsigned i;

    if (total_frames == 0U) {
        return 0;
    }
    for (i = 0U; i < total_frames; ++i) {
        if (received[i] == 0U) {
            return 0;
        }
    }
    return 1;
}

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

int mod_ble_run_flow(const uint8_t *data, size_t len, uint8_t prot, uint8_t *reply_buf, size_t reply_buf_size)
{
    protocol_frame_t request;
    uint8_t received[64];
    uint8_t frame_data[64][MOD_BLE_MAX_FRAME_DATA_LEN];
    size_t frame_len[64];
    unsigned total_frames = 0U;
    int dropped_abnormal_once = 0;
    int repair_requested = 0;
    int repair_mode_accept = 0;
    int loops = 0;
    int status;

    if (!g_is_open || data == NULL || reply_buf == NULL || len > MOD_BLE_MAX_FRAME_DATA_LEN) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    (void)memset(received, 0, sizeof(received));
    (void)memset(frame_data, 0, sizeof(frame_data));
    (void)memset(frame_len, 0, sizeof(frame_len));
    (void)memset(&request, 0, sizeof(request));

    request.control = mod_ble_make_control(MOD_BLE_DIR_HOST, MOD_BLE_PRM_DATA, MOD_BLE_FUN_DATA);
    request.pseq = g_session.next_pseq++;
    request.fseq = MOD_BLE_FRAME_FIRST;
    request.prot = prot;
    request.data_len = len;
    (void)memcpy(request.data, data, len);

    mod_ble_log_info("flow send request pseq=%u control=0x%02X", request.pseq, request.control);
    status = mod_ble_send_frame(&request);
    if (status != MOD_BLE_STATUS_OK) {
        return status;
    }

    while (loops++ < 160) {
        protocol_frame_t frame;
        uint8_t dir;
        uint8_t prm;
        uint8_t fun;

        status = mod_ble_recv_frame(&frame, g_config.recv_timeout_ms);
        if (status == MOD_BLE_STATUS_TIMEOUT && g_config.flow_mode == MOD_BLE_FLOW_REPAIR &&
            total_frames > 0U && received[MOD_BLE_REPAIR_SEQ] == 0U && !repair_requested) {
            status = mod_ble_send_repair_request(prot, (uint8_t)MOD_BLE_REPAIR_SEQ);
            if (status != MOD_BLE_STATUS_OK) {
                return status;
            }
            repair_requested = 1;
            repair_mode_accept = 1;
            continue;
        }
        if (status != MOD_BLE_STATUS_OK) {
            return status;
        }

        dir = mod_ble_control_dir(frame.control);
        prm = mod_ble_control_prm(frame.control);
        fun = mod_ble_control_fun(frame.control);
        mod_ble_log_info("flow rx pseq=%u fseq=0x%02X control=0x%02X data_len=%zu",
            frame.pseq,
            frame.fseq,
            frame.control,
            frame.data_len);

        if (dir == MOD_BLE_DIR_TERMINAL && prm == MOD_BLE_PRM_ACK) {
            if (fun == MOD_BLE_FUN_ACK) {
                mod_ble_log_info("flow terminal ack pseq=%u", frame.pseq);
                // Sending a host ACK here also clears the single-notify buffer before waiting for terminal data.
                (void)mod_ble_send_ack_for_frame(&frame, 0);
                continue;
            }
            if (fun == MOD_BLE_FUN_DATA) {
                mod_ble_log_error("flow terminal nack pseq=%u", frame.pseq);
                return MOD_BLE_STATUS_IO;
            }
        }

        if (dir == MOD_BLE_DIR_TERMINAL && prm == MOD_BLE_PRM_DATA && fun == MOD_BLE_FUN_DATA) {
            unsigned index = mod_ble_frame_index(&frame);

            if ((frame.fseq & MOD_BLE_FRAME_FIRST) != 0U) {
                total_frames = mod_ble_frame_total(&frame);
                if (total_frames == 0U || total_frames > 64U) {
                    return MOD_BLE_STATUS_IO;
                }
            }
            if (total_frames == 0U || index >= total_frames || frame.data_len > MOD_BLE_MAX_FRAME_DATA_LEN) {
                (void)mod_ble_send_ack_for_frame(&frame, 1);
                return MOD_BLE_STATUS_IO;
            }

            if (g_config.flow_mode == MOD_BLE_FLOW_ABNORMAL && index == MOD_BLE_REPAIR_SEQ && !dropped_abnormal_once) {
                dropped_abnormal_once = 1;
                mod_ble_log_info("flow simulate abnormal nack seq=%u", index);
                status = mod_ble_send_ack_for_frame(&frame, 1);
                if (status != MOD_BLE_STATUS_OK) {
                    return status;
                }
                continue;
            }

            if (g_config.flow_mode == MOD_BLE_FLOW_REPAIR && index == MOD_BLE_REPAIR_SEQ && !repair_mode_accept) {
                mod_ble_log_info("flow simulate missing seq=%u with nack", index);
                status = mod_ble_send_ack_for_frame(&frame, 1);
                if (status != MOD_BLE_STATUS_OK) {
                    return status;
                }
                continue;
            }

            if (received[index] == 0U) {
                frame_len[index] = frame.data_len;
                if (frame.data_len > 0U) {
                    (void)memcpy(frame_data[index], frame.data, frame.data_len);
                }
                received[index] = 1U;
            }

            status = mod_ble_send_ack_for_frame(&frame, 0);
            if (status != MOD_BLE_STATUS_OK) {
                return status;
            }

            if (mod_ble_all_frames_received(received, total_frames)) {
                return mod_ble_copy_reassembled_payload(frame_data, frame_len, total_frames, reply_buf, reply_buf_size);
            }
        }
    }

    return MOD_BLE_STATUS_TIMEOUT;
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
