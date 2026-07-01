# 蓝牙模块迁移说明

本文说明如何把 `bluetooth_host` 中的蓝牙能力迁移到另一个工程。当前工程已经拆成两层：

- 底层链路层：`ble_link.c/h`
- 协议封装解析层：`protocol.c/h`

如果目标工程也分 module 层和协议层，建议分别迁移，而不是直接搬整个 demo 工程。

## 迁移方式一：整体搬运

如果目标工程只想直接调用一个蓝牙 API，不关心内部层次，可以搬运完整模块：

```text
include/mod_ble.h
include/mod_ble_types.h
include/ble_link.h
include/protocol.h
include/mod_ble_log.h

src/mod_ble.c
src/ble_link.c
src/protocol.c
src/mod_ble_log.c
```

上层只需要包含：

```c
#include "mod_ble.h"
```

调用顺序：

```text
mod_ble_config_init
mod_ble_configure
mod_ble_open
mod_ble_send
mod_ble_recv
mod_ble_close
```

这种方式最省事，但 `mod_ble.c` 会成为目标工程里的胶水层。

## 迁移方式二：按层分别搬运

如果目标工程已经有自己的 module 层和协议层，建议按下面方式拆开迁移。

### 底层蓝牙 module

搬运文件：

```text
include/ble_link.h
include/mod_ble_types.h
include/mod_ble_log.h

src/ble_link.c
src/mod_ble_log.c
```

职责：

```text
BlueZ D-Bus
LE 扫描
设备匹配
连接
GATT discovery
WriteValue
Notify 接收
```

主要接口：

```c
int ble_link_open(ble_link_context_t *ctx);
int ble_link_send(ble_link_context_t *ctx, const uint8_t *data, size_t len);
int ble_link_receive(ble_link_context_t *ctx, uint8_t *buf, size_t buf_size, int timeout_ms);
void ble_link_close(ble_link_context_t *ctx);
```

### 协议层

搬运文件：

```text
include/protocol.h
include/mod_ble_types.h

src/protocol.c
```

职责：

```text
A5/L/C/PSEQ/FSEQ/PROT/DATA/CS/96 封包
协议帧解析
校验和检查
PSEQ/FSEQ 会话状态
```

主要接口：

```c
void protocol_session_init(protocol_session_t *session);
int protocol_session_build_request(protocol_session_t *session, mod_ble_proto_t prot,
    const uint8_t *payload, size_t payload_len, protocol_frame_t *frame);
int protocol_encode(const protocol_frame_t *frame, uint8_t *out, size_t out_size, size_t *out_len);
int protocol_decode(const uint8_t *raw, size_t raw_len, protocol_frame_t *frame);
int protocol_session_parse_response(protocol_session_t *session, const uint8_t *raw,
    size_t raw_len, protocol_frame_t *frame);
```

### 目标工程中的推荐调用链

```text
业务层
  -> protocol_session_build_request
  -> protocol_encode
  -> ble_link_send
  -> ble_link_receive
  -> protocol_session_parse_response
```

如果目标工程已有自己的协议调度层，可以不搬：

```text
include/mod_ble.h
src/mod_ble.c
```

`mod_ble.c` 只是当前 demo 工程的胶水层，用来把 `protocol` 和 `ble_link` 串起来。

## 编译依赖

Linux 真实 BlueZ 路径需要：

```text
glib-2.0
gio-2.0
```

CMake 示例：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0 gio-2.0)

target_compile_definitions(your_target PRIVATE MOD_BLE_HAVE_GIO=1)
target_include_directories(your_target PRIVATE
    path/to/include
    ${GLIB2_INCLUDE_DIRS}
)
target_link_libraries(your_target PRIVATE ${GLIB2_LIBRARIES})
```

如果没有定义 `MOD_BLE_HAVE_GIO`，`ble_link.c` 会走 stub backend，不会访问 BlueZ 或蓝牙硬件。

## 当前耦合点

`mod_ble_types.h` 目前同时包含：

```text
BLE 配置和链路上下文
协议帧和协议会话结构
公共状态码
```

因此按层迁移时，底层 module 和协议层都会依赖 `mod_ble_types.h`。这可以先接受；如果目标工程对层间依赖要求更严格，建议下一步拆成：

```text
ble_link_types.h      // ble_link_context_t, mod_ble_config_t, status
protocol_types.h      // protocol_frame_t, protocol_session_t, prot enum
```

拆分后，底层链路层和协议层的边界会更干净。

## 不建议搬运的 demo 文件

下面文件只服务于当前 demo，不属于可复用蓝牙模块：

```text
src/main.c
src/demo_cli.c
include/demo_cli.h
tests/
docs/superpowers/
```

