#ifndef MOD_BLE_H
#define MOD_BLE_H

#include "mod_ble_types.h"

void mod_ble_config_init(mod_ble_config_t *config);
int mod_ble_configure(const mod_ble_config_t *config);
int mod_ble_open(const char *target_id);
int mod_ble_send(const uint8_t *data, size_t len, uint8_t prot);
int mod_ble_recv(uint8_t *buf, size_t buf_size, int timeout_ms);
int mod_ble_run_flow(const uint8_t *data, size_t len, uint8_t prot, uint8_t *reply_buf, size_t reply_buf_size);
void mod_ble_close(void);

#endif
