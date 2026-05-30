# W5500 MQTT 框架说明

## 1. 概述

本项目是一个基于 STM32F4 + W5500 以太网芯片 + FreeRTOS 的 MQTT 客户端框架。该框架实现了完整的 MQTT 连接、订阅、发布功能，并具备异常检测与自动恢复机制。

## 2. 系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        应用层 (Application)                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  TimerTask   │  │   CLI Task   │  │    用户业务逻辑       │  │
│  │ (定时发布)    │  │  (命令行)    │  │                      │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────────┘  │
│         │                 │                                      │
├─────────┼─────────────────┼─────────────────────────────────────┤
│         │                 │         MQTT 层                      │
│         ▼                 ▼                                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    MQTT 任务管理                          │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │  │
│  │  │  MQTT Task  │  │   Tx Task   │  │     Rx Task     │  │  │
│  │  │ (状态机)     │  │  (发送队列) │  │    (接收队列)   │  │  │
│  │  └──────┬──────┘  └──────┬──────┘  └───────┬─────────┘  │  │
│  │         │                │                 │            │  │
│  │  ┌──────┴────────────────┴─────────────────┴──────────┐ │  │
│  │  │              MQTT 异常处理模块                       │ │  │
│  │  │  - PHY 链路检测                                     │ │  │
│  │  │  - MQTT 连接状态检测                                │ │  │
│  │  │  - 自动恢复策略                                     │ │  │
│  │  └────────────────────────────────────────────────────┘ │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
├──────────────────────────────┼─────────────────────────────────┤
│                              │    MQTT 客户端层                  │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              MQTT 客户端封装 (mqtt_client)                │  │
│  │  - mqtt_client_connect()                                 │  │
│  │  - mqtt_client_subscribe()                               │  │
│  │  - mqtt_client_publish()                                 │  │
│  │  - mqtt_client_loop()                                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
├──────────────────────────────┼─────────────────────────────────┤
│                              │    Paho MQTT 库                   │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Paho MQTT Client-C (嵌入式版)               │  │
│  │  - MQTTConnect()                                         │  │
│  │  - MQTTSubscribe()                                       │  │
│  │  - MQTTPublish()                                         │  │
│  │  - MQTTYield()                                           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
├──────────────────────────────┼─────────────────────────────────┤
│                              │    MQTT 适配层                    │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              MQTT 平台适配层 (mqtt_port)                  │  │
│  │  - Timer 实现 (HAL_GetTick)                              │  │
│  │  - Network 实现 (TCP 收发)                               │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
├──────────────────────────────┼─────────────────────────────────┤
│                              │    TCP/IP 层                      │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              TCP 客户端 (tcp_client)                      │  │
│  │  - tcp_client_connect()                                  │  │
│  │  - tcp_client_send()                                     │  │
│  │  - tcp_client_recv()                                     │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
├──────────────────────────────┼─────────────────────────────────┤
│                              │    W5500 驱动层                   │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              W5500 以太网芯片驱动                         │  │
│  │  - socket() / connect() / send() / recv()                │  │
│  │  - DHCP / DNS                                            │  │
│  │  - PHY 链路检测                                          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
├──────────────────────────────┼─────────────────────────────────┤
│                              │    硬件抽象层                     │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              STM32 HAL + FreeRTOS                         │  │
│  │  - SPI 驱动 (W5500 通信)                                 │  │
│  │  - GPIO (W5500 CS/RST, LED)                              │  │
│  │  - TIM2 (1ms 定时器)                                     │  │
│  │  - IWDG (独立看门狗)                                      │  │
│  │  - FreeRTOS (多任务调度)                                  │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 目录结构

```
Application/
├── MQTT/                    # MQTT 模块
│   ├── inc/                 # 头文件
│   │   ├── mqtt_client.h    # MQTT 客户端接口
│   │   ├── mqtt_config.h    # MQTT 配置参数
│   │   ├── mqtt_exception.h # 异常处理接口
│   │   ├── mqtt_port.h      # 平台适配层接口
│   │   ├── mqtt_queue.h     # 消息队列接口
│   │   ├── mqtt_task.h      # 任务管理接口
│   │   └── timer_task.h     # 定时任务接口
│   ├── src/                 # 源文件
│   │   ├── mqtt_client.c    # MQTT 客户端实现
│   │   ├── mqtt_exception.c # 异常处理实现
│   │   ├── mqtt_port.c      # 平台适配层实现
│   │   ├── mqtt_queue.c     # 消息队列实现
│   │   ├── mqtt_task.c      # 任务管理实现
│   │   └── timer_task.c     # 定时任务实现
│   └── paho/                # Paho MQTT 库
│       ├── MQTTClient.c     # MQTT 客户端核心
│       ├── MQTTClient.h
│       └── ...              # 其他 MQTT 协议文件
│
├── W5500/                   # W5500 驱动模块
│   ├── tcp_client.c         # TCP 客户端实现
│   ├── tcp_client.h
│   ├── w5500.c              # W5500 芯片驱动
│   ├── w5500.h
│   ├── w5500_conf.c         # W5500 SPI 配置
│   ├── w5500_conf.h
│   ├── netconf.c            # 网络配置
│   ├── netconf.h
│   ├── socket.c             # Socket API
│   ├── socket.h
│   ├── wizchip_conf.c       # WIZnet 芯片配置
│   ├── wizchip_conf.h
│   └── Internet/            # 网络协议
│       ├── DHCP/            # DHCP 协议
│       └── DNS/             # DNS 协议
│
├── FreeRTOS_CLI/            # FreeRTOS 命令行接口
│   ├── cli_commands.c
│   ├── cli_commands.h
│   ├── cli_task.c
│   └── cli_task.h
│   ├── FreeRTOS_CLI.c
│   └── FreeRTOS_CLI.h
│
├── RTT/                     # SEGGER RTT 日志
│   ├── LOG.h
│   ├── SEGGER_RTT.c
│   ├── SEGGER_RTT.h
│   └── ...
│
└── Core/                    # STM32 HAL 核心
    ├── Src/
    │   ├── main.c           # 主程序
    │   ├── stm32f4xx_it.c   # 中断处理
    │   └── ...
    └── Inc/
        ├── main.h
        ├── stm32f4xx_hal_conf.h
        └── ...
```

## 3. 核心模块详解

### 3.1 MQTT 任务管理 (mqtt_task)

#### 3.1.1 状态机设计

网络任务采用状态机设计，确保网络连接的可靠性：

```
┌─────────────┐
│ NET_STATE_  │
│   INIT      │ ──────────────────────────────────────────────┐
└──────┬──────┘                                               │
       │ 初始化 W5500                                         │
       ▼                                                      │
┌─────────────┐                                               │
│ NET_STATE_  │                                               │
│ PHY_CHECK   │ ←──────────────────────────────────┐         │
└──────┬──────┘                                   │         │
       │ PHY 链路就绪                              │         │
       ▼                                          │         │
┌─────────────┐                                   │         │
│ NET_STATE_  │                                   │         │
│ DHCP_START  │ (DHCP 模式)                       │         │
└──────┬──────┘                                   │         │
       │ DHCP 成功                                │         │
       ▼                                          │         │
┌─────────────┐                                   │         │
│ NET_STATE_  │                                   │         │
│ MQTT_CONNECT│                                   │         │
└──────┬──────┘                                   │         │
       │ MQTT 连接成功                            │         │
       ▼                                          │         │
┌─────────────┐                                   │         │
│ NET_STATE_  │                                   │         │
│ MQTT_       │                                   │         │
│ SUBSCRIBE   │                                   │         │
└──────┬──────┘                                   │         │
       │ 订阅成功                                 │         │
       ▼                                          │         │
┌─────────────┐                                   │         │
│ NET_STATE_  │                                   │         │
│  RUNNING    │ ──────► 正常运行                  │         │
│             │        - mqtt_client_loop()       │         │
│             │        - DHCP 租约维护            │         │
└──────┬──────┘                                   │         │
       │ 检测到异常                               │         │
       ▼                                          │         │
┌─────────────┐                                   │         │
│ NET_STATE_  │                                   │         │
│  ERROR      │ ──────────────────────────────────┘         │
└─────────────┘     5秒后重试                       恢复    │
                                                    策略    │
```

#### 3.1.2 任务分配

| 任务名称 | 优先级 | 功能 |
|---------|-------|------|
| StartNetTask | 高 | 网络状态机管理 |
| StartMQTTTxTask | 中 | MQTT 发送队列处理 |
| StartMQTTRxTask | 中 | MQTT 接收队列处理 |
| StartTimerTask | 低 | 定时任务管理 |
| StartCLITask | 低 | 命令行接口 |

### 3.2 MQTT 异常处理 (mqtt_exception)

#### 3.2.1 异常类型

```c
typedef enum {
    EXCEPTION_NONE,                  // 无异常
    EXCEPTION_PHY_LINK_DOWN,         // PHY 链路断开
    EXCEPTION_DHCP_FAILED,           // DHCP 失败
    EXCEPTION_MQTT_DISCONNECTED,     // MQTT 断开
    EXCEPTION_MQTT_SUBSCRIBE_FAILED, // 订阅失败
    EXCEPTION_NETWORK_ERROR          // 网络错误
} exception_type_t;
```

#### 3.2.2 恢复策略

```c
typedef enum {
    RECOVERY_FULL_RESET,      // 完全复位 (重启网络)
    RECOVERY_RESTART_DHCP,    // 重启 DHCP
    RECOVERY_MQTT_RECONNECT   // MQTT 重连
} recovery_strategy_t;
```

#### 3.2.3 异常检测机制

1. **PHY 链路检测**：每 100ms 检测 PHY 链路状态
2. **MQTT 连接检测**：通过 `keepalive` 机制检测连接状态
3. **看门狗保护**：异常情况下调整喂狗策略

### 3.3 MQTT 客户端封装 (mqtt_client)

提供简洁的 MQTT 操作接口：

```c
// 初始化
void mqtt_client_init(void);

// 连接
int mqtt_client_connect(void);

// 订阅
int mqtt_client_subscribe(const char* topicFilter, enum QoS qos, messageHandler handler);

// 发布
int mqtt_client_publish(const char* topicName, const char* payload, size_t payloadlen, enum QoS qos);

// 循环处理 (keepalive)
int mqtt_client_loop(int timeout_ms);

// 断开
int mqtt_client_disconnect(void);
```

### 3.4 MQTT 平台适配层 (mqtt_port)

适配 Paho MQTT 库到 STM32 + W5500 平台：

#### 3.4.1 Timer 实现

```c
void TimerInit(Timer* timer);
char TimerIsExpired(Timer* timer);
void TimerCountdownMS(Timer* timer, unsigned int timeout_ms);
void TimerCountdown(Timer* timer, unsigned int timeout);
int TimerLeftMS(Timer* timer);
```

使用 `HAL_GetTick()` 作为时间源。

#### 3.4.2 Network 实现

```c
static int mqtt_network_read(Network* n, unsigned char* buffer, int len, int timeout_ms);
static int mqtt_network_write(Network* n, unsigned char* buffer, int len, int timeout_ms);
static void mqtt_network_disconnect(Network* n);
```

底层调用 W5500 的 TCP 收发函数。

### 3.5 消息队列 (mqtt_queue)

实现异步消息发送/接收：

```c
// 发送队列
int mqtt_tx_queue_put(mqtt_msg_t* msg, uint32_t timeout);
int mqtt_tx_queue_get(mqtt_msg_t* msg, uint32_t timeout);

// 接收队列
int mqtt_rx_queue_put(mqtt_msg_t* msg, uint32_t timeout);
int mqtt_rx_queue_get(mqtt_msg_t* msg, uint32_t timeout);
```

### 3.6 定时任务 (timer_task)

支持多个定时任务的调度：

```c
typedef struct {
    uint32_t interval;       // 执行间隔 (ms)
    uint32_t last_tick;      // 上次执行时间戳
    timer_callback_t callback; // 回调函数
} timer_task_t;
```

内置定时任务：
- **LED 闪烁**：500ms
- **MQTT 定时发布**：5000ms
- **空闲任务**：1ms

### 3.7 TCP 客户端 (tcp_client)

封装 W5500 Socket API：

```c
int tcp_client_connect(void);      // 连接服务器
int tcp_client_send(uint8_t* buf, uint16_t len);  // 发送数据
int tcp_client_recv(uint8_t* buf, uint16_t len);  // 接收数据
void tcp_client_disconnect(void);  // 断开连接
int tcp_client_is_connected(void); // 查询连接状态
```

特点：
- **非阻塞设计**：先检查缓冲区，避免阻塞等待
- **DNS 支持**：可通过域名连接服务器

### 3.8 W5500 驱动

#### 3.8.1 硬件接口

- **SPI 接口**：STM32 SPI1 与 W5500 通信
- **CS 引脚**：PA4
- **RST 引脚**：PB0

#### 3.8.2 网络配置

支持两种 IP 获取方式：

```c
#define NET_CONFIG_DHCP     0   // DHCP 自动获取
#define NET_CONFIG_STATIC   1   // 静态 IP

#define NET_CONFIG_MODE     NET_CONFIG_DHCP
```

## 4. 数据流

### 4.1 MQTT 发布流程

```
TimerTask (5s 定时)
    │
    ▼
user_mqtt_publish()
    │ 准备消息
    ▼
mqtt_tx_queue_put() ─────► 发送队列
                              │
                              ▼
                         TxTask (等待队列)
                              │
                              ▼
                         mqtt_client_publish()
                              │
                              ▼
                         MQTTPublish() (Paho)
                              │
                              ▼
                         mqtt_network_write()
                              │
                              ▼
                         tcp_client_send()
                              │
                              ▼
                         send() (W5500 Socket)
                              │
                              ▼
                         SPI 发送到 W5500
                              │
                              ▼
                         W5500 发送到网络
```

### 4.2 MQTT 接收流程

```
网络数据到达 W5500
    │
    ▼
recv() (W5500 Socket)
    │
    ▼
tcp_client_recv()
    │
    ▼
mqtt_network_read()
    │
    ▼
MQTTYield() / cycle()
    │ 解析 MQTT 消息
    ▼
deliverMessage()
    │
    ▼
mqtt_message_callback()
    │
    ▼
mqtt_rx_queue_put() ─────► 接收队列
                              │
                              ▼
                         RxTask (等待队列)
                              │
                              ▼
                         用户处理消息
```

## 5. 配置参数

### 5.1 MQTT 配置 (mqtt_config.h)

| 参数 | 默认值 | 说明 |
|-----|-------|------|
| MQTT_BROKER_IP | "47.74.187.120" | MQTT 服务器 IP |
| MQTT_BROKER_PORT | 1883 | MQTT 服务器端口 |
| MQTT_CLIENT_ID | "stm32_w5500_client" | 客户端 ID |
| MQTT_KEEP_ALIVE | 60 | Keep Alive 间隔 (秒) |
| MQTT_COMMAND_TIMEOUT | 5000 | 命令超时 (ms) |
| MQTT_SUBSCRIBE_TOPIC | "stm32/test" | 订阅主题 |
| MQTT_PUBLISH_TOPIC | "stm32/uptime" | 发布主题 |
| MQTT_PUBLISH_INTERVAL | 5000 | 发布间隔 (ms) |

### 5.2 网络配置

| 参数 | 默认值 | 说明 |
|-----|-------|------|
| NET_CONFIG_MODE | DHCP | IP 获取方式 |
| STATIC_IP_ADDR | "192.168.1.88" | 静态 IP |
| STATIC_SUBNET_MASK | "255.255.255.0" | 子网掩码 |
| STATIC_GATEWAY | "192.168.1.1" | 网关 |
| STATIC_DNS | "8.8.8.8" | DNS 服务器 |
| MAC_ADDR | "00:08:DC:12:34:56" | MAC 地址 |

## 6. 日志系统

使用 SEGGER RTT 实现高效日志输出：

```c
LOGI("Info message");    // 信息日志
LOGW("Warning message"); // 警告日志
LOGE("Error message");   // 错误日志
```

特点：
- **零开销**：不占用 UART 资源
- **高速**：直接内存写入
- **实时**：通过调试器查看

## 7. 看门狗策略

采用智能喂狗策略：

```c
// 正常情况：每 1 秒喂狗
if (mqtt_is_running() && g_exception_status.type == EXCEPTION_NONE) {
    HAL_IWDG_Refresh(&hiwdg);
}

// 异常情况：延长喂狗间隔，等待恢复
if (HAL_GetTick() - g_last_watchdog_feed >= WATCHDOG_FEED_INTERVAL) {
    HAL_IWDG_Refresh(&hiwdg);
}
```

## 8. 关键设计要点

### 8.1 非阻塞设计

所有网络操作采用非阻塞设计：
- TCP 收发先检查缓冲区状态
- MQTT loop 使用超时参数控制阻塞时间

### 8.2 异常恢复机制

完整的异常检测与恢复链：
- PHY 链路检测 → 网络复位
- MQTT 断开 → MQTT 重连
- DHCP 失败 → 重启 DHCP

### 8.3 任务隔离

- 状态机任务独立管理网络连接
- 发送/接收任务独立处理消息
- 定时任务独立调度周期性任务

### 8.4 内存管理

- 静态分配消息缓冲区
- 避免动态内存分配
- FreeRTOS 队列管理消息流转