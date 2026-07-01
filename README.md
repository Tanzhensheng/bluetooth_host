# mod_ble_host_demo

独立的 Linux BLE 主机 demo 工程，目标是先在工程外部搭出 `platform/mod` 可复用的蓝牙通道骨架，再逐步补齐 BlueZ GATT 接入和协议细节。

## 当前范围

- 对外接口固定为 socket 风格：`open/send/recv/close`
- 协议层集中在 `protocol.c/h`：负责 `A5/L/C/PSEQ/FSEQ/PROT/DATA/CS/96` 的封包、解析和会话状态机
- 底层链路集中在 `ble_link.c/h`：Linux 下优先走 BlueZ D-Bus；其他平台回退为 stub
- GATT 的 `Service UUID / Characteristic UUID / 写入方式 / Notify 方式` 仍允许先留空

## 目录结构

```text
include/
  mod_ble.h
  mod_ble_types.h
  mod_ble_log.h
  ble_link.h       BlueZ 底层链路接口
  protocol.h       协议封包、解析和会话接口
  demo_cli.h
src/
  main.c
  demo_cli.c
  mod_ble_log.c
  ble_link.c       BlueZ D-Bus 扫描、连接、GATT 写入和通知
  protocol.c       A5/L/C/PSEQ/FSEQ/PROT/DATA/CS/96 封包解析
  mod_ble.c
docs/
  gatt_placeholders.md
  superpowers/
    specs/
    plans/
```

## 对外 API

```c
int mod_ble_open(const char *target_id);
int mod_ble_send(const uint8_t *data, size_t len, uint8_t prot);
int mod_ble_recv(uint8_t *buf, size_t buf_size, int timeout_ms);
void mod_ble_close(void);
```

## 构建

```bash
cmake -S . -B build
cmake --build build
```

Linux 上如果要启用真实 BlueZ D-Bus 路径，需要准备：

```bash
sudo apt-get install -y libglib2.0-dev pkg-config
```

如果缺少 `glib-2.0/gio-2.0`，构建会自动回退到 stub 传输层。

如果目标环境暂时没有 `cmake`，可以直接用 `gcc` 验证当前骨架：

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -I include \
  src/main.c src/mod_ble.c src/mod_ble_log.c src/ble_link.c src/protocol.c \
  -o build/mod_ble_demo
```

## 运行

```bash
./build/mod_ble_demo --target demo-target --discover-only
```

```bash
./build/mod_ble_demo \
  --target demo-target \
  --service-uuid 12345678-1234-5678-1234-56789abcdef0 \
  --write-char-uuid 11111111-2222-3333-4444-555555555555 \
  --notify-char-uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee \
  01 02 03
```

当前建议的 VM 验证顺序：

1. 先跑 `--discover-only`，确认目标设备能被找到、连接，并打印 service/characteristic
2. 拿到真实 UUID 后，再补 `--service-uuid/--write-char-uuid/--notify-char-uuid`
3. 最后再验证发送和通知接收
