# W5500 MQTT 框架移植指南

## 1. 概述

本指南详细说明如何将 W5500 MQTT 框架移植到其他 STM32 系列或其他 MCU 平台。

## 2. 移植前提条件

### 2.1 硬件要求

- MCU：STM32F1/F4/H7 系列（或其他支持 SPI 的 MCU）
- 以太网芯片：W5500
- SPI 接口：至少一个 SPI 端口
- GPIO：至少 2 个 GPIO（CS、RST）
- 定时器：至少一个定时器（用于时间戳）
- 看门狗：独立看门狗（可选）

### 2.2 软件要求

- IDE：Keil MDK / STM32CubeIDE / IAR
- HAL 库：STM32 HAL 或 LL 库
- RTOS：FreeRTOS（或其他 RTOS）
- 调试工具：SEGGER J-Link（用于 RTT 日志）

## 3. 移植步骤

### 3.1 步骤一：硬件接口适配

#### 3.1.1 SPI 配置

修改 `w5500_conf.h` 中的 SPI 引脚定义：

```c
/* W5500 CS GPIO Port and Pin */
#define W5500_CS_PORT  GPIOA     // 根据实际硬件修改
#define W5500_CS_PIN   GPIO_PIN_4

/* W5500 RST GPIO Port and Pin */
#define W5500_RST_PORT GPIOB     // 根据实际硬件修改
#define W5500_RST_PIN  GPIO_PIN_0
```

#### 3.1.2 SPI 初始化

在 `w5500_conf.c` 中实现 SPI 初始化：

```c
void wizchip_spi_cbfunc(void)
{
    // 注册 SPI 回调函数
    reg_wizchip_spi_cbfunc(spi_read_byte, spi_write_byte);
    reg_wizchip_spi_read_burst_cbfunc(spi_read_burst);
    reg_wizchip_spi_write_burst_cbfunc(spi_write_burst);
}
```

**适配要点：**
- 实现 `spi_read_byte` / `spi_write_byte` 单字节读写函数
- 实现 `spi_read_burst` / `spi_write_burst` 批量读写函数（可选，提高效率）

#### 3.1.3 SPI 实现示例（STM32 HAL）

```c
static uint8_t spi_read_byte(void)
{
    uint8_t data;
    HAL_SPI_Receive(&hspi1, &data, 1, HAL_MAX_DELAY);
    return data;
}

static void spi_write_byte(uint8_t data)
{
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}

static void spi_read_burst(uint8_t* buf, uint16_t len)
{
    HAL_SPI_Receive(&hspi1, buf, len, HAL_MAX_DELAY);
}

static void spi_write_burst(uint8_t* buf, uint16_t len)
{
    HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY);
}
```

### 3.2 步骤二：定时器适配

#### 3.2.1 时间源适配

修改 `mqtt_port.c` 中的 Timer 实现：

**方案一：使用 HAL_GetTick()（推荐）**

```c
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

**方案二：使用硬件定时器**

```c
extern volatile uint32_t g_timer_ms_counter;  // 定时器中断维护的计数器

void TimerInit(Timer* timer) {
    timer->start_ms = g_timer_ms_counter;
    timer->timeout_ms = 0;
}

char TimerIsExpired(Timer* timer) {
    return (g_timer_ms_counter - timer->start_ms) >= timer->timeout_ms;
}

void TimerCountdownMS(Timer* timer, unsigned int timeout_ms) {
    timer->start_ms = g_timer_ms_counter;
    timer->timeout_ms = timeout_ms;
}
```

#### 3.2.2 定时器中断配置

在 `main.c` 中配置定时器中断（例如 TIM2，1ms 中断）：

```c
volatile uint32_t g_tim2_ms_counter = 0;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        g_tim2_ms_counter++;
        // 其他定时任务...
    }
}

// 启动定时器
HAL_TIM_Base_Start_IT(&htim2);
```

### 3.3 步骤三：TCP 客户端适配

#### 3.3.1 Socket API 适配

如果使用其他以太网芯片（如 W5100、ENC28J60），需要适配 Socket API：

```c
// tcp_client.c 中需要适配的函数
int tcp_client_connect(void) {
    // 修改 socket() 和 connect() 调用
    ret = socket(TCP_CLIENT_SOCKET, Sn_MR_TCP, 0, 0);
    ret = connect(TCP_CLIENT_SOCKET, g_target_ip, MQTT_BROKER_PORT);
}

int tcp_client_send(uint8_t* buf, uint16_t len) {
    // 修改 send() 调用
    ret = send(TCP_CLIENT_SOCKET, buf, len);
}

int tcp_client_recv(uint8_t* buf, uint16_t len) {
    // 修改 recv() 调用和接收缓冲区检查
    rx_len = getSn_RX_RSR(TCP_CLIENT_SOCKET);
    ret = recv(TCP_CLIENT_SOCKET, buf, rx_len);
}
```

#### 3.3.2 DNS 适配

如果使用域名连接，需要适配 DNS：

```c
static int tcp_resolve_domain(const uint8_t* domain, uint8_t* ip)
{
    // 初始化 DNS
    DNS_init(TCP_CLIENT_SOCKET, buf);
    
    // 执行 DNS 解析
    int8_t ret = DNS_run(dns_ip, (uint8_t*)domain, ip);
    
    return (ret == 1) ? 0 : -1;
}
```

**注意：** DNS 需要一个临时 Socket，确保不与 MQTT Socket 冲突。

### 3.4 步骤四：RTOS 适配

#### 3.4.1 FreeRTOS 配置

确保 FreeRTOS 配置正确：

```c
// FreeRTOSConfig.h
#define configUSE_PREEMPTION            1
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0
#define configCPU_CLOCK_HZ              (SystemCoreClock)
#define configTICK_RATE_HZ              1000   // 1ms tick
#define configMAX_PRIORITIES            7
#define configMINIMAL_STACK_SIZE        128
#define configTOTAL_HEAP_SIZE           ((size_t)20480)
#define configMAX_TASK_NAME_LEN         16
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1
```

#### 3.4.2 任务创建

在 `main.c` 中创建任务：

```c
// 任务定义
osThreadId netTaskHandle;
osThreadId txTaskHandle;
osThreadId rxTaskHandle;
osThreadId timerTaskHandle;

// 创建任务
osThreadDef(NetTask, StartNetTask, osPriorityHigh, 0, 512);
netTaskHandle = osThreadCreate(osThread(NetTask), NULL);

osThreadDef(TxTask, StartMQTTTxTask, osPriorityNormal, 0, 256);
txTaskHandle = osThreadCreate(osThread(TxTask), NULL);

osThreadDef(RxTask, StartMQTTRxTask, osPriorityNormal, 0, 256);
rxTaskHandle = osThreadCreate(osThread(RxTask), NULL);

osThreadDef(TimerTask, StartTimerTask, osPriorityLow, 0, 256);
timerTaskHandle = osThreadCreate(osThread(TimerTask), NULL);
```

#### 3.4.3 其他 RTOS 适配

如果使用其他 RTOS（如 uC/OS、RT-Thread），需要适配：

1. **任务创建 API**
2. **队列 API**
3. **延时 API**

示例（uC/OS-II）：

```c
// 任务创建
OSTaskCreate(StartNetTask, NULL, &net_task_stk[TASK_STK_SIZE-1], NET_TASK_PRIO);

// 队列创建
OSQCreate(&mqtt_tx_queue, mqtt_tx_queue_buf, MQTT_QUEUE_SIZE);

// 延时
OSTimeDlyHMSM(0, 0, 0, 10);
```

### 3.5 步骤五：日志系统适配

#### 3.5.1 SEGGER RTT 配置

1. 添加 RTT 源文件到工程：
   - `SEGGER_RTT.c`
   - `SEGGER_RTT_printf.c`

2. 配置 RTT：

```c
// SEGGER_RTT_Conf.h
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS   2
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 2
#define BUFFER_SIZE_UP                  1024
#define BUFFER_SIZE_DOWN                16
```

3. 使用 J-Link RTT Viewer 查看日志。

#### 3.5.2 其他日志方案

如果不使用 RTT，可适配为：

**方案一：UART 日志**

```c
// LOG.h
#define LOGI(fmt, ...)  printf("[I] " fmt "\r\n", ##__VA_ARGS__)
#define LOGW(fmt, ...)  printf("[W] " fmt "\r\n", ##__VA_ARGS__)
#define LOGE(fmt, ...)  printf("[E] " fmt "\r\n", ##__VA_ARGS__)

// 实现 printf
int fputc(int ch, FILE *f) {
    HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
```

**方案二：禁用日志**

```c
#define LOGI(fmt, ...)  // 空实现
#define LOGW(fmt, ...)  // 空实现
#define LOGE(fmt, ...)  // 空实现
```

### 3.6 步骤六：看门狗适配

#### 3.6.1 独立看门狗配置

在 `main.c` 中初始化看门狗：

```c
// 看门狗配置
IWDG_HandleTypeDef hiwdg;

hiwdg.Instance = IWDG;
hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
hiwdg.Init.Reload = 0xFFF;  // 约 26 秒
HAL_IWDG_Init(&hiwdg);
```

#### 3.6.2 喂狗策略

在 `mqtt_exception.c` 中实现喂狗：

```c
static void feed_watchdog_safely(void)
{
    if (mqtt_is_running() && g_exception_status.type == EXCEPTION_NONE) {
        if (HAL_GetTick() - g_last_watchdog_feed >= 1000) {
            HAL_IWDG_Refresh(&hiwdg);
            g_last_watchdog_feed = HAL_GetTick();
        }
    }
}
```

### 3.7 步骤七：配置参数修改

#### 3.7.1 MQTT 配置

修改 `mqtt_config.h`：

```c
/* MQTT Broker 配置 */
#define MQTT_BROKER_IP         "your.broker.ip"
#define MQTT_BROKER_PORT       1883
#define MQTT_CLIENT_ID         "your_client_id"
#define MQTT_USERNAME          "your_username"
#define MQTT_PASSWORD          "your_password"

/* MQTT 参数 */
#define MQTT_KEEP_ALIVE        60      // 根据服务器要求调整
#define MQTT_COMMAND_TIMEOUT   5000    // 根据网络延迟调整

/* 缓冲区大小 */
#define MQTT_SEND_BUF_SIZE     256     // 根据消息大小调整
#define MQTT_READ_BUF_SIZE     512     // 根据消息大小调整
```

#### 3.7.2 网络配置

```c
/* IP 获取方式 */
#define NET_CONFIG_MODE        NET_CONFIG_DHCP  // 或 NET_CONFIG_STATIC

/* 静态 IP 配置 */
#define STATIC_IP_ADDR         "192.168.1.88"
#define STATIC_SUBNET_MASK     "255.255.255.0"
#define STATIC_GATEWAY         "192.168.1.1"
#define STATIC_DNS             "8.8.8.8"

/* MAC 地址 */
#define MAC_ADDR               "00:08:DC:XX:XX:XX"  // 使用唯一 MAC
```

## 4. 移植验证

### 4.1 验证步骤

1. **SPI 通信验证**
   - 读取 W5500 版本寄存器
   - 验证 SPI 读写正常

2. **网络初始化验证**
   - PHY 链路检测
   - DHCP 或静态 IP 配置

3. **TCP 连接验证**
   - 连接 MQTT Broker
   - 发送/接收测试数据

4. **MQTT 连接验证**
   - MQTT CONNECT 成功
   - 订阅主题成功
   - 发布消息成功

### 4.2 测试代码

```c
// SPI 测试
uint8_t version = getVERSIONR();
LOGI("W5500 Version: 0x%02X", version);  // 应返回 0x04

// PHY 测试
uint8_t phystatus = getPHYCFGR();
LOGI("PHY Status: 0x%02X", phystatus);

// DHCP 测试
if (DHCP_run() == DHCP_IP_LEASED) {
    LOGI("DHCP Success: IP = %d.%d.%d.%d", gWIZNETINFO.ip[0], ...);
}

// MQTT 测试
if (mqtt_client_connect() == MQTT_SUCCESS) {
    LOGI("MQTT Connected!");
    mqtt_client_subscribe("test/topic", QOS0, message_callback);
    mqtt_client_publish("test/topic", "Hello", 5, QOS0);
}
```

## 5. 常见问题与解决

### 5.1 SPI 通信问题

**问题：** W5500 版本寄存器读取错误

**解决：**
- 检查 SPI 模式（W5500 需要 Mode 0 或 Mode 3）
- 检查 SPI 时钟频率（建议 ≤ 33MHz）
- 检查 CS 引脚控制逻辑

### 5.2 DHCP 失败

**问题：** DHCP 无法获取 IP

**解决：**
- 检查 PHY 链路状态
- 检查 DHCP 服务器配置
- 检查 MAC 地址是否唯一

### 5.3 MQTT 连接失败

**问题：** MQTT CONNECT 返回错误码

**解决：**
- 检查 Broker IP 和端口
- 检查 Client ID 是否冲突
- 检查用户名/密码（如果需要）
- 检查 Keep Alive 间隔

### 5.4 发布失败

**问题：** MQTT Publish 返回 -1

**解决：**
- 检查 QOS 设置（QOS1 需要等待 PUBACK）
- 增加 COMMAND_TIMEOUT
- 检查网络延迟
- 检查 Broker 响应

### 5.5 内存不足

**问题：** FreeRTOS 堆内存不足

**解决：**
- 增加 `configTOTAL_HEAP_SIZE`
- 减小任务栈大小
- 减小 MQTT 缓冲区大小

## 6. 性能优化建议

### 6.1 SPI 优化

- 使用 DMA 传输提高效率
- 使用批量读写函数
- 提高时钟频率（最高 33MHz）

### 6.2 任务优化

- 调整任务优先级
- 优化任务栈大小
- 减少任务切换频率

### 6.3 内存优化

- 使用静态内存分配
- 减小缓冲区大小
- 优化消息队列大小

### 6.4 网络优化

- 使用 QOS0 减少确认延迟
- 调整 Keep Alive 间隔
- 优化 TCP 缓冲区大小

## 7. 移植到其他 MCU

### 7.1 STM32F1 系列

主要差异：
- SPI 配置略有不同
- HAL 库 API 相似
- 定时器配置相似

### 7.2 STM32H7 系列

主要差异：
- SPI 配置更复杂（支持更多模式）
- 需要配置 MPU（内存保护单元）
- 更高的时钟频率

### 7.3 其他 MCU（如 ESP32、Nordic）

需要完全重写适配层：
- SPI 驱动
- 定时器驱动
- RTOS 适配
- 网络驱动（如果使用其他以太网芯片）

## 8. 文件清单

移植时需要修改的关键文件：

| 文件 | 修改内容 |
|-----|---------|
| `w5500_conf.h` | SPI 引脚定义 |
| `w5500_conf.c` | SPI 回调函数 |
| `mqtt_port.c` | Timer 实现 |
| `tcp_client.c` | Socket API（如果换芯片） |
| `mqtt_config.h` | MQTT 参数配置 |
| `main.c` | 任务创建、定时器初始化 |
| `FreeRTOSConfig.h` | RTOS 配置 |

## 9. 参考资料

- W5500 数据手册：https://www.wiznet.io/
- Paho MQTT 文档：https://www.eclipse.org/paho/
- FreeRTOS 文档：https://www.freertos.org/
- STM32 HAL 文档：https://www.st.com/

## 10. 技术支持

如有问题，可参考：
- 项目源码注释
- 框架说明文档（W5500_MQTT_Framework.md）
- 日志输出定位问题