# OTA 升级架构与流程文档

> 适用项目：STM32F405RGT6 + W5500 + FreeRTOS + MQTT
> 文档版本：V2.0
> 更新日期：2026-06-03

---

## 1. 设计目标

工业级 OTA 升级必须满足：

1. **可靠性**：升级失败时**自动回滚**到旧版本
2. **稳定性**：新固件需要经过**多次重启验证**才认定升级成功
3. **网络容错**：服务器、网络故障不影响升级流程
4. **状态可见**：服务器能**实时知道**升级状态
5. **幂等性**：断电、异常重启后可继续升级

我们采用**三阶段 A/B 升级 + 服务器状态机**方案。

---

## 2. Flash 分区布局

STM32F405RGT6 总共 1MB Flash，分区如下：

```
0x08000000 ┌─────────────────┐
           │  Bootloader     │ 48KB   (0x0000C000)
0x0800C000 ├─────────────────┤
           │  App-A (旧/主)  │ 480KB  (0x00078000)
0x08084000 ├─────────────────┤
           │  App-B (新/备)  │ 480KB  (0x00078000)
0x080FC000 ├─────────────────┤
           │  OTA Params     │ 16KB   (0x00004000)
0x08100000 └─────────────────┘
```

**重要约束**：
- App-A 和 App-B 大小完全相同，可互换
- OTA Params 必须独立于 App 区（不可被覆盖）
---

## 3. OTA 状态机

### 3.1 OTA Flag（OTA 升级标志）

定义在 `ota_params_t.ota_flag`：

| 值 | 名称 | 含义 |
|----|------|------|
| 0 | OTA_FLAG_NORMAL | 正常运行，无升级进行中 |
| 1 | OTA_FLAG_PENDING | 升级待验证（新固件已下载并切换，待稳定性验证）|
| 2 | OTA_FLAG_SUCCESS | 升级成功（已通过稳定性验证）|
| 3 | OTA_FLAG_ROLLBACK | 升级失败，需要回滚 |

### 3.2 状态转换图

```
                         ┌──────────────┐
                         │   NORMAL     │ ◄──────────────────┐
                         │  (正常运行)  │                    │
                         └──────┬───────┘                    │
                                │ 设备下载完成                │
                                │ 设置 ota_flag=PENDING      │
                                │ active_app=target          │
                                ▼                            │
                         ┌──────────────┐                    │
                         │   PENDING    │                    │
                         │ (新固件验证) │                    │
                         └──────┬───────┘                    │
                                │                            │
                  boot_count    │                            │
                  >= max?       │                            │
                  ┌─────────────┴────────┐                   │
                  │ No                  │ Yes               │
                  ▼                     ▼                    │
           继续验证             bootloader 判定               │
           boot_count++         升级成功                     │
                                 │                            │
                                 ▼                            │
                         ┌──────────────┐                    │
                         │   SUCCESS    │                    │
                         │  (升级成功)   │ ───────────────►──┘
                         └──────────────┘  app 通知服务器
                                ▲
                                │ (若新固件崩溃自动重启
                                │  bootloader 会保持 PENDING)
                                │
                         ┌──────────────┐
                         │   ROLLBACK   │ ◄─── 新固件连续失败
                         │  (回滚)      │      时触发
                         └──────┬───────┘
                                │ 切换到旧分区
                                ▼
                            (回到 NORMAL)
```

### 3.3 状态转换触发者

| 转换 | 触发者 | 代码位置 |
|------|--------|----------|
| NORMAL → PENDING | 旧固件 OTA 下载完成后 | `ota_client.c::ota_trigger_upgrade()` |
| PENDING → PENDING+1 | Bootloader（每次启动 PENDING 时）| `bootloader/main.c::bootloader_handle_ota()` |
| PENDING → SUCCESS | Bootloader（boot_count >= max）| `bootloader/main.c::bootloader_handle_ota()` |
| PENDING → ROLLBACK | 用户/服务器命令 | （通过 ota_rollback 命令预留）|
| SUCCESS → NORMAL | App 启动检测后 | （下次新 OTA 开始时）|
| ROLLBACK → NORMAL | Bootloader 回滚后 | `bootloader/main.c::bootloader_handle_ota()` |

---

## 4. 三阶段 A/B 升级流程

### 4.1 阶段 1：旧固件下载新固件

**位置**：运行在 App-A（旧固件），新固件下载到 App-B（备用分区）

**流程**：

```
1. 设备启动 → MQTT 连接 → 订阅 ota/cmd
2. 收到服务器命令: {"cmd":"ota_start","version":"1.0.1","url":"...","size":...,"crc32":...}
3. 校验版本号（必须 > 当前版本）
4. 校验目标分区可用（检查 ota_flag != PENDING）
5. 擦除目标分区 (App-B) Flash
   - 期间调用 mqtt_client_loop() 保持 MQTT 连接
   - 调用 HAL_IWDG_Refresh() 防止看门狗复位
6. 接收分包数据 (Base64 编码)
   - 每包写入 Flash 后发送 ACK
   - 每 10% 上报一次进度到 ota/status
7. 全部接收完成后 CRC32 校验
8. 设置 ota_flag=PENDING, active_app=B, boot_count=0
9. 触发系统重启
```

**关键代码**：
- `ota_client.c::ota_parse_command()` 入口
- `ota_client.c::ota_start_upgrade()` 初始化
- `ota_client.c::ota_download_firmware()` 擦除
- `ota_client.c::ota_write_firmware_chunk()` 写入
- `ota_client.c::ota_verify_firmware()` CRC32 校验
- `ota_client.c::ota_trigger_upgrade()` 设置 PENDING + 重启

### 4.2 阶段 2：新固件启动确认（核心）

**位置**：Bootloader 启动 → 跳转到新固件 → App 联网成功

**目的**：验证新固件能稳定运行 + 能联网

**完整流程**：

```
┌─────────────────────────────────────────────┐
│  设备上电/重启                                │
└─────────────────┬───────────────────────────┘
                  ▼
┌─────────────────────────────────────────────┐
│  Bootloader 启动                              │
│  1. 读取 OTA Params                          │
│  2. 看到 ota_flag == PENDING                 │
│  3. boot_count++  ← 唯一递增点               │
│  4. 写入 Flash                                │
│  5. 跳转到目标分区 (active_app)               │
└─────────────────┬───────────────────────────┘
                  ▼
┌─────────────────────────────────────────────┐
│  新固件 App 启动                              │
│  1. W5500 初始化 → DHCP → MQTT 连接         │
│  2. 订阅所有 OTA 主题                        │
│  3. 调用 ota_confirm_boot_success()          │
│     - 读取 params, 看到 ota_flag==PENDING   │
│     - 检测 boot_count: 1/N, 2/N, ...         │
│     - 打印 "正在验证 N/N" 日志                │
│     - 【不再修改 boot_count, 避免双重递增】  │
└─────────────────┬───────────────────────────┘
                  ▼
        设备正常运行, 等待重启或断电
                  │
                  ▼
              (下次启动)
                  │
                  ▼
        重复 Bootloader 流程, boot_count++
```

**简化设计 (V2.1, 2026-06-03)**：

按用户最新要求 "只要第一次成功 就可以认为是成功了 不需要5次"，**联网成功 = 升级成功**。
Bootloader 的 5 次验证改为**安全网**（仅在 app 永远没机会写 SUCCESS 时兜底）。

```
┌─────────────────────────────────────────────┐
│  新固件 App 启动（首次）                       │
│  1. MQTT 连接成功                             │
│  2. 调用 ota_confirm_boot_success()          │
│  3. 读取 params, 看到 ota_flag==PENDING      │
│  4. 主动写 ota_flag = SUCCESS 到 Flash        │
│  5. 设置 app_x_valid=1, app_y_valid=0        │
│  6. boot_count = 0 (重置)                     │
│  7. 调用 ota_notify_boot_success()           │
│  8. 发布 "ota_success" 到 ota/notify         │
│  9. 服务器收到, 标记设备"升级成功"            │
└─────────────────────────────────────────────┘

(无需等待 5 次启动, 联网成功即视为稳定)
```

**Bootloader 安全网**（仅当 app 失效时）：

```
┌─────────────────────────────────────────────┐
│  Bootloader 启动 (兜底)                       │
│  1. 看到 ota_flag == PENDING  (app 写失败)  │
│  2. boot_count++                              │
│  3. 如果 boot_count >= max_boot_count (5)   │
│  4. 设置 ota_flag = SUCCESS (兜底)           │
│  5. 跳转到新固件                              │
│  6. 新固件下次启动时, 检测到 SUCCESS,         │
│     补发 ota_success 通知给服务器             │
└─────────────────────────────────────────────┘
```

### 4.3 阶段 3：服务器确认

**位置**：服务器端

**职责**：
- 收到 `ota/notify` 的 `ota_success` 事件
- 将设备状态从 "upgrading" 改为 "stable"
- 不再向此设备推送升级指令（直到有更新版本）

**注意**：服务器**不能**仅凭下载完成判断升级成功，必须等到 `ota_success` 通知。

---

## 5. Boot Count 安全网机制

### 5.1 设计决策 (V2.1)

**主路径**：App 在第一次联网成功后立即写 SUCCESS（用户体验最好）。

**安全网**：如果 App 永远没机会写 SUCCESS（比如新固件有 bug 卡在联网前），
Bootloader 会在第 5 次启动时兜底写 SUCCESS，确保系统不会卡在 PENDING 永远不出通知。

**简化前后的对比**：

| 维度 | V1.2 (5次验证) | V2.1 (简化) |
|------|----------------|-------------|
| 升级确认延迟 | ~5次启动 (可能数天) | 1次启动 (立即) |
| 误判率 | 低 (新固件崩溃会被发现) | 略高 (新固件可能运行后崩溃) |
| 适合场景 | 关键设备 (医疗/工业控制) | 普通物联网设备 |
| 服务器反馈延迟 | 几小时-几天 | 几秒-几分钟 |

### 5.2 双重递增 Bug 修复 (V1.2 → V2.0)

**Bug 现象**（修复前）：
```
ota_confirm_boot_success: boot confirmed (count=4/5)
```
但实际只启动了 2 次（bootloader 2 + app 2 = 4）。

**修复**：app 不再递增 boot_count，只读不写。

### 5.3 简化设计 (V2.1)

**V2.1 改动**：app 在检测到 PENDING 时**主动写 SUCCESS 到 Flash**，
然后立即通知服务器。bootloader 的 5 次验证作为安全网保留。

**为什么 app 写 SUCCESS 更合理**：
- 联网成功 = 设备"活着"的强信号
- 缩短服务器等待时间（用户体验）
- 如果新固件运行后崩溃，用户可手动 `ota_rollback`

**boot_count 仍由 bootloader 维护**：
- 递增点在 bootloader（在 ota_flag == PENDING 时启动时 +1）
- app 写 SUCCESS 时 reset 为 0

**为什么由 bootloader 递增**：
- 简单：bootloader 启动时 +1，写入 Flash
- 可靠：即使 app 崩溃也会计数（说明启动稳定性）
- app 侧的"联网成功"通过 `ota/notify` 事件体现

### 5.4 max_boot_count 取值（安全网）

默认 **5 次**（仅在 app 永远写不了 SUCCESS 时生效）：
- 覆盖典型崩溃场景：NMI、HardFault、Watchdog Reset
- 兜底机制：app 失效 5 次启动后，bootloader 仍能写 SUCCESS
- 可通过 `OTA_MAX_BOOT_COUNT` 修改

---

## 6. 与服务器的交互协议

### 6.1 MQTT 主题定义

| 方向 | 主题 | QoS | 用途 |
|------|------|-----|------|
| Server→Device | `device/{id}/ota/cmd` | 0 | OTA 命令 (ota_start/ota_cancel/ota_status/ota_rollback) |
| Server→Device | `device/{id}/ota/data` | 0 | 固件分包数据 (Base64) |
| Device→Server | `device/{id}/ota/status` | 0 | 状态/进度 (高频，每 10%) |
| Device→Server | `device/{id}/ota/ack` | 0 | 数据块确认 (低频，每块) |
| **Device→Server** | **`device/{id}/ota/notify`** | **1** | **状态变更事件 (新增：升级成功/回滚)** |

### 6.2 ota/notify 事件格式

新固件启动确认后，发布到 `ota/notify`：

**升级成功事件**：
```json
{
  "event": "ota_success",
  "app": 1,
  "version": 101,
  "boot_count": 0,
  "active": 1
}
```

**回滚事件**：
```json
{
  "event": "ota_rollback",
  "app": 0,
  "version": 100,
  "boot_count": 0,
  "active": 0
}
```

字段说明：

| 字段 | 类型 | 说明 |
|------|------|------|
| event | string | 事件类型：`ota_success` 或 `ota_rollback` |
| app | int | 当前运行分区 (0=App-A, 1=App-B) |
| version | int | 当前分区固件版本号 |
| boot_count | int | 累计启动确认次数 (成功后被 bootloader 重置为 0) |
| active | int | 当前激活的分区 |

### 6.3 完整交互时序图

```
Server                              Device
  │                                    │
  │  ─── ota/cmd (ota_start) ────────► │
  │                                    │ [验证版本、检查 PENDING]
  │  ◄── ota/status (preparing) ──────│
  │                                    │ [擦除 Flash]
  │  ◄── ota/status (downloading) ────│
  │  ─── ota/data (chunk 0) ─────────► │
  │  ◄── ota/ack (index=0) ───────────│
  │  ─── ota/data (chunk 1) ─────────► │
  │  ◄── ota/ack (index=1) ───────────│
  │       ... (重复) ...               │
  │  ─── ota/data (chunk N) ─────────► │
  │  ◄── ota/ack (index=N) ───────────│
  │                                    │ [CRC32 校验]
  │  ◄── ota/status (verifying) ──────│
  │  ◄── ota/status (installing) ─────│
  │  ◄── ota/status (success) ────────│
  │                                    │ [设置 PENDING, 重启]
  │                                    │
  │                          [Bootloader 启动]
  │                                    │ boot_count++
  │                          [新固件 App 启动]
  │  ◄── ota/notify (pending) ────────│ [可选, 若需要详细跟踪]
  │  ─── ota/cmd (心跳/无命令) ────────►│
  │                                    │ [MQTT 连接]
  │                          [下次启动: boot_count++]
  │                          [再次启动: ...]
  │                          [boot_count >= 5]
  │                          [Bootloader: ota_flag=SUCCESS]
  │                          [新固件 App 启动]
  │  ◄── ota/notify (ota_success) ────│ ★ 关键通知
  │                                    │
  │  [服务器标记设备为"已升级"]         │
```

---

## 7. 异常处理

### 7.1 下载阶段异常

| 异常 | 处理 |
|------|------|
| MQTT 断连 | 通过 `mqtt_task.c` 状态机自动重连, 升级流程不中断 |
| 写 Flash 失败 | 立即返回错误, 上报 ota/status (error=FLASH_WRITE) |
| CRC32 校验失败 | 警告但继续（参数 OTA_CRC_STRICT 可改为严格模式）|
| 数据块丢失 | 客户端不发送下一个 ACK, 服务器重发 |
| 看门狗复位 | `flash_driver.c` 已在擦除循环中喂狗 |

### 7.2 升级阶段异常

| 异常 | 处理 |
|------|------|
| 新固件无法启动 | 5 次后仍 PENDING, 用户可手动 `ota_rollback` 或重新下载 |
| 新固件无法联网 | 启动计数继续 +1, 但 `ota/notify` 不会发送, 服务器可超时检测 |
| 升级中又收到 ota_start | **拒绝** (ota_is_ready_for_new_upgrade 返回 0) |
| 升级中又收到 ota_start (新设备例外) | INVALID_MAGIC 不算"未确认", 允许新 OTA |

### 7.3 关键防御：拒绝覆盖 PENDING 分区

**核心保护** (`ota_client.c::ota_is_ready_for_new_upgrade`)：
```c
if (params.ota_flag == OTA_FLAG_PENDING) {
    LOGW("previous upgrade still PENDING, refuse new OTA");
    return 0;  // 拒绝新 OTA
}
```

**原因**：如果允许覆盖 PENDING 的新固件，会导致**两个分区都损坏**（无法回滚）。

---

## 8. 关键 API 速查

### 8.1 app 侧 API（ota_client.h）

| 函数 | 作用 | 调用时机 |
|------|------|----------|
| `ota_client_init()` | 初始化 OTA 模块 | 启动时 |
| `ota_parse_command()` | 解析并执行 OTA 命令 | 收到 `ota/cmd` 消息 |
| `ota_receive_data_chunk()` | 接收并写入固件数据块 | 收到 `ota/data` 消息 |
| `ota_confirm_boot_success()` | 检测并处理 PENDING/SUCCESS 状态 | MQTT 连接成功 |
| `ota_notify_boot_success()` | 发布 ota_success/rollback 事件 | 内部由 confirm 调用 |
| `ota_is_ready_for_new_upgrade()` | 检查是否可以开新 OTA | ota_start 入口检查 |
| `ota_report_status()` | 上报当前状态 | 升级过程中 |

### 8.2 bootloader 侧 API

| 函数 | 作用 |
|------|------|
| `bootloader_main()` | bootloader 入口 |
| `bootloader_handle_ota()` | 处理 PENDING/SUCCESS/ROLLBACK 转换 |
| `bootloader_read_params()` | 从 Flash 读取 OTA 参数 |
| `bootloader_write_params()` | 写入 OTA 参数到 Flash |
| `bootloader_validate_firmware()` | 校验固件栈指针和复位向量 |
| `bootloader_jump_to_app()` | 跳转到应用程序 |

### 8.3 params API（ota_params.h）

| 函数 | 作用 |
|------|------|
| `ota_params_read()` | 读取 OTA 参数 (返回 INVALID_MAGIC 表示全新设备) |
| `ota_params_write()` | 写入 OTA 参数 |
| `ota_params_inc_boot_count()` | 递增启动计数 (由 bootloader 调用) |
| `ota_params_get_boot_count()` | 读取启动计数 |
| `ota_params_get_default()` | 获取默认参数 |

---

## 9. 调试指南

### 9.1 关键日志

| 日志 | 含义 |
|------|------|
| `NET_STATE_DHCP_RUN: DHCP timeout after 10 seconds` | DHCP 服务器未响应 |
| `OTA: refused - previous upgrade is still pending verification` | 上次升级未确认完成 |
| `ota_confirm_boot_success: PENDING (verifying, count=N/5)` | 新固件正在验证中 |
| `ota_confirm_boot_success: SUCCESS detected, will notify server` | 已达到 max_boot_count |
| `ota_notify_boot_success: payload = {...}` | 服务器通知已发出 |
| `[OTA] New firmware verified successfully!` | bootloader 判定升级成功 |

### 9.2 常见问题

**Q1：boot_count 增长太慢？**
- 检查是否禁用了 IWDG
- 检查是否有其他任务在阻塞 bootloader

**Q2：服务器收不到 ota/notify？**
- 检查 MQTT 主题订阅关系
- 检查 `g_ota_success_notified` 标志是否被错误清除
- 确认 QOS1 是否被支持

**Q3：升级后设备无法启动？**
- 烧写器读取新固件前 16 字节验证栈指针
- 检查是否覆盖了 params 区
- 检查是否覆盖了 App 自身

**Q4：旧设备无法升级？**
- 检查 `OTA_PARAMS_BACKUP_ADDR` 是否已废弃（已修复）
- 检查 `ota_is_ready_for_new_upgrade()` 是否误判

---

## 10. 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| V1.0 | 2026-06-02 | 初版（基础 A/B 切换） |
| V1.1 | 2026-06-03 | 修复 INVALID_MAGIC 误判 |
| V1.2 | 2026-06-03 | 修复 boot_count 双重递增 |
| V2.0 | 2026-06-03 | 加入服务器通知 ota/notify, 完整重写架构文档 |
| V2.1 | 2026-06-03 | 简化设计：联网成功=升级成功, bootloader 5次验证降为安全网 |

---

## 附录 A：关键代码引用

- OTA App: `Application/OTA/ota_client.c`
- OTA Params: `Application/OTA/ota_params.c`
- Bootloader: `Bootloader/Core/Src/main.c`
- 网络任务: `Application/MQTT/src/mqtt_task.c`
- 协议规范: `docs/OTA_Protocol_Specification.md`
