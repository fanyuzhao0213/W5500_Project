# STM32F4 + W5500 + MQTT + A/B OTA 工业级方案

> 适用硬件:STM32F405RGT6 + W5500 以太网芯片
> RTOS:FreeRTOS(CMSIS-RTOS 封装)
> IDE:Keil MDK 5
> HAL:STM32CubeF4 HAL 库
> 文档版本:V3.0 / 2026-06-04

---

## ✨ 项目亮点

| 模块 | 能力 |
|------|------|
| **网络** | W5500 SPI 以太网 + DHCP/静态 IP + DNS |
| **MQTT** | Paho 嵌入式 C 客户端 + 多任务收发分离 + 异常自愈 |
| **OTA** | 三阶段 A/B 分区 + 服务器状态机 + 失败自动回滚 + 在线升级 |
| **可靠性** | IWDG 看门狗 + 状态机重连 + CRC32 校验 + 掉电安全 |
| **可观测** | SEGGER RTT 零开销日志 + JSON 状态上报 + 升级进度推送 |
| **工程规范** | 工业级编码(HAL/超时/分层/静态分配/错误码) |

---

## 📂 仓库结构

```
w5500_project/
├── APP/                                # STM32 App 工程
│   ├── Application/
│   │   ├── MQTT/                       # MQTT 客户端 (Paho 封装 + 任务 + 队列 + 异常)
│   │   ├── W5500/                      # W5500 驱动 + TCP + DHCP + DNS
│   │   ├── OTA/                        # OTA 客户端 (Flash 驱动 + 参数区 + 状态机)
│   │   ├── FreeRTOS_CLI/               # 命令行接口
│   │   └── RTT/                        # SEGGER RTT 日志
│   ├── Bootloader/                     # 独立 Bootloader 工程 (A/B 切换核心)
│   ├── Core/                           # STM32 HAL 核心 (main.c, system_stm32f4xx.c)
│   ├── Drivers/                        # HAL / CMSIS 驱动
│   ├── MDK-ARM/                        # Keil 工程 (含 A/B 两个 build target)
│   ├── docs/                           # 📚 全部设计文档
│   └── README.md                       # 本文件
├── SERVER/                             # 🖥️  Python OTA 服务器 (PyQt5 GUI)
│   ├── ota_server.py                   # 服务器主程序
│   ├── OTA_Protocol_Specification.md   # 协议规范(服务器侧)
│   └── requirements.txt
├── CLAUDE.md                           # Claude Agent 编码规则
├── PROJECT_RULES.md                    # 项目业务规则
└── (其他文档)                          # 调试记录、升级计划等
```

---

## 🏗️ 系统架构一图看懂

```
┌────────────────────────────────────────────────────────────────────┐
│  STM32F405RGT6                                                     │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │  App-A (0x0800C000, 480KB)        App-B (0x08084000, 480KB)  │    │
│  │  ┌──────────────────────────┐    ┌────────────────────────┐│    │
│  │  │ FreeRTOS                  │    │ FreeRTOS                ││    │
│  │  │  ├─ NetTask  (高)         │    │  ├─ NetTask (高)        ││    │
│  │  │  ├─ MQTTTxtask (中)        │    │  ├─ MQTTTxtask (中)      ││    │
│  │  │  ├─ MQTTRxTask (中)        │    │  ├─ MQTTRxTask (中)      ││    │
│  │  │  ├─ OTATask  (高)          │    │  ├─ OTATask (高)         ││    │
│  │  │  └─ Timer/CLI (低)         │    │  └─ Timer/CLI (低)       ││    │
│  │  └──────────────────────────┘    └────────────────────────┘│    │
│  │  Bootloader (0x08000000, 48KB)                            │    │
│  │  - 读 ota_params → 跳 active_app                          │    │
│  │  - 维护 boot_count 安全网                                  │    │
│  │  Params (0x080FC000, 16KB)                                │    │
│  │  - ota_flag / active_app / boot_count / valid / version    │    │
│  └──────────────────────────────────────────────────────────┘    │
│           │ SPI                                                      │
│           ▼                                                          │
│  ┌────────────────┐      MQTT over TCP        ┌────────────────┐ │
│  │  W5500 以太网  │ ◄──────────────────────────► │  PyQt5 OTA    │ │
│  │  (TCP/IP 协议栈) │      W5500 ◄── SPI ──► STM32   │  服务器       │ │
│  └────────────────┘                              └────────────────┘ │
└────────────────────────────────────────────────────────────────────┘
```

---

## 🚀 快速开始

### 1. 硬件准备
- STM32F405RGT6 开发板
- W5500 以太网模块(SPI 接口)
- 网络能访问 MQTT Broker

### 2. 打开工程
- 双击 `APP/MDK-ARM/w5500_project.uvprojx`
- Keil 自动识别 2 个 build target:
  - **`w5500_project_A`** — 编译 App-A,VECT_TAB_OFFSET=0x0000C000
  - **`w5500_project_B`** — 编译 App-B,VECT_TAB_OFFSET=0x00084000

### 3. 首次烧录(必须)
1. 切到 `w5500_project_A` target → F7 → 得到 `w5500_project_A.bin`
2. 用 ST-Link / J-Link 把 **Bootloader** + **App-A.bin** 烧录到 `0x08000000`
3. 烧 `w5500_project_B.bin` 到 `0x08084000`(用于后续 OTA 验证)

### 4. 启动服务器
```bash
cd SERVER
pip install -r requirements.txt
python ota_server.py
```
GUI 启动 → 配置 Broker → 连接 → 选固件 → 升级。

### 5. 触发 OTA
服务器选 App-B 固件 → "开始升级" → 设备收块 → 写入 → CRC 校验 → 自动重启 → 跳到 B → 联网 → 服务器收到 `ota_success`。

---

## 📚 文档导航

| 文档 | 何时读它 |
|------|---------|
| [docs/Project_Index.md](docs/Project_Index.md) | 7 个文档的总导航(不知道看哪个就先看这个) |
| [docs/OTA_Quick_Reference.md](docs/OTA_Quick_Reference.md) | 1 页速查 OTA 状态机/分区/标志/主题 |
| [docs/OTA_Architecture.md](docs/OTA_Architecture.md) | 设备/服务器/bootloader 三端完整架构 |
| [docs/OTA_Protocol_Specification.md](docs/OTA_Protocol_Specification.md) | 消息格式/字段/超时/重试协议细节 |
| [docs/OTA_Integration_Guide.md](docs/OTA_Integration_Guide.md) | Keil 工程配置/sct/VTOR 修复/构建步骤 |
| [docs/W5500_MQTT_Framework.md](docs/W5500_MQTT_Framework.md) | 框架结构/任务/数据流(网络/MQTT) |
| [docs/W5500_MQTT_Porting_Guide.md](docs/W5500_MQTT_Porting_Guide.md) | 移植到其他 STM32 |

---

## 🔑 关键设计原则

按本项目长期运行、工业级场景的要求:

1. **HAL 库** — 不用标准库/LL/裸寄存器
2. **完整输出** — 不留 `// TODO 用户自行实现`
3. **错误处理** — 所有函数检查返回值,失败 `LOGE`
4. **禁止动态内存** — 工业项目长期运行禁堆碎片
5. **所有操作支持超时** — 禁 `while(socket wait)`
6. **网络分层恢复** — 禁 MQTT 失败就重置整个 W5500
7. **统一日志** — `LOGI/LOGW/LOGE` 禁 printf
8. **UTF-8 编码** — 禁乱码中文
9. **命名规范** — 函数 snake_case / 变量 g_xxx / 宏 UPPER_CASE
10. **任务分层** — 禁 1 个任务包揽网络逻辑

详见 [CLAUDE.md](CLAUDE.md)

---

## 📊 关键参数速查

| 项 | 值 | 位置 |
|---|---|---|
| 设备 ID | `w5500_001` | `Application/MQTT/inc/mqtt_config.h` |
| MQTT Broker | `app-management-server.washer-saas.istarix.com:20118` | `mqtt_config.h` |
| 块大小 | 1 KB(默认) / 2 KB / 4 KB | `mqtt_config.h` |
| 最大固件 | 480 KB | `Application/OTA/ota_config.h` |
| ACK 超时 | 2 秒 | `SERVER/ota_server.py` |
| 重试次数 | 5 次/块 | `SERVER/ota_server.py` |
| 整个升级超时 | 5 分钟 | `SERVER/ota_server.py` |
| 新固件联网确认超时 | 1 分钟 | `SERVER/ota_server.py` |
| 看门狗 | IWDG 10 秒 | `Core/Src/main.c` |

---

## 🐛 故障排查

| 现象 | 看哪 |
|------|------|
| MQTT 连不上 | 物理链路 → DHCP → Broker 地址 → 用户名密码 |
| OTA 卡在下载中 | 服务器日志看 ACK 超时 / 设备日志看 Flash 写错误 |
| 升级后不重启 | 看是否出现 `Will reboot in 2s` 日志;否则 ota_trigger_upgrade 提前 return |
| 重启后没跳新分区 | bootloader 日志 / 验证 `active_app` 是否写对 |
| B→A 升级擦 sector 3 死机 | **VTOR 没设对** → 参考 [docs/OTA_Integration_Guide.md §VTOR 修复](docs/OTA_Integration_Guide.md) |
| 升级时设备掉电,再上电卡住 | bootloader 读 PENDING 后会一直 boot_count++,5 次后兜底 |

---

## 📝 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V1.0 | 2026-05-30 | 初版 MQTT 框架 |
| V2.0 | 2026-06-02 | 加入 A/B OTA 基础流程 |
| V2.1 | 2026-06-03 | 修复 boot_count 双重递增、加入 ota/notify 事件 |
| V2.2 | 2026-06-03 | 加入运行时段 VECT_TAB 修复、完整三阶段升级 |
| V3.0 | 2026-06-04 | 全面重写文档,按角色分类梳理;服务器 ACK 超时调为 2s;修复 timer 泄漏 |

---

## 🤝 贡献

修改前必读:
- [CLAUDE.md](CLAUDE.md) — 编码规则(给 Claude Agent 看的)
- [PROJECT_RULES.md](PROJECT_RULES.md) — 业务规则

代码改动要求:
- 任何 OTA 协议/状态机改动 → 同步更新 `docs/OTA_Architecture.md` 和 `docs/OTA_Protocol_Specification.md`
- 任何 Flash 分区改动 → 同步更新 `docs/OTA_Quick_Reference.md` §Flash 分区
- 任何新增 MQTT 主题 → 同步更新 `docs/OTA_Protocol_Specification.md` §主题定义
- 任何服务器超时/重试改动 → 同步更新 `docs/OTA_Quick_Reference.md` §超时配置
