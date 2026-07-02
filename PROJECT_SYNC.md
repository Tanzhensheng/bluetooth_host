# PROJECT_SYNC

## 基本信息

- 项目名称：ble_host_demo
- 项目编号：project002

## 全局同步状态

- 已读取全局规则版本：v2.0
- 最近读取时间：2026-06-30 10:59:58
- 最近全项目刷新检查时间：2026-06-30 10:59:58
- 当前是否落后于全局版本：否

## 待沉淀经验

- [ ] 基于 BlueZ D-Bus 的 BLE GATT 主机模块在前期缺少 UUID 时，先以 socket 风格 API 搭独立骨架，再补协议接入细节。
- [ ] 独立 demo 与后续平台模组共用同一层 `mod_ble_open/send/recv/close` 边界，可以减少迁移成本。
- [ ] 双机 BLE 联调排障顺序应固定为：先确认 BlueZ 低层可发现目标，再确认 `ServicesResolved`，再确认 write/notify characteristic 路径，最后核对 notify 启动时序和首包收发日志。
- [ ] host 与 sensor terminal 的协议帧当前已对齐为：`L` 包含 `C/PSEQ/FSEQ/PROT/DATA`，`CS` 也包含 `PROT`；host 若使用 `control=0x43`，其控制域拆解为 `DIR=0 PRM=1 FCB=0 FCV=0 FUN=3`，从机会按确认/链路测试路径只回 ACK，不会返回业务 payload。

## 已处理经验

- [x] 已回写全局：否
- [x] 已判定不回写：否
