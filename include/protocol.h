#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "mod_ble_types.h"

int protocol_encode(const protocol_frame_t *frame, uint8_t *out, size_t out_size, size_t *out_len);
int protocol_decode(const uint8_t *raw, size_t raw_len, protocol_frame_t *frame);

void protocol_session_init(protocol_session_t *session);
int protocol_session_build_request(protocol_session_t *session, mod_ble_proto_t prot, const uint8_t *payload,
    size_t payload_len, protocol_frame_t *frame);
int protocol_session_parse_response(protocol_session_t *session, const uint8_t *raw, size_t raw_len, protocol_frame_t *frame);

#endif
