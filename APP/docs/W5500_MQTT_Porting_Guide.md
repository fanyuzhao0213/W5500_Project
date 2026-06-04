# 🔌 W5500 MQTT 框架移植指南

> 适用项目:STM32F405RGT6 + W5500 + MQTT + FreeRTOS + Keil MDK 5
> 文档版本:V3.0 / 2026-06-04

本文档讲解如何把 W5500 + MQTT 框架移植到 **其他 STM32 系列** 或 **其他 MCU 平台**。所有改动点都对应到本工程的实际源码,方便精准定位。

---

## 📑 目录

- [1. 移植总览](#1-移植总览)
- [2. 硬件接口适配](#2-硬件接口适配)
- [3. 时间源适配](#3-时间源适配)
- [4. TCP 客户端适配](#4-tcp-客户端适配)
- [5. RTOS 适配](#5-rtos-适配)
- [6. 日志系统适配](#6-日志系统适配)
- [7. 看门狗适配](#7-看门狗适配)
- [8. CLI 调试通道](#8-cli-调试通道)
- [9. 配置参数迁移](#9-配置参数迁移)
- [10. 移植验证清单](#10-移植验证清单)
- [11. 常见问题](#11-常见问题)
- [12. 关键文件清单](#12-关键文件清单)

---

## 1. 移植总览

### 1.1 需要修改的源码范围

本框架采用 **"应用层与硬件解耦"** 的设计。移植时,需要改的只是**适配层**和**配置宏**,业务逻辑层 (`mqtt_client.c`、`mqtt_exception.c`、`mqtt_task.c`、`ota_client.c` 等) 几乎不用动。

| 层次 | 文件 | 是否需要改 | 说明 |
|------|------|----------|------|
| 硬件 SPI 回调 | `Application/W5500/w5500_conf.c` | ✅ **必须改** | CS/RST 引脚、SPI 句柄 |
| TCP Socket | `Application/W5500/tcp_client.c` | ⚠️ 视情况 | 换 WIZCHIP 型号才改 |
| 时间源 | `Application/MQTT/src/mqtt_port.c` | ✅ **必须改** | `TimerInit/IsExpired/CountdownMS` |
| 日志输出 | `Application/RTT/LOG.h` | ⚠️ 视情况 | RTT/UART/关闭 |
| 看门狗 | `Application/MQTT/src/mqtt_exception.c` | ✅ **必须改** | IWDG 句柄、超时时间 |
| 网络配置 | `Application/W5500/netconf.c` + `mqtt_config.h` | ✅ **必须改** | IP/MAC/Broker |
| FreeRTOS 任务 | `Core/Src/main.c` | ✅ **必须改** | 任务优先级、栈大小 |
| ioLibrary | `Application/ioLibrary_Driver-master/` | ❌ 不动 | 官方驱动 |
| 业务逻辑 | `mqtt_client.c`、`ota_client.c` | ❌ 不动 | 与平台无关 |

### 1.2 移植步骤流程图

```
┌─────────────────┐
│ 1. 硬件接口适配 │  SPI CS/RST/HAL_SPI_xx
└────────┬────────┘
         ▼
┌─────────────────┐
│ 2. 时间源适配   │  HAL_GetTick / 硬件定时器
└────────┬────────┘
         ▼
┌─────────────────┐
│ 3. 任务/优先级  │  FreeRTOS 任务创建
└────────┬────────┘
         ▼
┌─────────────────┐
│ 4. 日志 + 看门狗│  LOG.h 配置 + IWDG
└────────┬────────┘
         ▼
┌─────────────────┐
│ 5. MQTT/网络参数│  mqtt_config.h
└────────┬────────┘
         ▼
┌─────────────────┐
│ 6. 编译+烧录验证│  逐项跑通清单
└─────────────────┘
```

### 1.3 支持的目标平台

| 平台 | 工作量 | 主要差异 |
|------|------|---------|
| **STM32F4 系列** (F407/F411/F415) | 🟢 极小 | HAL API 一致,改引脚即可 |
| **STM32F1 系列** | 🟢 小 | HAL 一致,Flash/RAM 略小,需调栈 |
| **STM32F7 / H7 系列** | 🟡 中 | MPU 配置、D-Cache 一致性 |
| **STM32H7 QSPI W6300** | 🟡 中 | `_WIZCHIP_` 切 W6300、`_WIZCHIP_QSPI_MODE_` |
| **STM32L4 (低功耗)** | 🟡 中 | 低功耗模式与 MQTT 长连接冲突,需权衡 |
| **其他 MCU (ESP32 / NXP)** | 🔴 大 | 需重写 SPI 驱动、RTOS 适配 |

---

## 2. 硬件接口适配

### 2.1 核心文件:`Application/W5500/w5500_conf.h`

**唯一需要改的硬件定义**:

```c
/* Application/W5500/w5500_conf.h */

/* W5500 CS GPIO */
#define W5500_CS_PORT  GPIOA      // ← 改:实际 CS 引脚的 GPIO 端口
#define W5500_CS_PIN   GPIO_PIN_4 // ← 改:实际 CS 引脚的 PIN

/* W5500 RST GPIO */
#define W5500_RST_PORT GPIOB      // ← 改:实际 RST 引脚
#define W5500_RST_PIN  GPIO_PIN_0 // ← 改:实际 RST 引脚
```

> 💡 这两个宏在 `w5500_conf.c` 里也重复定义了,需要**同步修改**。

### 2.2 核心文件:`Application/W5500/w5500_conf.c`

包含 5 个回调函数,**唯一需要改的是 SPI 句柄**:

```c
extern SPI_HandleTypeDef hspi1;  // ← 改:实际用到的 SPI 句柄
```

#### 2.2.1 修改 SPI 句柄

如果用 `SPI2` 或 `SPI3`,改这里:

```c
extern SPI_HandleTypeDef hspi2;  // SPI2 示例
```

#### 2.2.2 修改 GPIO 端口

```c
/* 与 .h 文件保持一致 */
#define W5500_CS_PORT  GPIOx
#define W5500_CS_PIN   GPIO_PIN_y
#define W5500_RST_PORT GPIOx
#define W5500_RST_PIN  GPIO_PIN_y
```

#### 2.2.3 可选优化:DMA 传输

当前是 polling 模式 `HAL_MAX_DELAY`。如需 DMA 加速,把 `w5500_spi_readburst` 替换为:

```c
static void w5500_spi_readburst(uint8_t* buf, uint16_t len)
{
    /* 使用 DMA,但必须保证 SPI 总线空闲,否则会卡 */
    HAL_SPI_Receive_DMA(&hspi1, buf, len);
    /* 简单场景建议用 polling,生产环境用 DMA + 信号量等待 */
}
```

### 2.3 CubeMX 中 SPI 配置要求

| 项 | 值 | 说明 |
|---|---|---|
| Mode | **Master** | 必须 |
| Direction | **2 Lines Full Duplex** | W5500 只支持全双工 |
| Data Size | **8 Bits** |  |
| Clock Polarity | **Low (CPOL=0)** | W5500 支持 Mode 0/3,本工程用 Mode 0 |
| Clock Phase | **1 Edge (CPHA=0)** | |
| Baud Rate | **≤ 33 MHz** | W5500 SPI 上限 33MHz,建议 10-20MHz |
| NSS | **Software** | 软件拉 CS |
| MSB First | **Enable** |  |

### 2.4 CubeMX 中 GPIO 配置要求

| 引脚 | 模式 | 上下拉 | 速度 | 备注 |
|------|------|-------|------|------|
| SCK/MISO/MOSI | Alternate Function (AF5) | No Pull | High | |
| CS | **Output Push-Pull** | No Pull | High | 软件拉 |
| RST | **Output Push-Pull** | No Pull | Low | 拉低 50ms 复位 |
| INT (可选) | External Interrupt | Pull-up |  | 不用可省 |

### 2.5 验证 SPI 是否通了

```c
/* 读 W5500 版本寄存器,应返回 0x04 */
uint8_t ver = getVERSIONR();
LOGI("W5500 version: 0x%02X", ver);  // 期望 0x04
```

如果返回值不是 0x04,检查:
1. SPI 模式是否对(CPOL=0, CPHA=0)
2. CS 引脚是否在传输前拉低、传完后拉高
3. SPI 时钟是否 ≤ 33MHz
4. 接线是否正确(SCK/MISO/MOSI 不要接反)

---

## 3. 时间源适配

### 3.1 核心文件:`Application/MQTT/src/mqtt_port.c`

本工程用 `HAL_GetTick()` (SysTick, 1ms tick) 作为唯一时间源,**不需要额外的硬件定时器**。

#### 3.1.1 移植到 STM32F1/F4/F7/H7(其他系列)

**完全不用改**!HAL 自带 `HAL_GetTick()`,靠 SysTick 1ms 中断递增。

```c
/* 当前 mqtt_port.c 实现 - 不用动 */
void TimerInit(Timer* timer) {
    timer->start_ms = HAL_GetTick();
    timer->timeout_ms = 0;
}

char TimerIsExpired(Timer* timer) {
    return (HAL_GetTick() - timer->start_ms) >= timer->timeout_ms;
}

void TimerCountdownMS(Timer* timer, unsigned int timeout_ms) {
    timer->start_ms = HAL_GetTick();
    timer->timeout_ms = timeout_ms;
}
```

#### 3.1.2 移植到非 HAL 平台

如果你的 RTOS 没有 SysTick,改用硬件定时器:

```c
extern volatile uint32_t g_tim2_ms;  // TIM2 中断里 g_tim2_ms++

void TimerInit(Timer* timer) {
    timer->start_ms = g_tim2_ms;
    timer->timeout_ms = 0;
}

char TimerIsExpired(Timer* timer) {
    return (g_tim2_ms - timer->start_ms) >= timer->timeout_ms;
}

void TimerCountdownMS(Timer* timer, unsigned int timeout_ms) {
    timer->start_ms = g_tim2_ms;
    timer->timeout_ms = timeout_ms;
}
```

#### 3.1.3 关键约束

| 约束 | 原因 |
|------|------|
| 时间源必须**单调递增** | 不能用 RTC(走时不准)、不能用 `millis()` 之类受中断影响 |
| 1ms 精度**足够** | MQTT PING 周期 60s,OTA chunk 2KB 几十 ms 传完 |
| 32-bit 计数**必要** | 防止 49.7 天溢出,本工程用 `uint32_t` 配合无符号减法自动回绕 |
| **不能用** `HAL_Delay()` | 内部就是 SysTick 阻塞,会卡 RTOS |

### 3.2 定时器任务

`Application/MQTT/src/timer_task.c` 也基于 `HAL_GetTick()`,**不用改**。

---

## 4. TCP 客户端适配

### 4.1 核心文件:`Application/W5500/tcp_client.c`

#### 4.1.1 移植到同系列 W5500(默认)

**不用改**。TCP Client 直接用 ioLibrary 的 `socket/connect/send/recv`,这些是 WIZCHIP 芯片的通用 API。

#### 4.1.2 移植到其他 WIZCHIP 型号(W5100/W5200/W6100/W6300)

ioLibrary 已内置支持。只需切换宏:

```c
/* Application/W5500/wizchip_conf.h */
#define _WIZCHIP_  W5500   // 改成 W6100 / W6300 / W5100S 等
```

并根据芯片定义 SPI 模式:

```c
/* W5500: Variable Length Data Mode */
#define _WIZCHIP_IO_MODE_   _WIZCHIP_IO_MODE_SPI_

/* W6100: 同样 SPI_VDM */
#define _WIZCHIP_IO_MODE_   _WIZCHIP_IO_MODE_SPI_VDM_

/* W6300: QSPI 模式 */
#define _WIZCHIP_QSPI_MODE_  QSPI_SINGLE_MODE  // 或 DUAL/QUAD
```

> ⚠️ `tcp_client.c` 中 `socket/connect/send/recv` 走的是 ioLibrary 通用 API,芯片切换后**基本不动**。

#### 4.1.3 Socket 编号分配

```c
/* tcp_client.c 中 */
#define TCP_CLIENT_SOCKET   0   /* MQTT 用 0 */
#define DNS_SOCKET          3   /* DNS 用 3(与 DHCP 共用) */
```

如果你的工程里用了其他 socket 占用 0 或 3,改这里:

```c
#define TCP_CLIENT_SOCKET   2   /* 改空闲的 */
#define DNS_SOCKET          5
```

> ⚠️ 4 个 socket 上限(W5100),本工程用 W5500 有 8 个,够用。

### 4.2 DNS 域名解析

如果用 IP 直连,把 `tcp_client.h` 改为:

```c
#define TCP_USE_DOMAIN     0   // 0=用 IP, 1=用域名
#define MQTT_BROKER_IP     "47.74.187.120"  // 填 IP
```

### 4.3 移植到非 WIZCHIP 以太网方案

| 方案 | 改动量 | 说明 |
|------|------|------|
| LAN8720A(ETH 外设) | 🔴 大 | 需重写 `tcp_client.c` 为 LWIP socket |
| ENC28J60(SPI) | 🔴 大 | 需移植 uIP 或 LWIP |
| ESP32(WiFi) | 🟡 中 | 用 ESP-IDF socket API 替换 |

> 💡 工业项目建议保留 W5500,改动最小,稳定可靠。

---

## 5. RTOS 适配

### 5.1 任务清单

本工程在 `mqtt_task_create()` (`Application/MQTT/src/mqtt_task.c`) 里创建这些任务:

| 任务 | 函数 | 优先级 | 栈大小 | 说明 |
|------|------|-------|-------|------|
| NetworkTask | `StartNetworkTask` | Normal | 2048 | W5500 init + MQTT 状态机 |
| MQTTTxtask | `StartMQTTTxTask` | **High** | 2048 | 发送队列消费 |
| MQTTRxTask | `StartMQTTRxTask` | **High** | 2048 | 接收队列消费 |
| TimerTask | `StartTimerTask` | BelowNormal | 512 | LED/周期发布 |
| ExceptionTask | `StartExceptionTask` | AboveNormal | 512 | PHY 监测 + 喂狗 |
| CLITask | `StartCLITask` | BelowNormal | 1024 | FreeRTOS+CLI |
| OTATask | `StartOTATask` | Normal | 2048 | OTA 客户端 |

### 5.2 移植到其他 FreeRTOS 版本

如果用裸 FreeRTOS 而非 CMSIS-RTOS,把 `osThreadCreate` 替换为 `xTaskCreate`:

```c
/* 旧 (CMSIS-RTOS) */
osThreadDef(networkTask, StartNetworkTask, NETWORK_TASK_PRIORITY, 0, NETWORK_TASK_STACK);
g_network_task_handle = osThreadCreate(osThread(networkTask), NULL);

/* 新 (裸 FreeRTOS) */
xTaskCreate(StartNetworkTask, "Net", NETWORK_TASK_STACK, NULL,
            NETWORK_TASK_PRIORITY, &g_network_task_handle);
```

队列、信号量、互斥锁 API 也对应替换:

| CMSIS-RTOS | FreeRTOS |
|-----------|----------|
| `osMessageCreate` | `xQueueCreate` |
| `osMessagePut` | `xQueueSend` |
| `osMessageGet` | `xQueueReceive` |
| `osSemaphoreCreate` | `xSemaphoreCreateBinary/Counting` |
| `osDelay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `osSignalSet` | `xTaskNotifyGive` |

### 5.3 移植到其他 RTOS(uCOS / RT-Thread / ThreadX)

替换策略:
- **任务 API**:5 个任务用 RTOS 原生 API 替代
- **队列 API**:`mqtt_queue.c` 整体重写
- **信号量**:`mqtt_exception.c` 里的 `osSignalSet` 替换
- **延时**:`osDelay` 替换为 `OSTimeDly` / `rt_thread_mdelay` / `tx_thread_sleep`

工作量大,建议在原 CMSIS-RTOS 之上做兼容层。

### 5.4 栈大小调整

不同 MCU 的 RAM 不同,栈大小可能要调:

| MCU RAM | NetworkTask 栈 | MQTT Tx/Rx 栈 | 建议 |
|---------|---------------|--------------|------|
| < 64KB | 1024 | 1024 | 风险高,需实测 |
| 64-128KB | 2048 (默认) | 2048 (默认) | ✅ |
| > 128KB | 4096 | 2048 | 富余 |

**验证方法**:开启 `configCHECK_FOR_STACK_OVERFLOW = 2`,让 FreeRTOS 主动检测溢出。

---

## 6. 日志系统适配

### 6.1 核心文件:`Application/RTT/LOG.h`

通过 `DEBUG_OUTPUT` 宏切换输出方式:

```c
/* Application/RTT/LOG.h */

#define DEBUG_UART4   1
#define DEBUG_RTT     0

#define DEBUG_OUTPUT  DEBUG_UART4  // ← 三选一
```

#### 6.1.1 方案 A:UART4(本工程默认)

```c
#define DEBUG_OUTPUT  DEBUG_UART4
```

需要:
1. CubeMX 启用 `UART4`,115200 8N1
2. 实现 `uart_printf()`(工程已实现,或用 `HAL_UART_Transmit` + vsnprintf)
3. PC 用 USB-TTL 接 UART4 引脚

#### 6.1.2 方案 B:SEGGER RTT(推荐调试用)

```c
#define DEBUG_OUTPUT  DEBUG_RTT
```

需要:
1. 添加 SEGGER RTT 库文件到工程(`SEGGER_RTT.c` + `SEGGER_RTT_printf.c`)
2. J-Link 仿真器接 SWD
3. PC 端用 J-Link RTT Viewer / Ozone 查看

#### 6.1.3 方案 C:关闭日志(节省资源)

```c
#define DEBUG_OUTPUT  -1   // 关闭
```

所有 `LOGI/LOGW/LOGE` 编译为空,体积减小 ~2KB。

### 6.2 切换到其他 UART

如果用 USART1 替代 UART4:

```c
/* LOG.h */
#elif DEBUG_OUTPUT == DEBUG_UART4
#include "usart.h"   // 改为 usart1.h
```

并在 `uart_printf()` 实现里换 UART 句柄。

### 6.3 切换到其他日志后端

可以仿造 RTT 模式新增,例如 SWO:

```c
#define DEBUG_SWO  2

#if DEBUG_OUTPUT == DEBUG_SWO
#include "swo.h"
#define LOGI(fmt, ...) SWO_Printf("I: " fmt "\r\n", ##__VA_ARGS__)
#endif
```

---

## 7. 看门狗适配

### 7.1 核心文件:`Application/MQTT/src/mqtt_exception.c`

`feed_watchdog_safely()` 中直接调用 HAL IWDG:

```c
extern IWDG_HandleTypeDef hiwdg;  // ← 改:你的 IWDG 句柄

static void feed_watchdog_safely(void) {
    if (mqtt_is_running() && g_exception_status.type == EXCEPTION_NONE) {
        if (HAL_GetTick() - g_last_watchdog_feed >= 1000) {
            HAL_IWDG_Refresh(&hiwdg);  // ← 改:你的 IWDG
            g_last_watchdog_feed = HAL_GetTick();
        }
    } else {
        if (HAL_GetTick() - g_last_watchdog_feed >= 2000) {
            HAL_IWDG_Refresh(&hiwdg);
            g_last_watchdog_feed = HAL_GetTick();
        }
    }
}
```

### 7.2 移植步骤

1. **CubeMX 启用 IWDG**:
   - Counter reload value: `0xFFF` (约 26 秒 @32kHz/256)
   - 建议 5-10 秒,异常恢复有足够时间

2. **MX_IWDG_Init() 必须在 main() 最早调用**:
   ```c
   int main(void) {
       HAL_Init();
       MX_IWDG_Init();  // 最早
       ota_fix_vtor();
       SystemClock_Config();
       // ...
   }
   ```

3. **替换句柄** (如果用 IWDG2 / WWDG):
   ```c
   extern IWDG_HandleTypeDef hiwdg1;  // CubeMX 生成的句柄名
   HAL_IWDG_Refresh(&hiwdg1);
   ```

### 7.3 喂狗策略说明

| 条件 | 喂狗间隔 | 原因 |
|------|---------|------|
| MQTT 正常 + 无异常 | **1 秒** | 系统健康,正常喂 |
| 异常恢复中 | **2 秒** | 给恢复留时间,但又不能太慢卡死 |

> ⚠️ **不要在异常时停止喂狗**!那样 IWDG 复位,异常上下文丢失,反而更难定位。

---

## 8. CLI 调试通道

### 8.1 核心文件:`Application/FreeRTOS_CLI/`

调试命令集合(默认已实现):
- `net` - 查看 IP/MAC/网关
- `mqtt` - 查看连接状态/发测试消息
- `mqtt-pub` - 主动发布
- `ota` - 查看 OTA 状态
- `reboot` - 软复位
- `iwdg` - 看门狗测试

### 8.2 移植到其他 MCU

`FreeRTOS_CLI.c/h` 是 FreeRTOS 官方模块,跨平台。

- **UART 输入**:`Application/FreeRTOS_CLI/src/` 下有 UART 接收实现
- **换 UART**:改 `huart1` → `huart2`(或你的句柄)
- **关闭 CLI**:在 `mqtt_task_create()` 里不调 CLI 任务创建即可,不影响主流程

---

## 9. 配置参数迁移

### 9.1 核心文件:`Application/MQTT/inc/mqtt_config.h`

#### 9.1.1 网络配置

```c
/* IP 获取方式:DHCP 或 静态 */
#define NET_CONFIG_MODE        NET_CONFIG_DHCP

/* 静态 IP(仅 NET_CONFIG_STATIC 时生效) */
#define STATIC_IP_ADDR         "192.168.1.88"
#define STATIC_SUBNET_MASK     "255.255.255.0"
#define STATIC_GATEWAY         "192.168.1.1"
#define STATIC_DNS             "8.8.8.8"

/* MAC 地址(全网唯一!) */
#define MAC_ADDR               "00:08:DC:12:34:56"  // ← 改
```

#### 9.1.2 MQTT Broker

```c
#define MQTT_BROKER_HOSTNAME    "your.broker.com"  // ← 改
#define MQTT_BROKER_PORT        1883               // ← 改
#define MQTT_CLIENT_ID          "your_device_001"  // ← 改
#define MQTT_USERNAME           "user"             // ← 改
#define MQTT_PASSWORD           "pass"             // ← 改
```

#### 9.1.3 MQTT 参数

```c
#define MQTT_KEEP_ALIVE        60   /* 秒,0=禁用 */
#define MQTT_CLEAN_SESSION     1    /* 1=不保留 session,0=保留 */
#define MQTT_COMMAND_TIMEOUT   5000 /* 命令超时,ms */
```

#### 9.1.4 缓冲区大小

```c
#define MQTT_SEND_BUF_SIZE     (4 * 1024)   /* 4KB - 配合 OTA 4KB chunk */
#define MQTT_READ_BUF_SIZE     (8 * 1024)   /* 8KB - 容纳整 chunk */
```

> ⚠️ 缓冲区**不能小于 OTA 块大小**!否则会拆包失败。

#### 9.1.5 OTA 配置

```c
#define OTA_DEVICE_ID          "w5500_001"  /* ← 改:每个设备唯一 */

#define OTA_FIRMWARE_MAX_SIZE  (480 * 1024)
#define OTA_DOWNLOAD_TIMEOUT   30000
#define OTA_MAX_RETRY_COUNT    3
#define OTA_PROGRESS_INTERVAL  10           /* 每 10% 上报 */
#define OTA_DEFAULT_CHUNK_SIZE (1 * 1024)   /* 默认 1KB */
#define OTA_MAX_CHUNK_SIZE     (4 * 1024)   /* 最大 4KB */
```

### 9.2 W5500 网络参数:`Application/W5500/netconf.c`

```c
static wiz_NetInfo_t g_net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 56},  // ← 改 MAC
    .ip  = {192, 168, 1, 88},                    // ← 改 IP
    .sn  = {255, 255, 255, 0},
    .gw  = {192, 168, 1, 1},
    .dns = {8, 8, 8, 8}
};
```

### 9.3 TCP Broker:`Application/W5500/tcp_client.h`

```c
#define TCP_USE_DOMAIN         1    /* 1=域名,0=IP */
#define MQTT_BROKER_DOMAIN     "your.broker.com"  // ← 改
#define MQTT_BROKER_PORT       1883                 // ← 改
```

---

## 10. 移植验证清单

### 10.1 硬件层验证

| # | 检查项 | 期望 | 命令/方法 |
|---|--------|------|----------|
| 1 | W5500 版本寄存器 | 返回 `0x04` | `getVERSIONR()` |
| 2 | W5500 复位 | RST 拉低 50ms 后拉高 | 示波器看 RST 引脚 |
| 3 | SPI 时钟 | 频率正确,空闲低电平 | 示波器看 SCK |
| 4 | CS 时序 | 传输前拉低,传完拉高 | 示波器看 CS |
| 5 | PHY Link 灯 | 网线插上后亮 | 看 W5500 LINK LED |

### 10.2 网络层验证

| # | 检查项 | 期望 | 日志关键字 |
|---|--------|------|-----------|
| 6 | PHY Link up | `PHY Link ON` | `PHY` |
| 7 | DHCP 获取 IP | `DHCP Success: IP=...` | `DHCP` |
| 8 | DNS 解析 | `DNS: Resolved ... -> IP` | `DNS` |
| 9 | TCP 连接 | `TCP: Connected!` | `TCP` |
| 10 | MQTT CONNECT | `MQTT Connected` | `MQTT` |
| 11 | 主题订阅 | 收到订阅 ACK | `MQTT Subscribe` |
| 12 | 周期性发布 | 看到 `stm32/uptime` 消息 | `MQTT Publish` |

### 10.3 应用层验证

| # | 检查项 | 期望 |
|---|--------|------|
| 13 | 看门狗喂狗 | 系统持续运行不重启 |
| 14 | PHY 断开恢复 | 拔网线 5s 后再插,自动重连 |
| 15 | MQTT 断连恢复 | Broker 端踢出,设备自动重连 |
| 16 | OTA 升级 | 服务器推固件,设备接收并重启 |
| 17 | 异常日志 | 故意断开网络,看到恢复日志 |

### 10.4 压力测试

| # | 测试 | 持续时间 | 通过标准 |
|---|------|---------|---------|
| 18 | 7x24 长连接 | 72 小时 | 不掉线,内存不泄漏 |
| 19 | 反复断电 | 100 次 | 每次都能恢复连接 |
| 20 | 高频发布 | 1 Hz × 1 小时 | 队列不溢出 |

---

## 11. 常见问题

### 11.1 SPI 不通

**症状**:`getVERSIONR()` 返回 `0x00` 或 `0xFF`

**排查**:
1. 检查 CS 引脚:空闲高,传输前拉低,传完拉高
2. 检查 SPI 模式:必须 Mode 0(CPOL=0, CPHA=0)
3. 检查接线:SCK/MISO/MOSI 不要接反
4. 降低 SPI 频率到 1MHz 试试
5. 检查 W5500 供电:3.3V 稳定,无纹波

### 11.2 DHCP 一直超时

**症状**:日志卡在 `DHCP: Sending DISCOVER...`

**排查**:
1. 网线是否插好,PHY 灯亮
2. 路由器是否开启 DHCP 服务
3. MAC 地址是否冲突(网内已有相同 MAC)
4. 路由器是否限制了 MAC 数量

### 11.3 DNS 解析失败

**症状**:`DNS: Failed to resolve ... after 3 attempts`

**排查**:
1. 域名是否正确,是否能 ping 通
2. DNS 服务器是否可达(路由器是否给了 DNS)
3. 改用 IP 模式绕过 DNS(`#define TCP_USE_DOMAIN 0`)

### 11.4 MQTT 连接被拒

**症状**:`MQTT Connect failed, return code: X`

**排查**:
| 错误码 | 含义 | 解决 |
|--------|------|------|
| 1 | 协议版本不对 | 改为 MQTT 3.1.1 |
| 2 | Client ID 冲突 | 改 `MQTT_CLIENT_ID` |
| 3 | 服务器不可用 | 检查 broker |
| 4 | 用户名/密码错 | 检查 `MQTT_USERNAME/PASSWORD` |
| 5 | 未授权 | 联系 broker 管理员 |

### 11.5 MQTT 频繁断连

**症状**:几秒/几分钟就断一次

**排查**:
1. Keep Alive 是否设置(本工程 60s)
2. 网络是否稳定(看 PHY 状态)
3. Broker 端是否踢出(查 broker 日志)
4. 检查 `mqtt_command_timeout` 是否过短

### 11.6 看门狗复位

**症状**:设备运行几秒/几十秒就重启

**排查**:
1. 检查 `feed_watchdog_safely()` 是否被调用(`ExceptionTask` 是否启动)
2. MQTT 是否能正常建立(没建立不会喂狗)
3. IWDG 超时是否过短(本工程 26 秒)

### 11.7 内存不足

**症状**:`pvPortMalloc failed` 或栈溢出

**排查**:
1. 调小任务栈:`configMINIMAL_STACK_SIZE`
2. 调小 MQTT 缓冲区:`MQTT_SEND_BUF_SIZE` / `MQTT_READ_BUF_SIZE`
3. 增加 `configTOTAL_HEAP_SIZE`
4. 开启栈溢出检测:`configCHECK_FOR_STACK_OVERFLOW = 2`

### 11.8 移植到 F1 系列后 SPI 异常

**原因**:F1 的 SPI 时钟分频与 F4 不同

**解决**:
1. CubeMX 重新配置 SPI,把 Prescaler 调大
2. 不用 DMA,F1 的 SPI DMA 容易出问题
3. polling 模式加超时

### 11.9 移植到 H7 系列后死机

**原因**:D-Cache 一致性问题,SPI 收的数据是缓存中的旧值

**解决**:
1. `SCB_InvalidateDCache_by_Addr()` 在每次 SPI 读后调用
2. 或者关闭 D-Cache:`SCB_DisableDCache()`
3. 写 Flash 前必须 `SCB_CleanDCache_by_Addr()`

### 11.10 移植后 OTA 升级失败

**原因 1**:`MQTT_READ_BUF_SIZE` 小于 OTA chunk

**解决**:确保 ≥ `OTA_MAX_CHUNK_SIZE` (4KB)

**原因 2**:`SCB->VTOR` 没修正

**解决**:在 `main()` 第一行加 `ota_fix_vtor()`

**原因 3**:`ota_flag` 状态机卡住

**解决**:看日志最后一行 `ota_flag` 状态

---

## 12. 关键文件清单

### 12.1 移植必改

| 文件 | 改什么 | 难度 |
|------|-------|------|
| `Application/W5500/w5500_conf.h` | CS/RST 引脚 | 🟢 1分钟 |
| `Application/W5500/w5500_conf.c` | SPI 句柄、GPIO 端口 | 🟢 5分钟 |
| `Application/MQTT/inc/mqtt_config.h` | MQTT Broker、IP、MAC | 🟢 5分钟 |
| `Application/W5500/netconf.c` | 默认 IP/MAC | 🟢 1分钟 |
| `Application/MQTT/src/mqtt_exception.c` | IWDG 句柄 | 🟢 1分钟 |
| `Application/RTT/LOG.h` | `DEBUG_OUTPUT` | 🟢 1分钟 |
| `Core/Src/main.c` | 任务创建、IWDG 初始化 | 🟡 10分钟 |

### 12.2 移植可能改

| 文件 | 改什么 |
|------|-------|
| `Application/W5500/wizchip_conf.h` | 切 WIZCHIP 型号 |
| `Application/W5500/tcp_client.h` | Broker 域名/IP |
| `Core/Src/freertos.c` | 任务优先级、栈 |

### 12.3 移植不改

| 文件 | 理由 |
|------|------|
| `Application/MQTT/src/mqtt_client.c` | Paho 移植层,跨平台 |
| `Application/MQTT/src/mqtt_port.c` | 用 HAL_GetTick,跨 STM32 |
| `Application/MQTT/src/mqtt_queue.c` | CMSIS-RTOS 包装 |
| `Application/MQTT/src/mqtt_task.c` | 与平台无关的状态机 |
| `Application/OTA/ota_*.c` | 业务逻辑 |
| `Application/ioLibrary_Driver-master/` | WIZnet 官方驱动 |

### 12.4 完整目录结构

```
APP/
├── Application/
│   ├── FreeRTOS_CLI/      ← 调试 CLI(可选)
│   ├── MQTT/
│   │   ├── inc/           ← 头文件
│   │   │   ├── mqtt_client.h
│   │   │   ├── mqtt_config.h     ★ 改这里
│   │   │   ├── mqtt_exception.h
│   │   │   ├── mqtt_port.h
│   │   │   ├── mqtt_queue.h
│   │   │   ├── mqtt_task.h
│   │   │   └── timer_task.h
│   │   ├── src/           ← 源文件
│   │   │   ├── mqtt_client.c
│   │   │   ├── mqtt_exception.c  ★ 改 IWDG 句柄
│   │   │   ├── mqtt_port.c
│   │   │   ├── mqtt_queue.c
│   │   │   ├── mqtt_task.c
│   │   │   └── timer_task.c
│   │   └── paho/          ← Paho 库
│   ├── OTA/               ← OTA 客户端(与平台无关)
│   ├── RTT/               ← 日志 ★ 改 DEBUG_OUTPUT
│   ├── W5500/             ← W5500 驱动 ★ 改引脚
│   │   ├── w5500_conf.h   ★
│   │   ├── w5500_conf.c   ★
│   │   ├── netconf.c      ★
│   │   ├── netconf.h
│   │   ├── tcp_client.c
│   │   ├── tcp_client.h   ★
│   │   ├── w5500.c/h      ← 官方驱动
│   │   ├── wizchip_conf.c/h
│   │   ├── socket.c/h
│   │   ├── Ethernet/      ← DHCP/DNS
│   │   └── Internet/
│   └── ioLibrary_Driver-master/  ← 官方库
├── Core/                  ← HAL/CMSIS
├── MDK-ARM/               ← Keil 工程
└── docs/                  ← 文档
```

---

## 📚 相关文档

- 框架总览: [`W5500_MQTT_Framework.md`](W5500_MQTT_Framework.md)
- OTA 架构: [`OTA_Architecture.md`](OTA_Architecture.md)
- OTA 集成: [`OTA_Integration_Guide.md`](OTA_Integration_Guide.md)
- 项目索引: [`Project_Index.md`](Project_Index.md)

---

## 💡 移植速查表

> 5 分钟移植清单:
>
> 1. 改 `w5500_conf.h/c` → CS/RST 引脚
> 2. 改 `mqtt_config.h` → Broker / IP / MAC
> 3. 改 `LOG.h` → `DEBUG_OUTPUT`
> 4. 改 `mqtt_exception.c` → `hiwdg` 句柄
> 5. 编译,看 SPI 能否读到 `0x04`
> 6. 看 DHCP 能否拿到 IP
> 7. 看 MQTT 能否 Connected
> 8. 完成 ✓
