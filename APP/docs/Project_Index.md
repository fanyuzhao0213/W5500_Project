# 📚 文档总导航

> 适用项目:STM32F405 + W5500 + MQTT + FreeRTOS + OTA
> 文档版本:V3.0 / 2026-06-04

本目录是项目的**设计文档中心**。7 个文档按职责分离,各看一份不超过 20 分钟就能搞懂一个模块。

---

## 🗺️ 文档地图

```
README.md                          ← 项目总入口(30 秒看懂架构)
   │
   └── docs/
         │
         ├── Project_Index.md          ← 你在这里
         │
         ├── OTA_Quick_Reference.md    ← ★ OTA 速查(1 页)
         │     └─ 状态机 / 分区 / 标志位 / 主题 / 超时
         │
         ├── OTA_Architecture.md       ← ★★ OTA 完整架构(按角色分)
         │     ├─ 设备端职责
         │     ├─ 服务器端职责
         │     └─ Bootloader 端职责
         │
         ├── OTA_Protocol_Specification.md  ← ★★ 协议规范
         │     ├─ 消息格式 / 字段
         │     ├─ 超时与重试
         │     └─ 状态机事件
         │
         ├── OTA_Integration_Guide.md  ← ★ 构建/集成
         │     ├─ Keil 工程配置
         │     ├─ sct 链接脚本
         │     ├─ VTOR 运行时修复 (重要!)
         │     └─ 烧录顺序
         │
         ├── W5500_MQTT_Framework.md   ← 网络/MQTT 框架
         │     ├─ 任务分层
         │     ├─ 数据流
         │     └─ 异常恢复
         │
         └── W5500_MQTT_Porting_Guide.md  ← 移植到其他 STM32
               ├─ 硬件接口
               ├─ 驱动适配
               └─ 验证步骤
```

---

## 🎯 我该先看哪个?

| 你的角色 / 目标 | 推荐路径 |
|---------------|---------|
| **新加入的开发者** | README.md → Project_Index.md(本文) → OTA_Quick_Reference.md |
| **要改 OTA 状态机** | OTA_Architecture.md §设备端 → OTA_Protocol_Specification.md → ota_client.c |
| **要改服务器** | OTA_Protocol_Specification.md → SERVER/ota_server.py |
| **要改 Bootloader** | OTA_Architecture.md §Bootloader 端 → Bootloader/Core/Src/main.c |
| **要移植到新 MCU** | W5500_MQTT_Porting_Guide.md |
| **要排查 OTA 升级失败** | OTA_Quick_Reference.md §常见故障 → OTA_Integration_Guide.md §VTOR 修复 |
| **要重新构建/烧录** | OTA_Integration_Guide.md |

---

## 🧩 关键流程图索引

| 流程 | 位置 |
|------|------|
| **A→B 升级完整时序图** | [OTA_Architecture.md §完整时序](OTA_Architecture.md) |
| **B→A 升级完整时序图** | [OTA_Architecture.md §B→A 升级时序](OTA_Architecture.md) |
| **OTA Flag 状态机** | [OTA_Quick_Reference.md §状态机](OTA_Quick_Reference.md) |
| **boot_count 维护时序** | [OTA_Architecture.md §boot_count 维护](OTA_Architecture.md) |
| **网络/MQTT 状态机** | [W5500_MQTT_Framework.md §网络状态机](W5500_MQTT_Framework.md) |
| **MQTT 收发数据流** | [W5500_MQTT_Framework.md §数据流](W5500_MQTT_Framework.md) |

---

## 📋 各文档"是否已经最新"状态

| 文档 | 状态 | 基于代码版本 |
|------|------|------------|
| README.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| Project_Index.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| OTA_Quick_Reference.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| OTA_Architecture.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| OTA_Protocol_Specification.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| OTA_Integration_Guide.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| W5500_MQTT_Framework.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |
| W5500_MQTT_Porting_Guide.md | ✅ V3.0 (2026-06-04) | 当前 HEAD |

---

## 🔗 外部资源

- W5500 数据手册: https://www.wiznet.io/
- Paho MQTT 文档: https://www.eclipse.org/paho/
- FreeRTOS 文档: https://www.freertos.org/
- STM32 HAL 文档: https://www.st.com/

---

## 📝 文档维护规则

修改代码时**必须同步更新文档**:

| 改了什么 | 同步哪个文档 |
|---------|------------|
| OTA 状态机/标志位 | OTA_Architecture.md + OTA_Quick_Reference.md |
| 新增 MQTT 主题/字段 | OTA_Protocol_Specification.md |
| 服务器超时/重试 | OTA_Protocol_Specification.md + OTA_Quick_Reference.md |
| Flash 分区 | OTA_Quick_Reference.md + OTA_Architecture.md |
| Keil build target | OTA_Integration_Guide.md |
| 网络/MQTT 任务 | W5500_MQTT_Framework.md |
| 硬件引脚 | W5500_MQTT_Porting_Guide.md |
