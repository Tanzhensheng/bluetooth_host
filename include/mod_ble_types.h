#ifndef MOD_BLE_TYPES_H
#define MOD_BLE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define MOD_BLE_MAX_TARGET_ID_LEN 64U
#define MOD_BLE_MAX_UUID_LEN 40U
#define MOD_BLE_MAX_FRAME_DATA_LEN 160U
#define MOD_BLE_MAX_HEX_DUMP_LEN 1024U

typedef enum {
    MOD_BLE_STATUS_OK = 0,
    MOD_BLE_STATUS_INVALID_ARG = -1,
    MOD_BLE_STATUS_STATE = -2,
    MOD_BLE_STATUS_IO = -3,
    MOD_BLE_STATUS_TIMEOUT = -4,
    MOD_BLE_STATUS_UNSUPPORTED = -5
} mod_ble_status_t;

typedef enum {
    MOD_BLE_PROTO_MODBUS = 0x00,
    MOD_BLE_PROTO_DLT645 = 0x01,
    MOD_BLE_PROTO_NEAR_FIELD = 0x10
} mod_ble_proto_t;

typedef enum {
    MOD_BLE_MODE_FULL = 0,
    MOD_BLE_MODE_DISCOVER_ONLY = 1,
    MOD_BLE_MODE_SCAN_ONLY = 2
} mod_ble_mode_t;

typedef struct {
    char service_uuid[MOD_BLE_MAX_UUID_LEN];
    char write_char_uuid[MOD_BLE_MAX_UUID_LEN];
    char notify_char_uuid[MOD_BLE_MAX_UUID_LEN];
} ble_gatt_placeholders_t;

typedef struct {
    char target_id[MOD_BLE_MAX_TARGET_ID_LEN];
    ble_gatt_placeholders_t gatt;
    int scan_timeout_ms;
    int recv_timeout_ms;
    mod_ble_mode_t mode;
    int verbose;
} mod_ble_config_t;

typedef struct {
    mod_ble_config_t config;
    int is_connected;
} ble_link_context_t;

typedef struct {
    uint8_t control;
    uint8_t pseq;
    uint8_t fseq;
    uint8_t prot;
    uint8_t data[MOD_BLE_MAX_FRAME_DATA_LEN];
    size_t data_len;
} protocol_frame_t;

typedef struct {
    uint8_t next_pseq;
    uint8_t last_fseq;
    int is_open;
} protocol_session_t;


#endif
