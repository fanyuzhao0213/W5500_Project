# STM32F4 基础项目

基于 STM32CubeMX 创建的 STM32F4 项目，集成了 FreeRTOS 和 SEGGER RTT 日志。

## 项目架构

```
w5500_project/
├── Application/
│   └── RTT/                    # SEGGER RTT 日志模块
│       ├── LOG.h               # 日志封装接口
│       ├── SEGGER_RTT.c        # RTT 核心实现
│       └── ...
├── Core/
│   ├── Inc/                    # 头文件
│   └── Src/                    # 源文件
│       ├── freertos.c          # FreeRTOS 任务
│       ├── main.c              # 主程序入口
│       ├── spi.c               # SPI配置
│       └── ...
├── Drivers/                    # STM32 HAL 驱动
└── Middlewares/                # FreeRTOS
```

## 主要组件

### 1. 微控制器
- **MCU**: STM32F405RGT6
- **系统时钟**: 168 MHz (HSE)
- **调试接口**: SWD

### 2. 硬件接口
- **UART4**: 调试串口
- **LED**: PC8

### 3. FreeRTOS 任务
| 任务 | 优先级 | 栈大小 | 说明 |
|------|--------|--------|------|
| defaultTask | Normal | 256 | 默认任务 |

## RTT 日志系统

### 日志宏定义

| 宏 | 说明 | 颜色 |
|-----|------|------|
| `LOGI(...)` | 信息级别 | 绿色 |
| `LOGW(...)` | 警告级别 | 黄色 |
| `LOGE(...)` | 错误级别 | 红色 |
| `LOG(...)` | 无颜色 | 默认 |

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-28 | 初始版本，集成RTT和FreeRTOS |