# 📌 OTA 速查手册(1 页)

> 适用项目:STM32F405RGT6 + W5500 + MQTT
> 文档版本:V3.0 / 2026-06-04

---

## 1. Flash 分区(STM32F405 1MB)

```
0x08000000 ┌──────────────────────────────┐
           │  Bootloader      48KB        │ 0x0000C000
0x0800C000 ├──────────────────────────────┤
           │  App-A (主/旧)  480KB        │ 0x00078000
0x08084000 ├──────────────────────────────┤
           │  App-B (备/新)  480KB        │ 0x00078000
0x080FC000 ├──────────────────────────────┤
           │  OTA Params     16KB         │ 0x00004000
0x08100000 └──────────────────────────────┘
```

- 详细见 `Application/OTA/ota_config.h`
- A 和 B 完全相同,大小可互换
- **Params 区独立,不能被覆盖**

---

## 2. OTA Flag 状态机

```
                    ┌─────────────┐
        ┌──────────►│   NORMAL    │◄───────────────┐
        │           │  (正常运行)  │                │
        │           └──────┬──────┘                │
        │     ota_trigger │                        │
        │     _upgrade()  │                        │
        │     写 PENDING  │                        │
        │     active=target                        │
        │                  ▼                       │
        │           ┌─────────────┐                │
        │           │   PENDING   │                │
        │           │ (新固件验证) │                │
        │           └──────┬──────┘                │
        │  ┌───────────────┼──────────────┐        │
        │  联网成功        │  5次启动失败  │  ROLLBACK
        │  (app 写        │  (bootloader │  触发
        │   SUCCESS)      │   兜底)      │  (保留位)
        │  ▼              ▼              ▼        │
        │  ┌──────────────────────────┐          │
        │  │       SUCCESS            │──────────┘
        │  │  (升级成功, 可开新 OTA)  │
        │  └──────────────────────────┘
        │  ▲
        └──┘  下次 ota_trigger_upgrade 进入新流程
```

| Flag 值 | 名称 | 含义 |
|---------|------|------|
| 0 | `OTA_FLAG_NORMAL` | 正常运行,无升级进行中 |
| 1 | `OTA_FLAG_PENDING` | 新固件已下载,待验证 |
| 2 | `OTA_FLAG_SUCCESS` | 升级已成功,可开新 OTA |
| 3 | `OTA_FLAG_ROLLBACK` | 升级失败,需回滚(保留) |

详细:[ota_config.h](../Application/OTA/ota_config.h)

---

## 3. ota_params_t 关键字段

| 字段 | 类型 | 谁写 | 谁读 | 用途 |
|------|------|------|------|------|
| `magic_number` | u32 | params API | params API | 0x4F544153("OTAS") 标识合法参数区 |
| **`ota_flag`** | u32 | app/bootloader | bootloader/app | **状态机主标志** |
| **`active_app`** | u32 | `ota_trigger_upgrade` | bootloader | **下次启动跳哪个分区**(0=A, 1=B) |
| **`boot_count`** | u32 | bootloader(`+=1`) | bootloader/app | 稳定性验证计数,5 次兜底 |
| `app_a_version` | u32 | `ota_trigger_upgrade` | `ota_client_init` | App-A 版本号 |
| `app_b_version` | u32 | `ota_trigger_upgrade` | `ota_client_init` | App-B 版本号 |
| `app_a_crc32` | u32 | `ota_trigger_upgrade` | 调试 | App-A CRC32 |
| `app_b_crc32` | u32 | `ota_trigger_upgrade` | 调试 | App-B CRC32 |
| `app_a_valid` | u8 | `ota_trigger_upgrade` / `ota_confirm_boot_success` | bootloader | App-A 是否可启动(0=可被擦) |
| `app_b_valid` | u8 | `ota_trigger_upgrade` / `ota_confirm_boot_success` | bootloader | App-B 是否可启动(0=可被擦) |

**核心业务规则**:
- `app_a_valid` 和 `app_b_valid` 永远**只有一个是 0**(可被作为下次 OTA 的 target 覆盖)
- 开新 OTA 前调 `ota_is_ready_for_new_upgrade()`:`ota_flag` 必须是 NORMAL 或 SUCCESS(拒绝 PENDING)

---

## 4. RAM 中运行时标志(本启动有效)

| 标志 | 文件 | 作用 |
|------|------|------|
| `g_target_app` | `ota_client.c:31` | 当前 OTA 目标分区,默认 `APP_B` |
| `g_boot_confirmed` | `ota_client.c:34` | 本次启动已调过 `ota_confirm_boot_success` |
| `g_ota_success_notified` | `ota_client.c:35` | 本次启动已发过 `ota_success` 通知 |
| `g_ota_status.stage` | `ota_client.c:28` | 阶段 IDLE/PREPARING/DOWNLOADING/VERIFYING/INSTALLING/SUCCESS/FAILED |
| `g_ota_status.progress` | `ota_client.c:28` | 0-100 进度 |

---

## 5. MQTT 主题一览

设备 ID 默认 `w5500_001`(改 `mqtt_config.h` 的 `OTA_DEVICE_ID`)。

| 方向 | 主题 | QoS | 用途 |
|------|------|-----|------|
| S→D | `device/{id}/ota/cmd` | 0 | 命令(ota_start/ota_cancel/ota_status/ota_rollback) |
| S→D | `device/{id}/ota/data` | 0 | 固件数据块(Base64) |
| D→S | `device/{id}/ota/status` | 0 | 阶段/进度/错误(高频,每 10%) |
| D→S | `device/{id}/ota/ack` | 0 | 单块接收 ACK |
| **D→S** | **`device/{id}/ota/notify`** | **1** | **状态变更事件(关键通知)** |

**`ota/notify` JSON 格式**:
```json
{
  "event": "ota_success",        // 或 "ota_rollback"
  "app": 1,                       // 0=A, 1=B (当前运行分区)
  "version": 101,
  "boot_count": 0,
  "active": 1                     // 0=A, 1=B (params 中的 active_app)
}
```

---

## 6. 超时与重试配置(在服务器端 `SERVER/ota_server.py`)

| 参数 | 值 | 含义 |
|------|---|------|
| `ACK_TIMEOUT_MS` | **2000** ms | 单块等 ACK 超时,触发重发 |
| `MAX_RETRY_COUNT` | **5** 次 | 同块最多重发 5 次,失败则终止整个升级 |
| `COMMAND_TIMEOUT_MS` | 10000 ms | 命令级超时(ota_start 等) |
| `TOTAL_UPGRADE_TIMEOUT_MS` | 5 分钟 | 整个升级流程超时 |
| `VERIFY_TIMEOUT_MS` | 1 分钟 | 等待新固件联网确认(发完 firmware 等 ota/notify) |

设备端块大小:`OTA_DEFAULT_CHUNK_SIZE = 1 KB`,`OTA_MAX_CHUNK_SIZE = 4 KB`。

---

## 7. 升级成功标准(简化设计 V2.1+)

**主路径**:App 联网成功 → `ota_confirm_boot_success()` → 写 `SUCCESS` → 发 `ota_success`。

**安全网**:App 永远没机会写 SUCCESS(如新固件有 bug 卡在联网前)→ Bootloader 在 boot_count ≥ 5 时兜底写 SUCCESS。

---

## 8. 常见故障 30 秒定位

| 现象 | 直接原因 | 修复 |
|------|---------|------|
| 升级到一半不重启 | `ota_trigger_upgrade` 提前 return | 看日志最后一行 stage 是什么 |
| **B→A 升级擦 sector 3 死机** | **VTOR 错固化成 0x0800C000** | **[OTA_Integration_Guide.md §VTOR 修复](OTA_Integration_Guide.md)** |
| `ota_success` 收不到 | 1. ota_flag 不是 SUCCESS  2. `g_ota_success_notified` 已置 1  3. 主题订阅错 | 看服务器收到的 status 阶段 |
| 升级时设备掉电 | Bootloader 重启后读 PENDING → boot_count++ → 5 次后兜底 SUCCESS | 正常行为,等 boot_count 满 |
| 块大小和服务器不一致 | 服务器从 `cb_chunk_size` 选,设备从 `g_chunk_size` 算 offset | 服务器选 1KB |

---

## 9. 关键文件清单

| 文件 | 作用 |
|------|------|
| `Application/OTA/ota_config.h` | Flash 分区、Flag 值、魔数 |
| `Application/OTA/ota_params.h/c` | 参数区读写 API |
| `Application/OTA/flash_driver.h/c` | Flash 擦写 + CRC32 |
| `Application/OTA/ota_client.h/c` | OTA 客户端主逻辑 |
| `Core/Src/main.c` | 启动 + `ota_fix_vtor()` 运行时修正 VTOR |
| `Core/Src/system_stm32f4xx.c` | `SystemInit`(VTOR 兜底) |
| `Bootloader/Core/Src/main.c` | Bootloader 主逻辑 |
| `MDK-ARM/w5500_project.uvprojx` | Keil 工程(含 A/B 两个 build target) |
| `Application/MQTT/inc/mqtt_config.h` | MQTT Broker / OTA 主题 / 块大小 |
| `SERVER/ota_server.py` | PyQt5 服务器(超时/重试/状态机) |
