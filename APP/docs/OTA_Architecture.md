# 🏗️ OTA 架构设计

> 适用项目:STM32F405RGT6 + W5500 + FreeRTOS + MQTT
> 文档版本:V3.0 / 2026-06-04
> 配套代码:`Application/OTA/` + `Bootloader/` + `SERVER/`

本架构按**角色**组织三端职责。读完后,任何一端改代码都能快速定位影响范围。

---

## 📑 目录

- [1. 总体设计](#1-总体设计)
- [2. 设备端 (App-A / App-B)](#2-设备端-app-a--app-b)
- [3. Bootloader 端](#3-bootloader-端)
- [4. 服务器端 (PyQt5)](#4-服务器端-pyqt5)
- [5. 三端协作完整时序](#5-三端协作完整时序)
- [6. 关键设计决策](#6-关键设计决策)
- [7. 异常处理全景](#7-异常处理全景)

---

## 1. 总体设计

### 1.1 设计目标

工业级 OTA 必须满足:

1. **可靠性** — 升级失败自动回滚到旧版本
2. **稳定性** — 新固件联网成功即认为"活着"
3. **网络容错** — 服务器/网络故障不破坏流程
4. **状态可见** — 服务器实时知道升级状态
5. **幂等性** — 断电/异常重启可继续
6. **可移植** — 同一份 App 代码,A/B 分区只差一个 `VECT_TAB_OFFSET`

我们采用 **三阶段 A/B 升级 + 状态机协调**:

```
阶段1:旧固件下载新固件到 target
   ↓ 设置 ota_flag=PENDING, active_app=target
   ↓ NVIC_SystemReset()
阶段2:Bootloader 跳到 target, 新固件启动
   ↓ 联网成功 → ota_confirm_boot_success()
   ↓ 写 ota_flag=SUCCESS, 发 ota/notify
阶段3:服务器收到 ota_success
   ↓ 标记设备"已升级", 停止推送
```

### 1.2 Flash 分区

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

- 详细定义:[`Application/OTA/ota_config.h`](../Application/OTA/ota_config.h)
- App-A 和 App-B 大小完全相同,可互换
- Params 区独立,绝不能被覆盖(已废弃的 `OTA_PARAMS_BACKUP_ADDR` 0x080F8000 与 App-B 末段重叠,会导致 App-B 损坏!)

### 1.3 三端职责一览

| 端 | 核心职责 | 不该做的事 |
|----|---------|----------|
| **设备端** (App-A/B) | 收块、写入、CRC 校验、设标志、重启、联网后确认 | 决定跳哪个分区(那是 bootloader 的事) |
| **Bootloader** | 读 active_app、跳到目标、维护 boot_count 安全网 | 写固件、跑 MQTT |
| **服务器端** | 推块、等 ACK、判超时重试、收 notify、显示状态 | 直接写 Flash / 跳分区 |

---

## 2. 设备端 (App-A / App-B)

> 代码位置:`Application/OTA/ota_client.c`(主逻辑) + `Application/OTA/ota_params.c` + `Application/OTA/flash_driver.c`

### 2.1 设备端状态机

```
              ota_start 命令
                    │
                    ▼
   ┌────────────────────────────────────────────┐
   │ IDLE (空闲)                                │ ◄──┐
   │ - 监听 ota/cmd                              │    │
   │ - ota_flag == NORMAL/SUCCESS                │    │
   └────────────────┬───────────────────────────┘    │
                    │ 收到 ota_start                │
                    │ ota_is_ready_for_new_upgrade │
                    ▼                                │
   ┌────────────────────────────────────────────┐    │
   │ PREPARING (准备)                            │    │
   │ - 解析 JSON                                  │    │
   │ - 检查版本号                                 │    │
   │ - flash_erase_range(target)                │    │
   └────────────────┬───────────────────────────┘    │
                    ▼                                │
   ┌────────────────────────────────────────────┐    │
   │ DOWNLOADING (下载)                          │    │
   │ - 收 ota/data                                │    │
   │ - Base64 解码                                │    │
   │ - flash_write(offset, data)                │    │
   │ - 发 ota/ack {index, success}              │    │
   │ - 每 10% 上报 ota/status                    │    │
   └────────────────┬───────────────────────────┘    │
                    ▼ 收完最后一块                  │
   ┌────────────────────────────────────────────┐    │
   │ VERIFYING (校验)                            │    │
   │ - flash_calc_crc32(target, size)           │    │
   │ - 与 g_firmware_info.crc32 比对            │    │
   └────────────────┬───────────────────────────┘    │
                    ▼ 校验通过                      │
   ┌────────────────────────────────────────────┐    │
   │ INSTALLING (安装)                           │    │
   │ - 写 ota_params:                            │    │
   │     ota_flag = PENDING                      │    │
   │     active_app = target                     │    │
   │     app_target_valid = 1                    │    │
   │     app_target_version/size/crc32 = ...    │    │
   │ - 报 stage=4 progress=98                   │    │
   └────────────────┬───────────────────────────┘    │
                    ▼                                │
   ┌────────────────────────────────────────────┐    │
   │ SUCCESS (升级触发完成)                       │    │
   │ - 报 stage=5 progress=100                  │    │
   │ - osDelay(2000) ← 让最后那条 status 出去   │    │
   │ - __disable_irq()                          │    │
   │ - NVIC_SystemReset()                        │    │
   └────────────────┬───────────────────────────┘    │
                    │ 复位                           │
                    ▼                                │
        [Bootloader 跳到 target]                       │
                                                      │
   ┌────────────────────────────────────────────┐    │
   │ [新固件首次启动]                              │    │
   │ - ota_fix_vtor() 修正 VTOR (重要!)          │    │
   │ - ota_client_init                              │    │
   │ - MQTT 连接成功                               │    │
   │ - ota_confirm_boot_success()                  │    │
   │     ota_flag: PENDING → SUCCESS              │    │
   │     app_new_valid = 1, app_old_valid = 0     │    │
   │ - ota_notify_boot_success()                   │    │
   │     发 ota/notify {event:ota_success, ...}   │ ───┘
   └────────────────────────────────────────────┘
```

### 2.2 设备端关键函数

| 函数 | 位置 | 作用 |
|------|------|------|
| `ota_client_init()` | `ota_client.c:79` | 初始化 OTA 状态,读 running 分区版本 |
| `ota_parse_command()` | `ota_client.c` | 解析 ota/cmd JSON,分派 ota_start/cancel/status/rollback |
| `ota_start_upgrade()` | `ota_client.c:161` | 入口:检查版本、检查 PENDING、初始化状态 |
| `ota_download_firmware()` | (拆分到 receive chunk) | 旧版有,新版通过 `ota_receive_data_chunk` 逐块处理 |
| `ota_write_firmware_chunk()` | `flash_driver.c` 包装 | 写一帧到 Flash |
| `ota_verify_firmware()` | `flash_driver.c` 包装 | CRC32 校验整个 target 分区 |
| **`ota_trigger_upgrade()`** | `ota_client.c:433` | **写 ota_params + 触发 NVIC_SystemReset** ⭐ |
| **`ota_confirm_boot_success()`** | `ota_client.c:642` | **PENDING→SUCCESS 状态机处理** ⭐ |
| `ota_notify_boot_success()` | `ota_client.c:553` | 发 `ota/notify` 给服务器 |
| `ota_is_ready_for_new_upgrade()` | `ota_client.c:744` | PENDING 状态拒绝新 OTA |
| `ota_send_ack()` | `ota_client.c` | 发 `ota/ack {index, success}` |
| `ota_report_status()` | `ota_client.c` | 发 `ota/status` |

### 2.3 设备端运行时段 VTOR 修复 ⭐⭐

**问题**:
- VECT_TAB_OFFSET 是编译期宏,Keil A/B 两个 build target 必须分别配(0x0000C000 / 0x00084000)
- 增量编译有时不重编 `system_stm32f4xx.c`,B 固件里 VTOR 字面量被错固化成 0x0800C000
- B→A 升级擦 sector 3 时,擦掉了 B 正在用的中断向量表 → SysTick 来 → 跳到 0xFFFFFFFF → HardFault → IWDG 复位

**修复**(`Core/Src/main.c`):

```c
/* main() 第一行, HAL_Init() 之后立即调用 */
ota_fix_vtor();

static void ota_fix_vtor(void) {
    uint32_t pc = (uint32_t)(void *)&ota_fix_vtor;  // 自己函数的 PC, 必在 LR_IROM1
    uint32_t vtor_base;
    if (pc >= APP_B_ADDR && pc < APP_B_ADDR + APP_B_SIZE) {
        vtor_base = APP_B_ADDR;
    } else {
        vtor_base = APP_A_ADDR;
    }
    __DSB(); SCB->VTOR = vtor_base; __DSB(); __ISB();
    LOGI("ota_fix_vtor: PC=0x%08lX -> VTOR=0x%08lX", pc, SCB->VTOR);
}
```

**效果**:
- A/B 用同一份 .o 链接都不会出错
- Keil 不再需要为每个 target 单独配 `VECT_TAB_OFFSET`
- 永远以"实际跑在哪个分区"为准,不依赖编译时配置

详细:[`OTA_Integration_Guide.md §VTOR 修复`](OTA_Integration_Guide.md)

### 2.4 设备端: 升级完成后的软重启

```c
// ota_trigger_upgrade() 末尾 (ota_client.c:511-529)
LOGI("OTA: Upgrade triggered! Will reboot in 2s to apply...");
osDelay(2000);                  // 给 MQTT 把最后一条 status 发出去
LOGI("OTA: Triggering NVIC_SystemReset() now...");
ota_report_status();
__disable_irq();
NVIC_SystemReset();
```

**为什么需要 osDelay(2000)**: 让 server 收到 `stage=5`,显示"升级成功"。否则 server 看到的最后状态是 `stage=4 INSTALLING`,要等 `ota/notify` 才确认成功。

**为什么 NVIC_SystemReset 而不是别的**:Bootloader 通过 RCC->CSR 复位标志判断是软件复位,会按正常流程读 ota_params 跳到 active_app。

### 2.5 设备端: running_app 判定修复

`get_running_app()` 用**函数自己的 PC 地址**判断当前在哪个分区(不是 static 变量地址,那是 RAM 不在 Flash)。

```c
static uint32_t get_running_app(void) {
    uint32_t fn_addr = (uint32_t)(void *)&get_running_app;
    if (fn_addr >= APP_A_ADDR && fn_addr < APP_A_ADDR + APP_A_SIZE) return APP_A;
    if (fn_addr >= APP_B_ADDR && fn_addr < APP_B_ADDR + APP_B_SIZE) return APP_B;
    return APP_A;  // 兜底
}
```

**为什么不能用 static 变量地址**:RAM 地址永远 > APP_B_ADDR,旧实现会永远返回 APP_A,导致 `app_b_valid=0, app_a_valid=1` 写反,bootloader 跳到无效 App-A 卡死。

---

## 3. Bootloader 端

> 代码位置:`Bootloader/Core/Src/main.c` + `Bootloader/MDK-ARM/Bootloader.uvprojx`

### 3.1 启动流程

```
上电 / NVIC_SystemReset
        │
        ▼
┌──────────────────────────────────────┐
│  Bootloader 启动                      │
│  1. 初始化 HAL(CMSIS HAL 简版)         │
│  2. 读 ota_params (地址 0x080FC000)   │
│  3. 校验 magic                        │
│  4. 看 ota_flag:                      │
│     - PENDING  → boot_count++, 写回    │ ★ 唯一递增点
│     - SUCCESS  → 跳 active_app        │
│     - NORMAL   → 跳 active_app        │
│     - ROLLBACK → 跳上一分区(保留)    │
│  5. 校验目标 App 栈指针/MSP            │
│  6. 设置 MSP, 跳到目标 Reset_Handler   │
└──────────────────────────────────────┘
        │
        ▼
   [App 启动]
```

### 3.2 boot_count 维护规则 ⭐

**唯一递增点**:`ota_flag == PENDING` 时,Bootloader 启动 +1。

**简化设计 (V2.1+)**:
- App 联网成功 → 主动写 `SUCCESS` → boot_count 重置为 0
- App 永远没机会写 SUCCESS(比如新固件有 bug) → Bootloader 在 boot_count ≥ `OTA_MAX_BOOT_COUNT(5)` 时兜底写 `SUCCESS`
- 这样做的好处:App 一联网 server 立即知道成功,用户体验最好;Bootloader 兜底避免"app 永远没机会写"导致系统卡在 PENDING

**boot_count 流转时序**:

```
旧固件写 PENDING + active=B + boot_count=0
        │  NVIC_SystemReset
        ▼
Bootloader 启动:读 ota_flag=PENDING → boot_count++  → 写回 (boot_count=1)
        │  跳到 App-B
        ▼
App-B 启动 + 联网成功
        │
        ├─ 路径 A (主路径): ota_confirm_boot_success() 写 SUCCESS + boot_count=0
        │                   发 ota_success,结束
        │
        └─ 路径 B (兜底):   App 卡死,连续重启 5 次
                            Bootloader:boot_count=5 → 写 SUCCESS(兜底)
                            跳到 App-B,App 联网成功
                            检测到 SUCCESS,补发 ota_success
```

**App 端 `ota_confirm_boot_success` 不再写 boot_count**(防双重递增 bug)。

### 3.3 跳转到 App 的实现

```c
typedef void (*pFunction)(void);

/* 读目标 App 起始地址的栈指针(Offset 0 = MSP 初值) */
uint32_t app_sp = *(__IO uint32_t *)app_address;
uint32_t app_reset = *(__IO uint32_t *)(app_address + 4);

/* 校验栈指针在合法 RAM 范围(0x20000000 ~ 0x20020000) */
if ((app_sp & 0x2FFE0000) == 0x20000000) {
    __disable_irq();
    __set_MSP(app_sp);             // 设置主栈指针
    ((pFunction)app_reset)();      // 跳到 Reset_Handler
}
```

**注意**:App 端 `ota_fix_vtor()` 会在 `main()` 第一时间把 VTOR 改成自己分区对应的值,所以 Bootloader 不需要改 VTOR。

---

## 4. 服务器端 (PyQt5)

> 代码位置:`SERVER/ota_server.py`

### 4.1 服务器职责

1. 加载固件(HEX 或 BIN)
2. 分块(Base64 编码,默认 1KB,可选 2/4KB)
3. 推送命令 `ota_start`
4. 逐块推送,等 `ota/ack`
5. 超时重试(单块最多 5 次)
6. 监控状态(`ota/status`)
7. 等待新固件联网确认(`ota/notify`)
8. 显示"升级成功"或"升级失败"

### 4.2 服务器状态机

```
IDLE
  │  用户点"开始升级"
  ▼
START_SENT                                  ← 发 ota_start
  │  收 stage=1 PREPARING
  ▼
DOWNLOADING                                ← 逐块发送
  │  收 stage=2 + 进度
  │  每块等 ACK (ACK_TIMEOUT=2s, RETRY=5)
  │  ACK 超时 → 重发当前块 (sent_chunks 不变)
  │  ACK 失败 → 重发当前块
  │  retry >= 5 → 终止
  ▼
  ├─ 收 stage=3 VERIFYING → 继续等
  ├─ 收 stage=4 INSTALLING → 启动 1 分钟定时器等 ota/notify
  │
  │  (1 分钟内收 ota/notify ota_success)
  ▼
SUCCESS                                     ← 弹窗"升级成功"
  │  重置 ota_state = IDLE
  ▼
IDLE
```

### 4.3 服务器关键配置

| 宏 | 值 | 位置 |
|---|---|---|
| `ACK_TIMEOUT_MS` | **2000** ms | [ota_server.py:81](../SERVER/ota_server.py) |
| `MAX_RETRY_COUNT` | **5** 次 | [ota_server.py:82](../SERVER/ota_server.py) |
| `COMMAND_TIMEOUT_MS` | 10000 ms | [ota_server.py:83](../SERVER/ota_server.py) |
| `TOTAL_UPGRADE_TIMEOUT_MS` | 5 分钟 | [ota_server.py:84](../SERVER/ota_server.py) |
| `VERIFY_TIMEOUT_MS` | 1 分钟 | [ota_server.py:85](../SERVER/ota_server.py) |
| `CHUNK_SIZE` | 1 KB(默认) | [ota_server.py:34](../SERVER/ota_server.py) |

### 4.4 服务器关键修复(QTimer 泄漏)

**Bug**: 旧代码 `ack_timeout_timer` 只 `stop()`,不 `deleteLater()` + 引用清空。多次升级累积 timer,旧 timer 仍触发 `on_ack_timeout`,导致"已经停止却还在发包"。

**修复**:`SERVER/ota_server.py:1249-1260`

```python
def _clear_ack_timeout_timer(self):
    timer = getattr(self, 'ack_timeout_timer', None)
    if timer is not None:
        try:
            timer.stop()
            timer.timeout.disconnect(self.on_ack_timeout)
            timer.deleteLater()
        except (RuntimeError, TypeError):
            pass
        self.ack_timeout_timer = None
```

**调用点**:`handle_ota_ack` / `_handle_max_retries_reached` / `on_upgrade_timeout` / `on_stop_upgrade_clicked` / `send_next_chunk`(新建前清旧的)。

### 4.5 服务器:验证 ota_success 的可靠信号

**重要**:不要相信 `ota/status` 的 `stage=5`。那是旧固件在 `osDelay(2000)` 之前发的,可能没发出去就重启了。

**可靠信号**:`ota/notify` 里的 `event: ota_success`。这是新固件真的活着 + 联网成功才发的。

服务器逻辑:
- 收到 `ota/notify` 的 `ota_success` → 弹窗"升级成功"(不是 `stage=5`!)
- 收到 `ota/notify` 的 `ota_rollback` → 弹窗"已回滚"
- `VERIFY_TIMEOUT_MS(1 分钟)` 内没收到 → 弹窗"等待新固件上线超时"

---

## 5. 三端协作完整时序

### 5.1 A→B 升级时序

```
Server                          Device(A)                  Bootloader               Device(B)
  │                                │                          │                       │
  │  ── ota/cmd(ota_start) ────►  │                           │                       │
  │                                │ 解析、检查、擦 target    │                       │
  │  ◄── ota/status(stage=1) ───  │                           │                       │
  │  ── ota/data(chunk 0) ──────► │                           │                       │
  │  ◄── ota/ack(0, success) ───  │                           │                       │
  │  ── ota/data(chunk 1) ──────► │                           │                       │
  │  ◄── ota/ack(1, success) ───  │                           │                       │
  │           ...                  │                           │                       │
  │  ── ota/data(chunk N) ──────► │                           │                       │
  │  ◄── ota/ack(N, success) ───  │                           │                       │
  │                                │ CRC 校验                  │                       │
  │  ◄── ota/status(stage=3) ───  │                           │                       │
  │  ◄── ota/status(stage=4) ───  │ 写 ota_params             │                       │
  │  ◄── ota/status(stage=5) ───  │                           │                       │
  │                                │ osDelay(2000)             │                       │
  │                                │ NVIC_SystemReset()        │                       │
  │                                │                           │                       │
  │                                │                           │ 启动                  │
  │                                │                           │ 读 ota_params         │
  │                                │                           │ ota_flag=PENDING      │
  │                                │                           │ active_app=APP_B      │
  │                                │                           │ boot_count++ (=1)     │
  │                                │                           │ 跳到 0x08084000       │
  │                                │                           │ ────────────────────►│
  │                                │                           │                       │ 启动
  │                                │                           │                       │ ota_fix_vtor: VTOR=0x08084000
  │                                │                           │                       │ ota_client_init
  │                                │                           │                       │ MQTT 连接
  │                                │                           │                       │ ota_confirm_boot_success
  │                                │                           │                       │   ota_flag: P→S
  │  ◄── ota/notify(ota_success) ─│                           │                       │   ota_notify_boot_success
  │                                │                           │                       │
  │  [标记"已升级"]                 │                           │                       │
```

### 5.2 B→A 升级时序(同 A→B,只是 active_app 翻转)

**关键差异**:B 运行时 active_app=APP_B,要升级到 A,target=APP_A。

**历史上的坑**:B 固件里 VTOR 错固化成 0x0800C000 → 擦 sector 3 时擦掉 B 自己的向量表 → HardFault。

**已修复**:`ota_fix_vtor()` 在 main() 第一时间把 VTOR 改成 PC 实际指向的分区。详见 [§2.3](#23-设备端运行时段-vtor-修复-)。

---

## 6. 关键设计决策

### 6.1 简化"5 次验证"为"联网即成功"

| 维度 | V1.x (5次) | V2.1+ (联网即成功) |
|------|-----------|------------------|
| 升级确认延迟 | 5 次启动(可能数天) | 1 次启动(立即) |
| 误判率 | 低(崩溃会被发现) | 略高(运行后崩溃要等用户反馈) |
| 服务器反馈 | 几小时-几天 | 几秒-几分钟 |
| 安全网 | N/A | Bootloader 5 次兜底写 SUCCESS |

**boot_count 维护唯一性**:
- 递增点只有 Bootloader(App 联网成功不再递增,只读)
- App 写 SUCCESS 时重置为 0

### 6.2 参数区单区存储(去掉备份)

**问题**:旧代码 `OTA_PARAMS_BACKUP_ADDR = 0x080F8000` 与 App-B 末段重叠,每次写参数都会擦 App-B 末段,导致 App-B 固件损坏。

**修复**:
- `OTA_PARAMS_BACKUP_ADDR` 宏保留但标记 DEPRECATED,值为空
- 单一存储 + 失败重试

### 6.3 块大小 1KB 默认

| 块大小 | 优点 | 缺点 |
|-------|------|------|
| 1 KB | 4G 网络下最稳定,丢包重传快 | 块数多,协议开销大 |
| 4 KB | 效率高 | 4G 弱网容易丢整包 |

当前默认 1KB,服务器 UI 可切 2/4KB,设备端通过 `g_chunk_size` 同步(`ota_start` 命令里的 `chunk_size` 字段)。

### 6.4 为什么 App 端 `ota_notify_boot_success` 不靠 `g_mqtt_running`

旧逻辑用 `g_mqtt_running` 判断"是否联网",有 race condition(MQTT 重连瞬间置 0)。

新逻辑:在 MQTT 任务的 `connected` 信号里调 `ota_confirm_boot_success`,确保 MQTT 真的连上,不会误判。

---

## 7. 异常处理全景

### 7.1 下载阶段

| 异常 | 处理位置 | 行为 |
|------|---------|------|
| MQTT 断连 | `mqtt_task.c` 状态机 | 自动重连,OTA 流程不中断 |
| 服务器 N 秒无 ACK | `SERVER/ota_server.py` | 重发当前块,最多 5 次 |
| 设备 Flash 写失败 | `ota_receive_data_chunk` | 发 `ack{success:false}`,server 重发 |
| 设备 CRC 校验失败 | `ota_verify_firmware` | 警告继续(可改严格模式),最终报 stage=6 |
| IWDG 超时 | `feed_watchdog_safely` | 1 秒/2 秒喂狗,不会触发 |
| 单块擦除耗时(sector 5 = 128KB, 4s+) | `flash_erase_range` | 每 sector 前 HAL_IWDG_Refresh |

### 7.2 升级触发阶段

| 异常 | 处理 |
|------|------|
| 写 ota_params 失败 | `ota_trigger_upgrade` 返回 `OTA_ERR_FLASH_WRITE`,流程不重启 |
| 校验写后读回 | 立即 `ota_params_read` 验证 |
| 重启前 2 秒看门狗 | MQTT 任务持续喂狗,不会超时 |

### 7.3 新固件启动阶段

| 异常 | 处理 |
|------|------|
| 新固件有 bug 启动崩溃 | Bootloader boot_count++,5 次后兜底写 SUCCESS |
| 新固件能启动但无法联网 | boot_count 继续 +1,但永远没 `ota/notify`,服务器 `VERIFY_TIMEOUT(1 分钟)` 后报警 |
| 新固件启动后网络分区变了 | MQTT 重连,自动恢复 |
| B→A 升级擦 sector 3 死机(VTOR 错) | **已修复**:`ota_fix_vtor()` 运行时修正 |

### 7.4 关键防御:拒绝覆盖 PENDING 分区

```c
int ota_is_ready_for_new_upgrade(void) {
    ota_params_t params;
    ota_params_read(&params);
    if (params.ota_flag == OTA_FLAG_PENDING) return 0;  // 拒绝
    return 1;
}
```

**原因**:PENDING 说明上次升级的新固件还在"验证中",如果允许新 OTA 覆盖,会导致**两个分区都损坏**。

---

## 📚 相关文档

- 协议消息格式: [`OTA_Protocol_Specification.md`](OTA_Protocol_Specification.md)
- 构建/烧录步骤: [`OTA_Integration_Guide.md`](OTA_Integration_Guide.md)
- 1 页速查: [`OTA_Quick_Reference.md`](OTA_Quick_Reference.md)
- MQTT/网络框架: [`W5500_MQTT_Framework.md`](W5500_MQTT_Framework.md)
