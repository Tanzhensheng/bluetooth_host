#ifndef BLE_LINK_H
#define BLE_LINK_H

#include "mod_ble_types.h"

int ble_link_open(ble_link_context_t *ctx);
int ble_link_send(ble_link_context_t *ctx, const uint8_t *data, size_t len);
int ble_link_receive(ble_link_context_t *ctx, uint8_t *buf, size_t buf_size, int timeout_ms);
void ble_link_close(ble_link_context_t *ctx);

#endif
