#include "protocol.h"

#include <string.h>

enum {
    PROTO_FRAME_HEAD = 0xA5,
    PROTO_FRAME_TAIL = 0x96,
    PROTO_LENGTH_FIELD_SIZE = 2,
    PROTO_MIN_FRAME_SIZE = 9
};

static uint8_t protocol_checksum(const protocol_frame_t *frame)
{
    size_t i;
    uint32_t sum = 0U;

    sum += frame->control;
    sum += frame->pseq;
    sum += frame->fseq;
    sum += frame->prot;
    for (i = 0; i < frame->data_len; ++i) {
        sum += frame->data[i];
    }

    return (uint8_t)(sum & 0xFFU);
}

int protocol_encode(const protocol_frame_t *frame, uint8_t *out, size_t out_size, size_t *out_len)
{
    size_t payload_len;
    uint16_t length_field;

    if (frame == NULL || out == NULL || out_len == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    payload_len = frame->data_len + 4U;
    if (frame->data_len > MOD_BLE_MAX_FRAME_DATA_LEN) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (out_size < frame->data_len + 9U) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    length_field = (uint16_t)payload_len;

    out[0] = PROTO_FRAME_HEAD;
    out[1] = (uint8_t)(length_field & 0xFFU);
    out[2] = (uint8_t)((length_field >> 8) & 0xFFU);
    out[3] = frame->control;
    out[4] = frame->pseq;
    out[5] = frame->fseq;
    out[6] = frame->prot;
    (void)memcpy(&out[7], frame->data, frame->data_len);
    out[7 + frame->data_len] = protocol_checksum(frame);
    out[8 + frame->data_len] = PROTO_FRAME_TAIL;
    *out_len = 9U + frame->data_len;
    return MOD_BLE_STATUS_OK;
}

int protocol_decode(const uint8_t *raw, size_t raw_len, protocol_frame_t *frame)
{
    size_t payload_len;
    uint8_t checksum;
    uint16_t length_field;

    if (raw == NULL || frame == NULL || raw_len < PROTO_MIN_FRAME_SIZE) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (raw[0] != PROTO_FRAME_HEAD || raw[raw_len - 1U] != PROTO_FRAME_TAIL) {
        return MOD_BLE_STATUS_IO;
    }
    length_field = (uint16_t)raw[1] | ((uint16_t)raw[2] << 8);
    if ((size_t)length_field + 5U != raw_len) {
        return MOD_BLE_STATUS_IO;
    }
    if (length_field < 4U) {
        return MOD_BLE_STATUS_IO;
    }

    payload_len = (size_t)length_field - 4U;
    if (payload_len > MOD_BLE_MAX_FRAME_DATA_LEN) {
        return MOD_BLE_STATUS_UNSUPPORTED;
    }

    (void)memset(frame, 0, sizeof(*frame));
    frame->control = raw[3];
    frame->pseq = raw[4];
    frame->fseq = raw[5];
    frame->prot = raw[6];
    frame->data_len = payload_len;
    (void)memcpy(frame->data, &raw[7], payload_len);
    checksum = protocol_checksum(frame);
    if (checksum != raw[7 + payload_len]) {
        return MOD_BLE_STATUS_IO;
    }
    return MOD_BLE_STATUS_OK;
}
void protocol_session_init(protocol_session_t *session)
{
    if (session == NULL) {
        return;
    }

    (void)memset(session, 0, sizeof(*session));
    session->is_open = 1;
}

int protocol_session_build_request(protocol_session_t *session, mod_ble_proto_t prot, const uint8_t *payload,
    size_t payload_len, protocol_frame_t *frame)
{
    if (session == NULL || payload == NULL || frame == NULL || payload_len > MOD_BLE_MAX_FRAME_DATA_LEN) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }
    if (!session->is_open) {
        return MOD_BLE_STATUS_STATE;
    }

    (void)memset(frame, 0, sizeof(*frame));
    frame->control = 0x43U;
    frame->pseq = session->next_pseq++;
    frame->fseq = 0x80U;
    frame->prot = (uint8_t)prot;
    frame->data_len = payload_len;
    (void)memcpy(frame->data, payload, payload_len);
    return MOD_BLE_STATUS_OK;
}

int protocol_session_parse_response(protocol_session_t *session, const uint8_t *raw, size_t raw_len, protocol_frame_t *frame)
{
    int status;

    if (session == NULL) {
        return MOD_BLE_STATUS_INVALID_ARG;
    }

    status = protocol_decode(raw, raw_len, frame);
    if (status == MOD_BLE_STATUS_OK && frame != NULL) {
        session->last_fseq = frame->fseq;
    }
    return status;
}
