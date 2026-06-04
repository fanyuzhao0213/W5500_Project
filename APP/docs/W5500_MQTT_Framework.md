# 🌐 W5500 + MQTT 框架说明

> 适用项目:STM32F405RGT6 + W5500 + FreeRTOS
> 文档版本:V3.0 / 2026-06-04

本框架实现了一个工业级 MQTT 客户端:网络状态机自动恢复、多任务收发分离、分层异常处理。

---

## 📑 目录

- [1. 系统架构](#1-系统架构)
- [2. 目录结构](#2-目录结构)
- [3. 任务分层](#3-任务分层)
- [4. MQTT 模块详解](#4-mqtt-模块详解)
- [5. W5500 驱动](#5-w5500-驱动)
- [6. 数据流](#6-数据流)
- [7. 配置参数](#7-配置参数)
- [8. 日志与看门狗](#8-日志与看门狗)
- [9. 设计原则](#9-设计原则)

---

## 1. 系统架构

### 1.1 整体架构

```
┌────────────────────────────────────────────────────────────────────┐
│                          应用层 (Application)                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │
│  │  TimerTask   │  │  CLI Task    │  │ OTA Task     │  用户业务     │
│  │  (定时发布)   │  │  (命令行)    │  │ (高优先级)   │              │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘              │
│         │                 │                 │                       │
├─────────┼─────────────────┼─────────────────┼───────────────────────┤
│         │                 │                 │   MQTT 层              │
│         ▼                 ▼                 ▼                       │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │                    MQTT 任务管理                             │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────────┐     │   │
│  │  │ NetTask      │  │ MQTTTxtask   │  │ MQTTRxTask     │     │   │
│  │  │ (状态机,高)  │  │ (发送队列,中)│  │ (接收队列,中)  │     │   │
│  │  │              │  │              │  │                │     │   │
│  │  │ INIT         │  │ 等待发送队列  │  │ 等待接收队列   │     │   │
│  │  │ PHY_CHECK    │  │ → publish    │  │ → 分发到命令队列│     │   │
│  │  │ DHCP_START   │  │              │  │                │     │   │
│  │  │ MQTT_CONNECT │  │              │  │                │     │   │
│  │  │ MQTT_SUB     │  │              │  │                │     │   │
│  │  │ RUNNING      │  │              │  │                │     │   │
│  │  │ ERROR (重试) │  │              │  │                │     │   │
│  │  └──────────────┘  └──────────────┘  └────────────────┘     │   │
│  │           │                                                 │   │
│  │           └─→ mqtt_exception 模块 (状态机 + 恢复策略)         │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              │                                     │
├──────────────────────────────┼─────────────────────────────────────┤
│                              │   MQTT 客户端层                       │
│                              ▼                                     │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │             MQTT 客户端封装 (mqtt_client)                   │   │
│  │  - mqtt_client_connect / subscribe / publish / loop        │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              │                                     │
│                              ▼   Paho MQTT-C (嵌入式版)              │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │  MQTTConnect / MQTTSubscribe / MQTTPublish / MQTTYield      │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              │                                     │
│                              ▼   适配层                             │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │             mqtt_port (Timer + Network)                     │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              │                                     │
├──────────────────────────────┼─────────────────────────────────────┤
│                              │   TCP/IP 层                          │
│                              ▼                                     │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │  tcp_client (基于 W5500 Socket)                              │   │
│  │  - tcp_client_connect / send / recv / disconnect            │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              │                                     │
│                              ▼   W5500 驱动层                       │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │  W5500 SPI 驱动                                              │   │
│  │  - socket() / connect() / send() / recv()                   │   │
│  │  - DHCP / DNS                                                │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              │                                     │
├──────────────────────────────┼─────────────────────────────────────┤
│                              │   硬件抽象层                          │
│                              ▼                                     │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │  STM32 HAL + FreeRTOS + IWDG + TIM2(1ms)                    │   │
│  └────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────┘
```

### 1.2 关键设计思想

- **分层任务**:网络状态机 / 发送 / 接收各一个任务,互不阻塞
- **队列解耦**:业务层 `mqtt_tx_queue_put` → 发送任务,接收任务 → `mqtt_rx_queue_put` / `ota_cmd_queue_put`
- **非阻塞 I/O**:所有网络操作都带超时,绝不 `while(socket wait)`
- **异常自愈**:PHY 链路 / DHCP / MQTT 三级检测,各自重试,不互相影响
- **静态分配**:禁止 `malloc`,所有 buffer/queue 编译期固定

---

## 2. 目录结构

```
APP/Application/
├── MQTT/                              # MQTT 协议层
│   ├── inc/                           # 头文件
│   │   ├── mqtt_client.h              # 客户端封装接口
│   │   ├── mqtt_config.h              # ★ Broker / OTA 主题 / 块大小配置
│   │   ├── mqtt_exception.h           # 异常类型 + 恢复策略
│   │   ├── mqtt_port.h                # 平台适配层 (Timer/Network)
│   │   ├── mqtt_queue.h               # 消息队列 API
│   │   ├── mqtt_task.h                # 任务管理
│   │   └── timer_task.h               # 定时任务
│   ├── src/                           # 实现
│   │   ├── mqtt_client.c              # 客户端封装
│   │   ├── mqtt_exception.c           # 异常处理(状态机 + 喂狗)
│   │   ├── mqtt_port.c                # 平台适配实现
│   │   ├── mqtt_queue.c               # 消息队列实现
│   │   ├── mqtt_task.c                # 任务入口
│   │   └── timer_task.c               # 定时任务
│   └── paho/                          # Paho MQTT-C 库(标准版)
│       ├── MQTTClient.c / .h
│       ├── MQTTPacket.c / .h
│       ├── MQTTConnect.c / .h
│       └── ...
│
├── W5500/                             # W5500 驱动 + TCP
│   ├── w5500.c / .h                   # 芯片驱动(SPI 读写、版本、PING)
│   ├── w5500_conf.c / .h              # SPI 回调注册
│   ├── wizchip_conf.c / .h            # WIZnet 通用接口
│   ├── socket.c / .h                  # Socket API
│   ├── tcp_client.c / .h              # TCP 客户端封装
│   ├── netconf.c / .h                 # 网络配置(DHCP/Static)
│   └── Internet/                      # 网络协议
│       ├── DHCP/                      # DHCP 客户端
│       └── DNS/                       # DNS 解析
│
├── OTA/                               # OTA 客户端
│   ├── ota_config.h                   # ★ Flash 分区 / OTA Flag 定义
│   ├── ota_params.h / .c              # 参数区 API
│   ├── flash_driver.h / .c            # Flash 擦写 + CRC32
│   ├── ota_client.h / .c              # ★ OTA 主逻辑
│   └── ota_example.c                  # 测试代码(可选)
│
├── FreeRTOS_CLI/                      # 命令行接口
├── RTT/                               # SEGGER RTT 日志
│
└── Core/                              # STM32 HAL 核心
    ├── Src/
    │   ├── main.c                     # ★ main() + ota_fix_vtor()
    │   ├── system_stm32f4xx.c         # SystemInit(VTOR 兜底)
    │   └── stm32f4xx_it.c             # 中断向量
    └── Inc/
```

---

## 3. 任务分层

### 3.1 任务清单

| 任务 | 优先级 | 栈大小 | 功能 | 位置 |
|------|-------|--------|------|------|
| `StartNetTask` | 高 (osPriorityHigh) | 512 | 网络状态机(NetTask) | `mqtt_task.c` |
| `StartMQTTTxTask` | 中 (osPriorityNormal) | 256 | 发送队列处理 | `mqtt_task.c` |
| `StartMQTTRxTask` | 中 (osPriorityNormal) | 256 | 接收队列处理 | `mqtt_task.c` |
| `StartTimerTask` | 低 (osPriorityLow) | 256 | 定时器调度 | `mqtt_task.c` |
| `StartCLITask` | 低 (osPriorityLow) | 256 | 命令行 | `cli_task.c` |
| `otaClientTask` | **高 (osPriorityHigh)** | 8192 | OTA 任务(8KB Base64 + 4KB Binary 局部变量) | `ota_client.c` |

### 3.2 网络状态机(`NetTask`)

```
┌──────────────┐
│   INIT       │ 初始化 W5500 (SPI 复位、版本检测)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  PHY_CHECK   │ 每 100ms 检测 PHY 链路
└──────┬───────┘
       │ 链路就绪
       ▼
┌──────────────┐
│  DHCP_START  │ (DHCP 模式)启动 DHCP
└──────┬───────┘
       │ DHCP 成功
       ▼
┌──────────────┐
│ MQTT_CONNECT │ MQTT CONNECT
└──────┬───────┘
       │ 连接成功
       ▼
┌──────────────┐
│ MQTT_SUB     │ 订阅 OTA 主题
└──────┬───────┘
       │ 订阅成功
       ▼
┌──────────────┐
│  RUNNING     │ 正常运行, mqtt_client_loop(100ms) + DHCP 租约
└──────┬───────┘
       │ 检测到异常
       ▼
┌──────────────┐
│   ERROR      │ ──→ mqtt_exception 决策恢复策略 ──→ 回到对应状态
└──────────────┘
```

### 3.3 任务间通信(消息队列)

| 队列 | 方向 | 长度 | 元素 |
|------|------|------|------|
| `mqtt_tx_queue` | 业务层 → TxTask | 10 | `mqtt_msg_t {topic, payload, qos}` |
| `mqtt_rx_queue` | RxTask → 业务层 | 10 | `mqtt_msg_t` (用户通用消息) |
| `ota_cmd_queue` | RxTask → OTATask | 5 | OTA 专用消息(ota/cmd, ota/data) |

**消息分派**(在 `mqtt_task.c::on_mqtt_message` 中):
- 主题包含 `/ota/cmd` → 进 `ota_cmd_queue`
- 主题包含 `/ota/data` → 进 `ota_cmd_queue`
- 其他 → 进 `mqtt_rx_queue`

---

## 4. MQTT 模块详解

### 4.1 mqtt_exception 异常处理

**异常类型**:
```c
typedef enum {
    EXCEPTION_NONE,                  // 正常
    EXCEPTION_PHY_LINK_DOWN,         // 网线断开
    EXCEPTION_DHCP_FAILED,           // DHCP 获取 IP 失败
    EXCEPTION_DHCP_TIMEOUT,          // DHCP 超时
    EXCEPTION_MQTT_DISCONNECTED,     // MQTT 断连
    EXCEPTION_MQTT_SUBSCRIBE_FAILED, // 订阅失败
    EXCEPTION_NETWORK_ERROR          // 其他网络错误
} exception_type_t;
```

**恢复策略**:
```c
typedef enum {
    RECOVERY_FULL_RESET,    // 完全复位 W5500
    RECOVERY_RESTART_DHCP,  // 重启 DHCP
    RECOVERY_RETRY_MQTT     // MQTT 重连
} recovery_strategy_t;
```

**恢复策略表**:

| 异常 | 默认恢复 | 退避 |
|------|---------|------|
| `EXCEPTION_PHY_LINK_DOWN` | `RECOVERY_FULL_RESET` | 5 秒后重试 |
| `EXCEPTION_DHCP_FAILED` | `RECOVERY_RESTART_DHCP` | 5 秒后重试 |
| `EXCEPTION_MQTT_DISCONNECTED` | `RECOVERY_RETRY_MQTT` | 3 秒后重试 |
| `EXCEPTION_MQTT_SUBSCRIBE_FAILED` | `RECOVERY_RETRY_MQTT` | 立即 |

### 4.2 mqtt_client 客户端封装

```c
// 初始化
void mqtt_client_init(void);

// 连接 Broker
int mqtt_client_connect(void);

// 订阅主题 + 注册回调
int mqtt_client_subscribe(const char* topicFilter, enum QoS qos, messageHandler handler);

// 发布消息
int mqtt_client_publish(const char* topicName, const char* payload, size_t len, enum QoS qos);

// 非阻塞循环(用于 keepalive)
int mqtt_client_loop_nonblocking(int timeout_ms);

// 阻塞循环
int mqtt_client_loop(int timeout_ms);

// 断开
int mqtt_client_disconnect(void);
```

**内部使用 Paho MQTT-C**:
- `MQTTClientInit` — 初始化客户端
- `MQTTConnect` — 发送 CONNECT 包
- `MQTTSubscribe` — 发送 SUBSCRIBE
- `MQTTPublish` — 发送 PUBLISH
- `MQTTYield` — 处理 PINGREQ/PINGRESP + 接收数据

### 4.3 mqtt_port 平台适配

Paho MQTT-C 库与平台无关,通过 `mqtt_port` 适配 STM32 + FreeRTOS:

**Timer 适配**(用 `HAL_GetTick()`):
```c
void TimerInit(Timer* timer) { timer->start_ms = HAL_GetTick(); }
char TimerIsExpired(Timer* timer) {
    return (HAL_GetTick() - timer->start_ms) >= timer->timeout_ms;
}
void TimerCountdownMS(Timer* timer, unsigned int ms) {
    timer->start_ms = HAL_GetTick();
    timer->timeout_ms = ms;
}
```

**Network 适配**(调用 W5500 TCP):
```c
static int mqtt_network_read(Network* n, unsigned char* buffer, int len, int timeout_ms) {
    // 调用 tcp_client_recv + W5500 socket recv
}

static int mqtt_network_write(Network* n, unsigned char* buffer, int len, int timeout_ms) {
    // 调用 tcp_client_send + W5500 socket send
}
```

### 4.4 消息队列

```c
// 发送队列(业务层 → TxTask)
int mqtt_tx_queue_put(mqtt_msg_t* msg, uint32_t timeout);
osStatus mqtt_tx_queue_get(mqtt_msg_t* msg, uint32_t timeout);

// 接收队列(RxTask → 业务层)
int mqtt_rx_queue_put(mqtt_msg_t* msg, uint32_t timeout);
osStatus mqtt_rx_queue_get(mqtt_msg_t* msg, uint32_t timeout);

// OTA 专用队列
int ota_cmd_queue_put(mqtt_msg_t* msg, uint32_t timeout);
osStatus ota_cmd_queue_get(mqtt_msg_t* msg, uint32_t timeout);
```

**`mqtt_msg_t` 结构**:
```c
typedef struct {
    char topic[64];
    char payload[2048];   // 支持大包(Base64 编码的 1KB chunk ≈ 1366 字符)
    uint16_t len;
    QoS qos;
} mqtt_msg_t;
```

### 4.5 timer_task 定时任务

```c
typedef struct {
    uint32_t interval;       // 执行间隔 (ms)
    uint32_t last_tick;      // 上次执行时间戳
    timer_callback_t callback;
} timer_task_t;
```

**内置定时任务**:
| 任务 | 间隔 | 用途 |
|------|------|------|
| LED 闪烁 | 500ms | 指示运行 |
| MQTT 定时发布 | 5000ms | 定期上报 `ota/notify` 心跳 |
| 空闲任务 | 1ms | 喂狗等 |

---

## 5. W5500 驱动

### 5.1 硬件接口

| 引脚 | 功能 | GPIO |
|------|------|------|
| SPI1 SCK | 时钟 | PA5 |
| SPI1 MISO | 主入从出 | PA6 |
| SPI1 MOSI | 主出从入 | PA7 |
| W5500 CS | 片选 | PA4 |
| W5500 RST | 复位 | PB0 |
| W5500 INT | 中断(可选) | 未用, 轮询 |

### 5.2 SPI 适配(`w5500_conf.c`)

```c
static uint8_t spi_read_byte(void) {
    uint8_t data;
    HAL_SPI_Receive(&hspi1, &data, 1, HAL_MAX_DELAY);
    return data;
}

static void spi_write_byte(uint8_t data) {
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}

void wizchip_spi_cbfunc(void) {
    reg_wizchip_spi_cbfunc(spi_read_byte, spi_write_byte);
    reg_wizchip_spi_read_burst_cbfunc(spi_read_burst);
    reg_wizchip_spi_write_burst_cbfunc(spi_write_burst);
}
```

### 5.3 网络配置

`mqtt_config.h`:
```c
#define NET_CONFIG_DHCP     0
#define NET_CONFIG_STATIC   1
#define NET_CONFIG_MODE     NET_CONFIG_DHCP   // 切换 NET_CONFIG_STATIC 改用静态 IP
```

### 5.4 TCP 客户端

`tcp_client.c` 封装 W5500 socket,提供**非阻塞**接口:
```c
int tcp_client_connect(void);       // 连接 Broker
int tcp_client_send(uint8_t* buf, uint16_t len);
int tcp_client_recv(uint8_t* buf, uint16_t len);
void tcp_client_disconnect(void);
int tcp_client_is_connected(void);
```

**关键**: `tcp_client_recv` 先 `getSn_RX_RSR()` 检查缓冲区,不阻塞等。

---

## 6. 数据流

### 6.1 发布流程

```
业务层调用 mqtt_tx_queue_put(&msg, 0)
        │
        ▼
   mqtt_tx_queue (FreeRTOS Queue)
        │
        ▼
   MQTTTxtask 等待队列
        │  mqtt_tx_queue_get
        ▼
   mqtt_client_publish()
        │
        ▼
   MQTTPublish (Paho)
        │
        ▼
   mqtt_network_write
        │
        ▼
   tcp_client_send
        │
        ▼
   W5500 socket send
        │
        ▼
   SPI → 网络
```

### 6.2 接收流程

```
网络数据到达 W5500
        │
        ▼
   W5500 socket (接收缓冲区)
        │
        ▼
   mqtt_task 中的 mqtt_client_loop_nonblocking(100ms)
        │  MQTTYield
        ▼
   MQTTPacket 解析
        │
        ▼
   deliverMessage → messageHandler
        │
        ▼
   mqtt_task::on_mqtt_message
        │  按 topic 分派
        ▼
   ota_cmd_queue_put 或 mqtt_rx_queue_put
        │
        ▼
   OTATask / 业务层从队列取出处理
```

---

## 7. 配置参数

### 7.1 MQTT Broker(`mqtt_config.h`)

| 参数 | 默认值 | 说明 |
|------|-------|------|
| `MQTT_BROKER_HOSTNAME` | `app-management-server.washer-saas.istarix.com` | Broker 地址(支持域名) |
| `MQTT_BROKER_PORT` | `20118` | 端口 |
| `MQTT_CLIENT_ID` | `W5500_DEVICE` | Client ID(全网唯一) |
| `MQTT_USERNAME` | `washer_saas_mu` | 用户名(可选) |
| `MQTT_PASSWORD` | `$5ywq8bye5e7ah2hb*` | 密码(可选) |
| `MQTT_KEEP_ALIVE` | `60` | 心跳间隔(秒) |
| `MQTT_CLEAN_SESSION` | `1` | 清理会话 |
| `MQTT_COMMAND_TIMEOUT` | `5000` | 命令超时(ms) |
| `MQTT_SEND_BUF_SIZE` | `4096` | 发送缓冲 |
| `MQTT_READ_BUF_SIZE` | `8192` | 接收缓冲(支持 4KB OTA 块) |

### 7.2 OTA 配置

| 参数 | 默认值 | 说明 |
|------|-------|------|
| `OTA_DEVICE_ID` | `w5500_001` | 设备 ID |
| `OTA_TOPIC_CMD` | `device/{id}/ota/cmd` | 命令主题 |
| `OTA_TOPIC_DATA` | `device/{id}/ota/data` | 数据主题 |
| `OTA_TOPIC_STATUS` | `device/{id}/ota/status` | 状态主题 |
| `OTA_TOPIC_ACK` | `device/{id}/ota/ack` | ACK 主题 |
| `OTA_TOPIC_NOTIFY` | `device/{id}/ota/notify` | **事件主题(QoS1)** |
| `OTA_DEFAULT_CHUNK_SIZE` | `1024` | 默认块大小 |
| `OTA_MAX_CHUNK_SIZE` | `4096` | 最大块大小 |
| `OTA_MIN_CHUNK_SIZE` | `1024` | 最小块大小 |
| `OTA_FIRMWARE_MAX_SIZE` | `480 * 1024` | 最大固件尺寸 |
| `OTA_PROGRESS_INTERVAL` | `10` | 进度上报间隔(%) |

### 7.3 网络配置

| 参数 | 默认值 |
|------|-------|
| `NET_CONFIG_MODE` | `NET_CONFIG_DHCP` |
| `STATIC_IP_ADDR` | `192.168.1.88` |
| `STATIC_SUBNET_MASK` | `255.255.255.0` |
| `STATIC_GATEWAY` | `192.168.1.1` |
| `STATIC_DNS` | `8.8.8.8` |
| `MAC_ADDR` | `00:08:DC:12:34:56` |

---

## 8. 日志与看门狗

### 8.1 日志

使用 **SEGGER RTT**(零开销,不占用 UART):

```c
LOGI("Info message");    // 正常
LOGW("Warning message"); // 警告
LOGE("Error message");   // 错误
```

查看方式:Keil Debug 模式 + J-Link RTT Viewer。

### 8.2 看门狗

**IWDG 配置**(`main.c::watchdog_init`):
- Prescaler: 256
- Reload: 1250
- **超时 10 秒** @ 32kHz LSI

**喂狗策略**(`mqtt_exception.c::feed_watchdog_safely`):
```c
static void feed_watchdog_safely(void) {
    if (HAL_GetTick() - g_last_watchdog_feed >= 1000) {
        HAL_IWDG_Refresh(&hiwdg);
        g_last_watchdog_feed = HAL_GetTick();
    }
}
```

- 正常情况:每 1 秒喂一次
- 异常情况:仍每 1 秒喂(避免无谓复位)
- 关键:在 `mqtt_exception.c` 状态机中调用,任务挂起时仍能喂

**为什么不用窗口看门狗**:IWDG 简单可靠,适合工业级;WWDG 太严格,容易误复位。

---

## 9. 设计原则

### 9.1 非阻塞
所有网络操作都带超时,绝不 `while(socket wait)`。例如:
```c
int ret = mqtt_client_loop_nonblocking(100);  // 100ms 超时
// 而不是 mqtt_client_loop(无限等)
```

### 9.2 分层恢复

```
PHY 链路断开   →  RECOVERY_FULL_RESET (W5500 软复位 + 重新初始化)
DHCP 失败     →  RECOVERY_RESTART_DHCP
MQTT 断连     →  RECOVERY_RETRY_MQTT (不重置 W5500)
```

**禁止**MQTT 失败就 `NVIC_SystemReset()` 或重置整个 W5500。

### 9.3 任务隔离

- NetTask 负责网络状态机
- TxTask 负责发送
- RxTask 负责接收
- OTATask 负责 OTA(高优先级,独立)
- TimerTask / CLITask 跑在低优先级

### 9.4 静态内存

- 消息队列大小编译期固定
- 消息 buffer 静态分配(`mqtt_msg_t` 数组)
- 禁止 `malloc` / `free`
- FreeRTOS heap 仅供任务栈/队列创建,业务层不申请

### 9.5 错误处理

- 所有函数检查返回值
- 失败 `LOGE` 记录上下文
- 关键路径上报 `ota/status`

---

## 📚 相关文档

- 移植到其他 STM32: [`W5500_MQTT_Porting_Guide.md`](W5500_MQTT_Porting_Guide.md)
- OTA 架构: [`OTA_Architecture.md`](OTA_Architecture.md)
- 构建集成: [`OTA_Integration_Guide.md`](OTA_Integration_Guide.md)
- 1 页速查: [`OTA_Quick_Reference.md`](OTA_Quick_Reference.md)
