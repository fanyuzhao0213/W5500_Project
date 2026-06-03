# STM32F4 + W5500 MQTT 客户端项目

基于 STM32F405 + W5500 以太网芯片 + FreeRTOS 的 MQTT 客户端框架，实现了完整的 MQTT 连接、订阅、发布功能，并具备异常检测与自动恢复机制。

## 项目概述

本项目是一个完整的嵌入式 MQTT 客户端解决方案，适用于需要以太网通信的物联网应用。主要特点：

- **完整的 MQTT 协议支持**：连接、订阅、发布、Keep Alive
- **异常检测与自动恢复**：PHY 链路检测、MQTT 断线重连、DHCP 租约维护
- **非阻塞设计**：所有网络操作采用非阻塞模式，避免任务阻塞
- **多任务架构**：基于 FreeRTOS 的多任务设计，任务隔离清晰
- **高效日志系统**：使用 SEGGER RTT 实现零开销日志输出
- **看门狗保护**：智能喂狗策略，异常情况下自动恢复

## 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        应用层 (Application)                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  TimerTask   │  │   CLI Task   │  │    用户业务逻辑       │  │
│  │ (定时发布)    │  │  (命令行)    │  │                      │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────────┘  │
├─────────┼─────────────────┼─────────────────────────────────────┤
│         │                 │         MQTT 层                      │
│         ▼                 ▼                                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    MQTT 任务管理                          │  │
│  │  - NetTask (状态机)                                       │  │
│  │  - TxTask (发送队列)                                      │  │
│  │  - RxTask (接收队列)                                      │  │
│  │  - 异常处理模块                                            │  │
│  └──────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                        MQTT 客户端层                             │
│  - mqtt_client (封装层)                                         │
│  - Paho MQTT Client-C (嵌入式 MQTT 库)                          │
│  - mqtt_port (平台适配层)                                        │
├─────────────────────────────────────────────────────────────────┤
│                        TCP/IP 层                                 │
│  - tcp_client (TCP 客户端封装)                                   │
│  - W5500 Socket API                                             │
│  - DHCP / DNS                                                   │
├─────────────────────────────────────────────────────────────────┤
│                        硬件抽象层                                │
│  - STM32 HAL + FreeRTOS                                         │
│  - SPI 驱动 (W5500 通信)                                         │
│  - GPIO (W5500 CS/RST, LED)                                     │
│  - TIM2 (1ms 定时器)                                             │
│  - IWDG (独立看门狗)                                              │
└─────────────────────────────────────────────────────────────────┘
```

## 目录结构

```
w5500_project/
├── Application/                    # 应用层模块
│   ├── MQTT/                       # MQTT 模块
│   │   ├── inc/                    # 头文件
│   │   │   ├── mqtt_client.h       # MQTT 客户端接口
│   │   │   ├── mqtt_config.h       # MQTT 配置参数
│   │   │   ├── mqtt_exception.h    # 异常处理接口
│   │   │   ├── mqtt_port.h         # 平台适配层接口
│   │   │   ├── mqtt_queue.h        # 消息队列接口
│   │   │   ├── mqtt_task.h         # 任务管理接口
│   │   │   └── timer_task.h        # 定时任务接口
│   │   ├── src/                    # 源文件
│   │   │   ├── mqtt_client.c       # MQTT 客户端实现
│   │   │   ├── mqtt_exception.c    # 异常处理实现
│   │   │   ├── mqtt_port.c         # 平台适配层实现
│   │   │   ├── mqtt_queue.c        # 消息队列实现
│   │   │   ├── mqtt_task.c         # 任务管理实现
│   │   │   └── timer_task.c        # 定时任务实现
│   │   └── paho/                   # Paho MQTT 库
│   │
│   ├── W5500/                      # W5500 驱动模块
│   │   ├── tcp_client.c            # TCP 客户端实现
│   │   ├── w5500.c                 # W5500 芯片驱动
│   │   ├── w5500_conf.c            # W5500 SPI 配置
│   │   ├── netconf.c               # 网络配置
│   │   ├── socket.c                # Socket API
│   │   ├── wizchip_conf.c          # WIZnet 芯片配置
│   │   └── Internet/               # 网络协议
│   │       ├── DHCP/               # DHCP 协议
│   │       └── DNS/                # DNS 协议
│   │
│   ├── FreeRTOS_CLI/               # FreeRTOS 命令行接口
│   │
│   ├── RTT/                        # SEGGER RTT 日志
│   │   ├── LOG.h                   # 日志封装接口
│   │   ├── SEGGER_RTT.c            # RTT 核心实现
│   │   └── ...
│   │
│   ├── ioLibrary_Driver-master/    # WIZnet 官方驱动库
│   └── paho.mqtt.embedded-c-master/ # Paho MQTT 官方库
│
├── Core/                           # STM32 HAL 核心
│   ├── Inc/                        # 头文件
│   └── Src/                        # 源文件
│       ├── main.c                  # 主程序入口
│       ├── freertos.c              # FreeRTOS 配置
│       ├── spi.c                   # SPI 配置
│       ├── tim.c                   # 定时器配置
│       └── ...
│
├── Drivers/                        # STM32 HAL 驱动
├── Middlewares/                    # FreeRTOS 中间件
└── docs/                           # 文档目录
    ├── W5500_MQTT_Framework.md     # 框架说明文档
    └── W5500_MQTT_Porting_Guide.md # 移植指南文档
```

## 硬件配置

### 1. 微控制器
- **MCU**: STM32F405RGT6
- **系统时钟**: 168 MHz (HSE)
- **调试接口**: SWD

### 2. W5500 以太网芯片
- **接口**: SPI1
- **CS 引脚**: PA4
- **RST 引脚**: PB0
- **SPI 时钟**: 最高 33 MHz

### 3. 其他硬件
- **LED**: PC8 (状态指示)
- **UART4**: 调试串口（可选）
- **TIM2**: 1ms 定时器（用于时间戳）
- **IWDG**: 独立看门狗（系统保护）

## 软件组件

### 1. FreeRTOS 任务

| 任务名称 | 优先级 | 栈大小 | 功能 |
|---------|-------|-------|------|
| StartNetTask | 高 | 512 | 网络状态机管理 |
| StartMQTTTxTask | 中 | 256 | MQTT 发送队列处理 |
| StartMQTTRxTask | 中 | 256 | MQTT 接收队列处理 |
| StartTimerTask | 低 | 256 | 定时任务管理 |
| StartCLITask | 低 | 256 | 命令行接口 |

### 2. MQTT 功能

- **连接**: 自动连接 MQTT Broker
- **订阅**: 自动订阅指定主题
- **发布**: 定时发布消息（5秒间隔）
- **Keep Alive**: 60 秒心跳间隔
- **QoS**: 支持 QoS0 和 QoS1

### 3. 异常处理

| 异常类型 | 检测机制 | 恢复策略 |
|---------|---------|---------|
| PHY 链路断开 | 每 100ms 检测 | 网络完全复位 |
| DHCP 失败 | DHCP 超时 | 重启 DHCP |
| MQTT 断开 | Keep Alive 失败 | MQTT 重连 |
| 订阅失败 | SUBACK 错误 | MQTT 重连 |

### 4. 网络配置

支持两种 IP 获取方式：
- **DHCP 模式**: 自动获取 IP 地址
- **静态 IP**: 手动配置 IP 地址

## 功能特性

### 1. 核心功能
- ✅ MQTT 连接、订阅、发布
- ✅ PHY 链路检测与恢复
- ✅ DHCP 自动获取 IP
- ✅ DNS 域名解析
- ✅ Keep Alive 心跳维护
- ✅ 异常检测与自动恢复

### 2. 高级功能
- ✅ 消息队列异步发送/接收
- ✅ 定时任务调度
- ✅ 看门狗智能喂狗
- ✅ SEGGER RTT 高效日志
- ✅ FreeRTOS CLI 命令行

### 3. 可配置参数
- ✅ MQTT Broker 地址/端口
- ✅ MQTT Client ID
- ✅ MQTT 用户名/密码
- ✅ MQTT Keep Alive 间隔
- ✅ MQTT QoS 等级
- ✅ 订阅/发布主题
- ✅ 发布间隔
- ✅ 缓冲区大小

## 使用说明

### 1. 环境准备

**硬件：**
- STM32F405 开发板
- W5500 以太网模块
- J-Link 调试器
- 网线

**软件：**
- Keil MDK 或 STM32CubeIDE
- J-Link RTT Viewer（查看日志）
- MQTT Broker（如 EMQX、Mosquitto）

### 2. 配置修改

修改 `Application/MQTT/inc/mqtt_config.h`：

```c
/* MQTT Broker 配置 */
#define MQTT_BROKER_IP         "your.broker.ip"    // 修改为你的 Broker IP
#define MQTT_BROKER_PORT       1883                // 修改端口
#define MQTT_CLIENT_ID         "your_client_id"    // 修改 Client ID

/* 订阅/发布主题 */
#define MQTT_SUBSCRIBE_TOPIC   "your/topic"        // 修改订阅主题
#define MQTT_PUBLISH_TOPIC     "your/topic"        // 修改发布主题
#define MQTT_PUBLISH_INTERVAL  5000                // 修改发布间隔
```

### 3. 编译与烧录

1. 打开 Keil MDK 工程：`MDK-ARM/w5500_project.uvprojx`
2. 编译项目（Build）
3. 使用 J-Link 烧录到 STM32

### 4. 查看日志

1. 连接 J-Link 到 STM32
2. 打开 J-Link RTT Viewer
3. 选择正确的连接配置
4. 查看实时日志输出

### 5. 验证功能

**正常启动日志：**

```
I: TimerTask: started
I: W5500: Init success, version=0x04
I: PHY: Link detected!
I: DHCP: Success, IP=192.168.1.100
I: MQTT: Connecting to broker...
I: MQTT CONNECT OK
I: MQTT: Subscribe OK
I: MQTT: Successfully connected to broker!
I: TimerTask: publish queued success!
```

## 配置参数

### MQTT 配置 (mqtt_config.h)

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

### 网络配置

| 参数 | 默认值 | 说明 |
|-----|-------|------|
| NET_CONFIG_MODE | DHCP | IP 获取方式 |
| STATIC_IP_ADDR | "192.168.1.88" | 静态 IP |
| MAC_ADDR | "00:08:DC:12:34:56" | MAC 地址 |

## 日志系统

### 日志级别

| 宏 | 说明 | 颜色 |
|-----|------|------|
| `LOGI(...)` | 信息级别 | 绿色 |
| `LOGW(...)` | 警告级别 | 黄色 |
| `LOGE(...)` | 错误级别 | 红色 |

### 使用示例

```c
LOGI("MQTT: Connected to broker");
LOGW("MQTT: Keep Alive timeout");
LOGE("MQTT: Publish failed, rc=%d", rc);
```

## 文档

详细文档请参阅：

- **框架说明**: [docs/W5500_MQTT_Framework.md](docs/W5500_MQTT_Framework.md)
- **移植指南**: [docs/W5500_MQTT_Porting_Guide.md](docs/W5500_MQTT_Porting_Guide.md)

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-28 | 初始版本，集成 RTT 和 FreeRTOS |
| v2.0 | 2026-05-29 | 添加 W5500 驱动和 MQTT 客户端 |
| v2.1 | 2026-05-30 | 添加异常检测与自动恢复机制 |
| v2.2 | 2026-05-30 | 添加定时任务和消息队列 |

## 许可证

本项目使用以下开源组件：

- **STM32 HAL**: STMicroelectronics BSD 许可证
- **FreeRTOS**: MIT 许可证
- **Paho MQTT**: Eclipse 公共许可证
- **WIZnet ioLibrary**: WIZnet 许可证
- **SEGGER RTT**: SEGGER 许可证

## 参考资料

- [STM32F405 数据手册](https://www.st.com/)
- [W5500 数据手册](https://www.wiznet.io/)
- [Paho MQTT 文档](https://www.eclipse.org/paho/)
- [FreeRTOS 文档](https://www.freertos.org/)
- [SEGGER RTT 文档](https://www.segger.com/)

## 技术支持

如有问题，请参考项目文档或查看源码注释。