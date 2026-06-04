# 📡 OTA 协议规范

> 适用项目:STM32F405 + W5500 + MQTT
> 文档版本:V3.0 / 2026-06-04
> 协议版本:1.0

---

## 📑 目录

- [1. 概述](#1-概述)
- [2. MQTT 主题](#2-mqtt-主题)
- [3. 数据结构](#3-数据结构)
- [4. 命令与消息](#4-命令与消息)
- [5. 状态机](#5-状态机)
- [6. 超时与重试](#6-超时与重试)
- [7. 异常流程](#7-异常流程)
- [8. 完整时序](#8-完整时序)
- [9. 版本兼容](#9-版本兼容)

---

## 1. 概述

### 1.1 适用范围
- 设备:STM32 + W5500,运行 MQTT 客户端
- 服务器:任意 MQTT Broker + 自定义 OTA Server
- 传输层:MQTT 3.1.1 / 5.0

### 1.2 协议设计原则
- **单向推送为主**:服务器→设备(`ota/cmd`, `ota/data`)
- **设备主动上报状态**:`ota/status`(高频), `ota/ack`(低频), `ota/notify`(事件)
- **JSON 编码**:易于调试、跨平台、向后兼容
- **Base64 传输固件**:避开 MQTT payload 字节流问题

### 1.3 设备标识
- 设备 ID 格式:`w5500_XXX`,配置在 `Application/MQTT/inc/mqtt_config.h::OTA_DEVICE_ID`
- 主题前缀:`device/{device_id}/ota/`

---

## 2. MQTT 主题

| 方向 | 主题 | QoS | 用途 | 频率 |
|------|------|-----|------|------|
| S→D | `device/{id}/ota/cmd` | 0 | OTA 命令 | 低频(一次升级几条) |
| S→D | `device/{id}/ota/data` | 0 | 固件数据块 | 高频(几百到上千块) |
| D→S | `device/{id}/ota/status` | 0 | 状态/进度 | 中频(每 10% 一次 + 阶段切换) |
| D→S | `device/{id}/ota/ack` | 0 | 数据块确认 | 中频(每块一次) |
| **D→S** | **`device/{id}/ota/notify`** | **1** | **状态变更事件** | **极低频(成功/回滚)** |

**Qos 1 仅用于 `ota/notify`**:这是关键事件,不能丢。

---

## 3. 数据结构

### 3.1 OTA 阶段 (stage)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `OTA_STAGE_IDLE` | 空闲,无升级任务 |
| 1 | `OTA_STAGE_PREPARING` | 准备中(擦 Flash、解析) |
| 2 | `OTA_STAGE_DOWNLOADING` | 正在下载固件 |
| 3 | `OTA_STAGE_VERIFYING` | 正在 CRC32 校验 |
| 4 | `OTA_STAGE_INSTALLING` | 正在安装(写 ota_params) |
| 5 | `OTA_STAGE_SUCCESS` | 升级触发完成(设备准备重启) |
| 6 | `OTA_STAGE_FAILED` | 升级失败 |

### 3.2 错误码 (error)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `OTA_ERR_OK` | 成功 |
| 1 | `OTA_ERR_INVALID_PARAM` | 参数无效 |
| 2 | `OTA_ERR_NO_MEMORY` | 内存不足 |
| 3 | `OTA_ERR_FLASH_WRITE` | Flash 写入失败 |
| 4 | `OTA_ERR_FLASH_ERASE` | Flash 擦除失败 |
| 5 | `OTA_ERR_CRC_MISMATCH` | CRC32 校验失败 |
| 6 | `OTA_ERR_DOWNLOAD_FAILED` | 下载失败 |
| 7 | `OTA_ERR_INVALID_FIRMWARE` | 固件无效 |
| 8 | `OTA_ERR_ALREADY_LATEST` | 已是最新版本 |
| 9 | `OTA_ERR_NETWORK_ERROR` | 网络错误 |

### 3.3 通用状态消息 (`ota/status`)

```json
{
  "stage": 2,
  "progress": 50,
  "error": 0,
  "downloaded": 40960,
  "total": 81920,
  "version": "1.0.1"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| stage | int | 是 | 当前阶段(见 §3.1) |
| progress | int | 是 | 进度 0-100 |
| error | int | 是 | 错误码(0=正常,见 §3.2) |
| downloaded | int | 是 | 已下载字节数 |
| total | int | 是 | 总字节数 |
| version | string | 是 | 当前固件版本号 |

---

## 4. 命令与消息

### 4.1 ota_start (S→D)

**主题**:`device/{id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_start",
  "version": "1.0.2",
  "size": 81920,
  "crc32": 2088014220,
  "chunks": 80,
  "chunk_size": 1024
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| cmd | string | 是 | 固定 `"ota_start"` |
| version | string | 是 | 新固件版本号,如 `"1.0.2"` |
| size | number | 是 | 固件总字节数 |
| crc32 | number | 是 | 整固件 CRC32(0 表示跳过校验) |
| chunks | number | 是 | 分包总数 |
| chunk_size | number | 否 | 单块字节数(默认 1024,可省) |

**设备端响应**:
- 进入 `stage=1 PREPARING`
- 校验:`ota_is_ready_for_new_upgrade()` 必须返回 1(否则拒绝)
- 校验版本号 > 当前版本
- 擦除 target 分区 Flash

### 4.2 ota_data (S→D)

**主题**:`device/{id}/ota/data`

**Payload**:
```json
{
  "index": 0,
  "total": 80,
  "size": 1024,
  "data": "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8w..."
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| index | number | 是 | 数据块索引(从 0 开始) |
| total | number | 是 | 总块数(冗余字段,用于校验) |
| size | number | 是 | 本块解码后实际字节数(可能 < chunk_size) |
| data | string | 是 | **Base64 编码的二进制数据** |

**Base64 编码说明**:
- 编码后长度约 `ceil(size/3)*4`
- 例如 1KB 数据 → ~1366 字符 Base64
- 设备端先 Base64 decode 再写 Flash

### 4.3 ota_ack (D→S)

**主题**:`device/{id}/ota/ack`

**Payload**:
```json
{
  "index": 5,
  "success": true
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| index | number | 是 | 数据块索引 |
| success | boolean | 是 | `true`=成功,`false`=失败(服务器重发当前块) |

### 4.4 ota_cancel (S→D)

**主题**:`device/{id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_cancel"
}
```

**设备端行为**:
- 停止接收后续块
- 重置 ota_status.stage = IDLE
- **不修改 ota_params**(避免破坏 PENDING 状态)
- 不重启

### 4.5 ota_status 查询 (S→D,可选)

**主题**:`device/{id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_status"
}
```

**设备端响应**:发一条 `ota/status` 消息(用当前状态)。

### 4.6 ota_rollback (S→D,保留)

**主题**:`device/{id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_rollback"
}
```

**当前实现**:服务器端预留,设备端暂未实现(可触发回滚到另一分区)。

### 4.7 ota_notify (D→S) ⭐ 关键事件

**主题**:`device/{id}/ota/notify`

**QoS 1**:保证服务器收到(状态变更事件不能丢)

**ota_success 事件**(新固件联网成功):
```json
{
  "event": "ota_success",
  "app": 1,
  "version": 102,
  "boot_count": 0,
  "active": 1
}
```

**ota_rollback 事件**(新固件运行失败):
```json
{
  "event": "ota_rollback",
  "app": 0,
  "version": 100,
  "boot_count": 0,
  "active": 0
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| event | string | `"ota_success"` 或 `"ota_rollback"` |
| app | int | 当前运行分区(0=App-A, 1=App-B) |
| version | int | 当前分区版本号 |
| boot_count | int | 累计启动确认次数(SUCCESS 后重置为 0) |
| active | int | params 中 active_app(下次启动跳哪个) |

**触发时机**(简化设计 V2.1+):
- App 检测到 `ota_flag == PENDING` 且联网成功 → 写 SUCCESS → 发 ota_success
- 检测到 `ota_flag == SUCCESS`(之前漏发) → 补发 ota_success
- 检测到 `ota_flag == ROLLBACK` → 发 ota_rollback

**去重**:每次启动只发一次(`g_ota_success_notified` 标志)。

---

## 5. 状态机

### 5.1 设备端状态机

```
                  ota_start 命令
                        │
                        ▼
   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
   │   IDLE   ├─►│PREPARING ├─►│DOWNLOAD  ├─►│VERIFYING │
   │          │   │ stage=1  │   │ stage=2  │   │ stage=3  │
   └────▲─────┘   └──────────┘   └────┬─────┘   └────┬─────┘
        │                              │              │
        │                              │              ▼
        │                              │        ┌──────────┐
        │                              │        │INSTALLING│
        │                              │        │ stage=4  │
        │                              │        └────┬─────┘
        │                              │             │
        │                              │             ▼
        │                              │        ┌──────────┐
        │                              │        │ SUCCESS  │  ──► NVIC_SystemReset
        │                              │        │ stage=5  │
        │                              │        └──────────┘
        │                              │
        │       ota_cancel              │
        └──────────────────────────────┘
        ▲
        │ 任何阶段出错
        │
   ┌──────────┐
   │  FAILED  │ stage=6 + error_code
   └──────────┘
```

### 5.2 服务器端状态机

```
IDLE
  │  用户点"开始升级"
  ▼
START_SENT                                ← 发 ota_start, 等 ota/status stage=1
  │
  ▼
DOWNLOADING                               ← 逐块发, 等 ota/ack
  │  ACK_TIMEOUT(2s) 超时 → 重发当前块
  │  retry >= 5 → 终止(FAILED)
  │  收 stage=3 → 等 stage=4
  │
  ▼
VERIFYING                                 ← 等 stage=4
  │
  ▼
INSTALLING                                ← 启动 1 分钟定时器等 ota/notify
  │  收 ota/notify(ota_success) → SUCCESS
  │  收 ota/notify(ota_rollback) → ROLLBACK
  │  1 分钟内没收到 → 用户弹窗"等待超时"
  │
  ▼
SUCCESS / ROLLBACK
  │
  ▼  重置 ota_state = IDLE
IDLE
```

### 5.3 ota_flag 状态机(跨重启)

```
                ┌─────────┐
   升级触发 ──► │PENDING  │ ── 联网成功 ──► SUCCESS ── 下次升级 ──► PENDING
                │         │                  ▲
                │         │                  │
                │  5次启动失败                  │ App 写 SUCCESS
                │  Bootloader 兜底 ────────────┘
                │         │
                │         │  异常(保留)
                │         ▼
                │     ROLLBACK ──► 切回旧分区
                │
                ▼
             (无)
```

**关键规则**:
- 只有 `NORMAL` / `SUCCESS` 可以开新 OTA
- `PENDING` 状态拒绝新 OTA(避免破坏正在验证的新固件)

---

## 6. 超时与重试

### 6.1 服务器端超时(`SERVER/ota_server.py`)

| 参数 | 值 | 触发 | 处理 |
|------|---|------|------|
| `ACK_TIMEOUT_MS` | **2000 ms** | 发送 chunk 后 N 毫秒无 ACK | `current_chunk_retries++`,重发 |
| `MAX_RETRY_COUNT` | **5** | 同块重发 N 次仍失败 | 终止整个升级,弹窗"失败" |
| `COMMAND_TIMEOUT_MS` | 10000 ms | ota_start 等命令级 | (当前未严格使用) |
| `TOTAL_UPGRADE_TIMEOUT_MS` | 5 分钟 | 整个升级流程 | 终止,弹窗"升级超时" |
| `VERIFY_TIMEOUT_MS` | 1 分钟 | 收完最后一块后等 ota/notify | 弹窗"等待新固件上线超时" |

### 6.2 设备端超时

| 位置 | 超时 | 行为 |
|------|------|------|
| `ota_download_firmware`(旧版已拆分) | Flash 擦除 | 短 delay + 喂狗 |
| `ota_verify_firmware` | CRC 计算(无超时) | 计算耗时长,但 Flash 读不阻塞 |
| `mqtt_client_loop` | 100ms(非阻塞) | 不阻塞其他任务 |
| `osDelay(2000)` | 升级完成 → 重启前 | 给 MQTT 发最后 status |

### 6.3 设备端重试

**设备端不主动重发数据块**。重发机制全在服务器:
- 服务器 ACK 超时 → 重发
- 服务器收 `success:false` → 重发

设备端只负责:
- 写 Flash 失败 → 发 `success:false`,等服务器重发
- 块大小不匹配 → 丢弃,等服务器重发

---

## 7. 异常流程

### 7.1 设备端异常

#### Flash 写入失败
```
收 chunk N → 写 Flash 失败
  → 发 ack{index:N, success:false}
  → 不发 ota/status
  → 等待服务器重发
  → 5 次后服务器终止
```

#### CRC32 校验失败
```
收完最后一块
  → flash_calc_crc32(target, size)
  → 与 g_firmware_info.crc32 比对
  → 不一致:发 ota/status {stage:6, error:5}
  → 一致(简化模式:警告但继续)
```

#### JSON 解析失败
```
收 ota/cmd 但 JSON 无效
  → 发 ota/status {stage:6, error:1}
  → 不修改任何状态
```

#### PENDING 状态收到新 ota_start
```
ota_is_ready_for_new_upgrade() → 0
  → 忽略 ota_start
  → 不发任何消息
  → 服务器 UI 显示"设备忙"
```

### 7.2 服务器端异常

#### 设备 N 秒无 ACK
```
send_next_chunk() 后启动 2s 定时器
  → 超时:on_ack_timeout
  → current_chunk_retries++
  → 若 < 5:重发当前块(sent_chunks 不变)
  → 若 >= 5:_handle_max_retries_reached → 终止
```

#### 设备发 success:false
```
handle_ota_ack(index, success=false)
  → 视为 ACK 超时,走相同重试逻辑
```

#### 设备发 ota/notify(ota_rollback)
```
_handle_ota_notify_rollback
  → 弹窗"已回滚到旧版本"
  → 重置 ota_state = IDLE
```

#### 1 分钟内没收到 ota/notify
```
on_verify_timeout
  → 弹窗"等待新固件上线超时"
  → 提示:新固件可能启动失败 / 无法联网 / 已断电
```

#### 整个升级超过 5 分钟
```
on_upgrade_timeout
  → 弹窗"升级超时"
  → 发 ota_cancel 给设备
  → 重置状态
```

### 7.3 QTimer 泄漏修复(2026-06-04)

**Bug**:`ack_timeout_timer` 只 `stop()` 不 `deleteLater()`,多次升级累积,旧 timer 仍触发 `on_ack_timeout`,导致"已经停止却还在发包"。

**修复**: `_clear_ack_timeout_timer()` 工具函数,所有 ack timer 释放都走它。

调用点:
- `handle_ota_ack` 收到 ACK
- `_handle_max_retries_reached` 终止升级
- `on_upgrade_timeout` 总超时
- `on_stop_upgrade_clicked` 用户停止
- `send_next_chunk` 新建前清旧

---

## 8. 完整时序

### 8.1 成功升级

```
Server                                Device(A)                          Bootloader                    Device(B)
  │                                      │                                   │                             │
  │ ─── ota/cmd(ota_start) ────────────► │                                   │                             │
  │                                      │ 解析+检查+擦 target               │                             │
  │ ◄── ota/status(stage=1) ───────────  │                                   │                             │
  │ ─── ota/data(chunk 0) ─────────────► │                                   │                             │
  │ ◄── ota/ack(0, success) ────────────  │                                   │                             │
  │ ─── ota/data(chunk 1) ─────────────► │                                   │                             │
  │ ◄── ota/ack(1, success) ────────────  │                                   │                             │
  │           ...                        │                                   │                             │
  │ ─── ota/data(chunk N) ─────────────► │                                   │                             │
  │ ◄── ota/ack(N, success) ────────────  │                                   │                             │
  │                                      │ CRC 校验                          │                             │
  │ ◄── ota/status(stage=3) ───────────  │                                   │                             │
  │ ◄── ota/status(stage=4) ───────────  │ 写 ota_params                     │                             │
  │ ◄── ota/status(stage=5) ───────────  │                                   │                             │
  │                                      │ osDelay(2000)                     │                             │
  │                                      │ NVIC_SystemReset()                │                             │
  │                                      │                                   │                             │
  │                                      │                                   │ 启动                        │
  │                                      │                                   │ 读 ota_params               │
  │                                      │                                   │ ota_flag=PENDING            │
  │                                      │                                   │ active_app=APP_B            │
  │                                      │                                   │ boot_count++ (=1)           │
  │                                      │                                   │ 跳到 0x08084000             │
  │                                      │                                   │ ──────────────────────────►│
  │                                      │                                   │                             │ 启动
  │                                      │                                   │                             │ ota_fix_vtor: VTOR=0x08084000
  │                                      │                                   │                             │ ota_client_init
  │                                      │                                   │                             │ MQTT 连接
  │                                      │                                   │                             │ ota_confirm_boot_success
  │                                      │                                   │                             │   ota_flag: PENDING → SUCCESS
  │ ◄── ota/notify(ota_success) ────────│                                   │                             │   ota_notify_boot_success
  │                                      │                                   │                             │
  │  [弹窗"升级成功"]                     │                                   │                             │
```

### 8.2 失败升级(单块 5 次重试)

```
Server                                Device
  │                                      │
  │ ─── ota/data(chunk 5) ─────────────► │
  │ ◄── ota/ack(5, success) ────────────  │   ← 写成功
  │ ─── ota/data(chunk 6) ─────────────► │
  │                                      │   ← 2 秒无 ACK
  │   [on_ack_timeout: retries=1/5]      │
  │ ─── ota/data(chunk 6) ─────────────► │   ← 重发
  │                                      │   ← 2 秒无 ACK
  │   [on_ack_timeout: retries=2/5]      │
  │           ...                        │
  │   [on_ack_timeout: retries=5/5]      │
  │   [_handle_max_retries_reached]      │
  │   [弹窗"升级失败"]                    │
  │ ─── ota/cmd(ota_cancel) ───────────► │
  │                                      │   设备重置 ota_status.stage=IDLE
```

### 8.3 B→A 升级(VTOR 修复前会死机)

修复前时序(会死机):
```
Server                          Device(B, VTOR=0x0800C000 错误)
  │                                    │
  │ ─── ota/cmd(ota_start target=A) ─► │
  │                                    │ 擦 App-A (sector 3, 包含 0x0800C000-0x0800FFFF)
  │                                    │   擦到一半,SysTick 触发
  │                                    │   跳到 0x0800C000 取中断入口
  │                                    │   读到 0xFFFFFFFF
  │                                    │   跳过去 → HardFault → while(1)
  │                                    │   10s 后 IWDG 复位
  │                                    │   Bootloader 读 ota_params,跳 App-A
  │                                    │   启动成功(因为 sector 3 擦完了又写入了新固件)
  │                                    │   但过程中:Bootloader 也会读 RCC->CSR 看不清
```

修复后(ota_fix_vtor):
```
Server                          Device(B, VTOR=0x08084000 正确)
  │                                    │
  │ ─── ota/cmd(ota_start target=A) ─► │
  │                                    │ 擦 App-A sector 3
  │                                    │   SysTick 触发
  │                                    │   跳到 0x08084000 取中断入口(自己分区的)
  │                                    │   正常处理中断
  │                                    │ CRC → 写 ota_params → 重启
  │ ◄── ota/notify(ota_success) ──────│   跳到 App-A,联网,确认
```

---

## 9. 版本兼容

### 9.1 协议版本
- 当前协议版本:**1.0**
- 后续升级保持向后兼容

### 9.2 字段兼容性规则
- **新增字段**应是**可选**,旧版本忽略
- 不删除已有字段
- 字段名不变(避免破坏解析)

### 9.3 主题兼容性
- 主题命名规则不变
- 设备 ID 可变(`OTA_DEVICE_ID` 宏)

### 9.4 QoS 兼容性
- 当前只有 `ota/notify` 用 QoS 1
- 其余用 QoS 0(性能优先)
- 服务器必须支持 QoS 1 订阅

---

## 📚 相关文档

- 架构设计: [`OTA_Architecture.md`](OTA_Architecture.md)
- 1 页速查: [`OTA_Quick_Reference.md`](OTA_Quick_Reference.md)
- 构建/烧录: [`OTA_Integration_Guide.md`](OTA_Integration_Guide.md)
