#include "protocol.h"

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

static int test_encode_uses_two_byte_length_and_includes_prot_in_length_and_checksum(void)
{
    protocol_frame_t frame;
    uint8_t raw[32];
    size_t raw_len = 0U;
    int status;

    (void)memset(&frame, 0, sizeof(frame));
    frame.control = 0x43U;
    frame.pseq = 0x12U;
    frame.fseq = 0x80U;
    frame.prot = 0x10U;
    frame.data[0] = 0x01U;
    frame.data[1] = 0x02U;
    frame.data[2] = 0x03U;
    frame.data_len = 3U;

    status = protocol_encode(&frame, raw, sizeof(raw), &raw_len);
    if (assert_true(status == MOD_BLE_STATUS_OK, "encode should succeed") != 0) {
        return 1;
    }
    if (assert_true(raw_len == 12U, "encoded frame length should be 12 bytes") != 0) {
        return 1;
    }
    if (assert_true(raw[0] == 0xA5U, "frame should start with 0xA5") != 0) {
        return 1;
    }
    if (assert_true(raw[1] == 0x07U && raw[2] == 0x00U, "L should be 2-byte little-endian and include PROT") != 0) {
        return 1;
    }
    if (assert_true(raw[3] == 0x43U && raw[4] == 0x12U && raw[5] == 0x80U, "header fields should follow length") != 0) {
        return 1;
    }
    if (assert_true(raw[6] == 0x10U, "PROT should be present before DATA") != 0) {
        return 1;
    }
    if (assert_true(raw[7] == 0x01U && raw[8] == 0x02U && raw[9] == 0x03U, "DATA bytes should be preserved") != 0) {
        return 1;
    }
    if (assert_true(raw[10] == 0xEBU, "checksum should include C/PSEQ/FSEQ/PROT/DATA") != 0) {
        return 1;
    }
    if (assert_true(raw[11] == 0x96U, "frame should end with 0x96") != 0) {
        return 1;
    }

    return 0;
}

static int test_decode_round_trips_two_byte_length_frame(void)
{
    static const uint8_t raw[] = {0xA5U, 0x07U, 0x00U, 0x43U, 0x12U, 0x80U, 0x10U, 0x01U, 0x02U, 0x03U, 0xEBU, 0x96U};
    protocol_frame_t frame;
    int status;

    (void)memset(&frame, 0, sizeof(frame));
    status = protocol_decode(raw, sizeof(raw), &frame);
    if (assert_true(status == MOD_BLE_STATUS_OK, "decode should succeed") != 0) {
        return 1;
    }
    if (assert_true(frame.control == 0x43U, "control should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.pseq == 0x12U, "pseq should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.fseq == 0x80U, "fseq should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.prot == 0x10U, "prot should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.data_len == 3U, "data length should be decoded from 2-byte L") != 0) {
        return 1;
    }
    if (assert_true(frame.data[0] == 0x01U && frame.data[1] == 0x02U && frame.data[2] == 0x03U, "data bytes should round-trip") != 0) {
        return 1;
    }

    return 0;
}

static int test_decode_accepts_zero_payload_ack_frame(void)
{
    static const uint8_t raw[] = {0xA5U, 0x04U, 0x00U, 0x80U, 0x00U, 0x80U, 0x10U, 0x10U, 0x96U};
    protocol_frame_t frame;
    int status;

    (void)memset(&frame, 0, sizeof(frame));
    status = protocol_decode(raw, sizeof(raw), &frame);
    if (assert_true(status == MOD_BLE_STATUS_OK, "decode zero-payload ack should succeed") != 0) {
        return 1;
    }
    if (assert_true(frame.control == 0x80U, "ack control should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.pseq == 0x00U, "ack pseq should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.fseq == 0x80U, "ack fseq should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.prot == 0x10U, "ack prot should round-trip") != 0) {
        return 1;
    }
    if (assert_true(frame.data_len == 0U, "ack payload should be empty") != 0) {
        return 1;
    }

    return 0;
}

int main(void)
{
    int failed = 0;

    failed |= test_encode_uses_two_byte_length_and_includes_prot_in_length_and_checksum();
    failed |= test_decode_round_trips_two_byte_length_frame();
    failed |= test_decode_accepts_zero_payload_ack_frame();

    if (failed != 0) {
        return 1;
    }

    (void)fprintf(stdout, "test_protocol: PASS\n");
    return 0;
}
