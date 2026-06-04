# 🔧 OTA 集成与构建指南

> 适用项目:STM32F405RGT6 + W5500 + Keil MDK 5
> 文档版本:V3.0 / 2026-06-04

本文档讲解如何把 OTA 模块集成到 Keil 工程、配置 sct、烧录顺序,以及关键的 **VTOR 修复**。

---

## 📑 目录

- [1. Flash 分区回顾](#1-flash-分区回顾)
- [2. Keil 工程配置](#2-keil-工程配置)
- [3. 链接脚本 (sct)](#3-链接脚本-sct)
- [4. 运行时 VTOR 修复 ⭐](#4-运行时-vtor-修复-)
- [5. 烧录顺序](#5-烧录顺序)
- [6. 服务器部署](#6-服务器部署)
- [7. 端到端验证](#7-端到端验证)
- [8. 常见问题](#8-常见问题)

---

## 1. Flash 分区回顾

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

定义在 [`Application/OTA/ota_config.h`](../Application/OTA/ota_config.h)。

---

## 2. Keil 工程配置

### 2.1 工程结构

`MDK-ARM/w5500_project.uvprojx` 包含 **2 个 build target**:

| Target | 用途 | `VECT_TAB_OFFSET` | Scatter File | IROM1 Start / Size |
|--------|------|-------------------|--------------|-------------------|
| `w5500_project_A` | 编译 App-A 烧录到 `0x0800C000` | `0x0000C000` | `w5500_project_A.sct` | `0x0800C000` / `0x00078000` |
| `w5500_project_B` | 编译 App-B 烧录到 `0x08084000` | `0x00084000` | `w5500_project_B.sct` | `0x08084000` / `0x00078000` |

**重要**:Bootloader 是**独立工程** `Bootloader/MDK-ARM/Bootloader.uvprojx`,烧录到 `0x08000000`。

### 2.2 源文件分组

`Application/OTA/` 下的文件需要加入工程:

| 文件 | 是否必须 |
|------|---------|
| `ota_client.c` | ✅ 核心 |
| `ota_params.c` | ✅ 参数区 |
| `flash_driver.c` | ✅ Flash 驱动 |
| `ota_example.c` | ❌ 仅测试用 |

### 2.3 头文件路径

`Project → Options for Target → C/C++ → Include Paths`:
```
..\Application\OTA
```

### 2.4 全局宏定义

`Project → Options for Target → C/C++ → Define`:

| Target | Define |
|--------|--------|
| `w5500_project_A` | `USE_HAL_DRIVER,STM32F405xx,VECT_TAB_OFFSET=0x0000C000` |
| `w5500_project_B` | `USE_HAL_DRIVER,STM32F405xx,VECT_TAB_OFFSET=0x00084000` |

> 💡 V3.0+ 起,VECT_TAB_OFFSET 的精确值已**不再关键**(`ota_fix_vtor()` 会运行时修正),但建议仍然配置,作为 `SystemInit` 里的兜底。

---

## 3. 链接脚本 (sct)

### 3.1 App-A 的 sct (`w5500_project_A.sct`)

```scat
LR_IROM1 0x0800C000 0x00078000  {    ; load region size_region (App-A)
  ER_IROM1 0x0800C000 0x00078000  {  ; load address = execution address
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x0001C000  {  ; 112KB RAM
    .ANY (+RW +ZI)
  }
}
```

### 3.2 App-B 的 sct (`w5500_project_B.sct`)

```scat
LR_IROM1 0x08084000 0x00078000  {    ; load region size_region (App-B)
  ER_IROM1 0x08084000 0x00078000  {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x0001C000  {
    .ANY (+RW +ZI)
  }
}
```

### 3.3 关键点

- **起始地址必须严格匹配 Flash 分区**,否则会覆盖 Bootloader 或 Params
- `*.o (RESET, +First)` 保证 Reset_Handler 在最前
- RAM 区固定在 `0x20000000`,两个 App 用同一片 RAM

### 3.4 Keil 里的 IROM1 配置

`Project → Options for Target → Target`:

| Target | IROM1 Start | Size | IRAM1 Start | Size |
|--------|-----------|------|-------------|------|
| A | `0x0800C000` | `0x00078000` | `0x20000000` | `0x0001C000` |
| B | `0x08084000` | `0x00078000` | `0x20000000` | `0x0001C000` |

---

## 4. 运行时 VTOR 修复 ⭐⭐⭐

### 4.1 为什么需要这个修复

**问题现象**:
- A→B 升级 OK
- B→A 升级擦 sector 3 时死机(0x0800C000-0x0800FFFF)
- 复位原因显示"Unknown"(其实是 IWDG)

**根因**:
- `SCB->VTOR` 是**编译期常量**(`VECT_TAB_OFFSET` 宏)
- Keil 增量编译有时**不会重编** `system_stm32f4xx.c`
- B 固件里 `SystemInit` 把 VTOR 设成 0x0800C000(还是 A 的向量表)
- B 运行时所有中断跳到 0x0800C000 取入口
- 擦 sector 3 把向量表擦成 0xFFFFFFFF
- SysTick 来 → 跳到 0xFFFFFFFF → HardFault → IWDG 复位

**反汇编验证**:
```bash
arm-none-eabi-objdump -d w5500_project_B.axf | grep -A 5 "<SystemInit>:"
# 看到 .word 0x0800c000 ← 字面量是 0x0800C000, 而不是 0x08084000
```

### 4.2 解决方案:运行时段修正

`Core/Src/main.c` 中加 `ota_fix_vtor()`:

```c
#include "ota_config.h"   /* APP_A_ADDR / APP_B_ADDR */

static void ota_fix_vtor(void) {
    /* 用自己函数的 PC 判断当前在哪个分区
     * 函数地址必在 LR_IROM1 范围内, 跟随固件所在 Flash 分区 */
    uint32_t pc = (uint32_t)(void *)&ota_fix_vtor;
    uint32_t vtor_base;

    if (pc >= APP_B_ADDR && pc < APP_B_ADDR + APP_B_SIZE) {
        vtor_base = APP_B_ADDR;
    } else if (pc >= APP_A_ADDR && pc < APP_A_ADDR + APP_A_SIZE) {
        vtor_base = APP_A_ADDR;
    } else {
        /* 极端兜底:不在任何已知分区,保持原 VTOR */
        return;
    }

    __DSB();
    SCB->VTOR = vtor_base;
    __DSB();
    __ISB();
}
```

**`main()` 第一时间调用**:

```c
int main(void) {
    HAL_Init();

    /* 必须在任何中断/外设初始化之前修正 VTOR
     * 否则 HAL_Init() 内部 SysTick 配置后, SysTick 中断会跳到错误的向量表 */
    ota_fix_vtor();

    /* ... 后续初始化 ... */
}
```

### 4.3 配合:让 SystemInit 不再 #error

`Core/Src/system_stm32f4xx.c`:

```c
#if !defined(VECT_TAB_OFFSET)
/* 默认值, 即使 Keil 没配 Define 也能编译
 * 真正生效的 VTOR 由 main() 里的 ota_fix_vtor() 运行时设置 */
#define VECT_TAB_OFFSET    0x0000C000UL
#endif
```

这样即使 B build target 漏配 `VECT_TAB_OFFSET=0x00084000`,也能编译过,`ota_fix_vtor()` 兜底。

### 4.4 验证修复

启动日志应看到(每个分区不同):

```
App-A 启动:I: ota_fix_vtor: PC=0x0800xxxx -> VTOR=0x0800C000 (APP_A)
App-B 启动:I: ota_fix_vtor: PC=0x0808xxxx -> VTOR=0x08084000 (APP_B)
```

**重要**:如果 A 启动看到 `VTOR=0x08084000` 或 B 启动看到 `VTOR=0x0800C000`,说明新代码没编进去,需要清 `system_stm32f4xx.o` 后重编。

---

## 5. 烧录顺序

### 5.1 全新设备(从未烧过)

**步骤 1:烧 Bootloader** (`0x08000000` ~ `0x0800BFFF`)
```
Bootloader.uvprojx → 编译 → Bootloader.bin
烧录到 0x08000000, size 0xC000 (48KB)
```

**步骤 2:烧 App-A** (`0x0800C000` ~ `0x08083FFF`)
```
w5500_project_A target → 编译 → w5500_project_A.bin
烧录到 0x0800C000, size 0x78000 (480KB)
```

**步骤 3:烧 App-B** (`0x08084000` ~ `0x080FBFFF`,可选,用于直接验证)
```
w5500_project_B target → 编译 → w5500_project_B.bin
烧录到 0x08084000, size 0x78000
```

**注意**:
- App-B 也可以不烧,等 OTA 推过来
- Params 区(`0x080FC000`)**不要手动烧任何东西**,App 会自己初始化
- 用 ST-LINK Utility / J-Flash / Keil 烧录都行

### 5.2 升级现有设备(已有 App-A)

只需要用服务器推 App-B 固件即可,App-A 烧录后 Bootloader 跳到 App-A,App-A 收到 ota_start 后下载到 App-B。

### 5.3 烧录脚本示例 (J-Link)

```
# JLinkExe 命令
r
loadfile Bootloader.bin 0x08000000
loadfile w5500_project_A.bin 0x0800C000
# 烧 App-B 是可选的
# loadfile w5500_project_B.bin 0x08084000
r
g
```

### 5.4 Keil Download 按钮配置

`Project → Options for Target → Debug → Settings → Flash Download`:

勾选:
- ☑ Program
- ☑ Verify
- ☑ Reset and Run

**不要**勾"Erase Full Chip",只擦除要烧录的扇区即可。

---

## 6. 服务器部署

### 6.1 安装依赖

```bash
cd SERVER
pip install -r requirements.txt
```

`requirements.txt`:
```
PyQt5>=5.15
paho-mqtt>=2.0
intelhex>=2.3
```

### 6.2 启动

```bash
python ota_server.py
```

GUI 启动,默认连接 `app-management-server.washer-saas.istarix.com:20118`。

### 6.3 关键配置

| 项 | 默认值 | 修改位置 |
|----|-------|---------|
| Broker | `app-management-server.washer-saas.istarix.com` | UI "连接配置" 标签 |
| 端口 | `20118` | UI |
| 设备 ID | `w5500_001` | UI |
| 块大小 | 1 KB | UI "升级配置" → "分块大小" |
| ACK 超时 | 2000 ms | `ota_server.py:81` |
| 重试次数 | 5 次 | `ota_server.py:82` |
| 总升级超时 | 5 分钟 | `ota_server.py:84` |
| 新固件确认超时 | 1 分钟 | `ota_server.py:85` |

### 6.4 固件文件格式

- **HEX**:Intel HEX 格式(Keil 默认输出)
- **BIN**:二进制(去掉地址信息,运行时按目标分区偏移写入)

服务器自动识别扩展名。

---

## 7. 端到端验证

### 7.1 启动验证清单

| 检查 | 命令/操作 | 期望 |
|------|---------|------|
| 设备启动 | 上电 | RTT log 出现 `ota_fix_vtor: PC=0x0800xxxx -> VTOR=0x0800C000 (APP_A)` |
| DHCP 成功 | 等待 | 日志有 `DHCP Success: IP=...` |
| MQTT 连接 | 等待 | 日志有 `MQTT Connected` |
| 主题订阅 | 服务器订阅 | 服务器日志显示 `device/w5500_001/ota/status` 等已订阅 |
| 服务器连接 | GUI 启动 → 连接 | 状态变绿色 |

### 7.2 A→B 升级验证

1. 服务器选 App-B 固件 → 输入版本(如 `1.0.1`)→ "开始升级"
2. 设备日志应看到:
   - `I: OTA: ota_start received`
   - `I: OTA: erasing target...`
   - 逐块:`I: OTA: Flash write success, sending ACK...`
   - `I: OTA: All data chunks received, verifying...`
   - `I: OTA: CRC32 verification passed!`
   - `I: OTA: Params written verified: active_app=1, ota_flag=1, app_b_valid=1`
   - `I: OTA: Will reboot in 2s to apply...`
   - `I: OTA: Triggering NVIC_SystemReset() now...`
3. 设备复位 → Bootloader 跳到 App-B
4. 新 App-B 启动日志:
   - `I: ota_fix_vtor: PC=0x0808xxxx -> VTOR=0x08084000 (APP_B)`
   - `I: ota_client_init: running APP_B, version=101`
5. 服务器收到 `ota/notify(ota_success)`
6. GUI 弹窗"升级成功"

### 7.3 B→A 升级验证(关键!VTOR 修复验证)

1. 当前是 App-B
2. 服务器选 App-A 固件 → "开始升级"
3. **关键观察**:`flash_erase_range: erasing sector 3` 时不应死机
4. 同 7.2 的 2~6 步,只是 active_app 反过来
5. 完成后 App-A 启动,看到 `VTOR=0x0800C000 (APP_A)`

**如果 B→A 死机**:
- 看是否看到 `Will reboot in 2s` 日志(没看到说明 ota_trigger_upgrade 提前 return)
- 死机前最后一行是不是 `erasing sector 3` 或 `erasing sector 4`
- 检查是否删除了 `system_stm32f4xx.o` 强制重编

### 7.4 重复升级验证(稳定性)

1. A→B→A→B→A 连续 5 次升级
2. 每次都应成功
3. 服务器每次都应收到 `ota_success`

---

## 8. 常见问题

### 8.1 编译错误

**Q: `undefined reference to ota_params_xxx`**
- 确认 `Application/OTA/ota_params.c` 已加入工程

**Q: `ota_config.h: No such file`**
- 确认 Include Paths 加了 `..\Application\OTA`

### 8.2 烧录错误

**Q: Keil 报 "Contents mismatch at 0x0800C000"**
- 启动 App 后,VTOR 错误导致跑飞,改 boot 区数据
- 重新烧录整个 Flash(`Erase Full Chip`)

**Q: ST-Link 找不到设备**
- 检查 SWD 接口接线
- 确认 BOOT0 = 0(从 Flash 启动,不是 System Memory)

### 8.3 运行时错误

**Q: DHCP 超时**
- 检查网线、DHCP 服务器
- 改用静态 IP(改 `mqtt_config.h` 的 `NET_CONFIG_MODE`)

**Q: MQTT 连接被拒**
- 检查 Broker 地址/端口
- 检查用户名/密码
- 检查 Client ID 是否冲突

**Q: 升级中途卡住,设备不响应**
- 服务器收不到 `ota/ack`,2 秒后重发
- 5 次后服务器弹窗"升级失败"

**Q: `ota_success` 收不到**
- 看 RTT log 是否有 `PENDING -> SUCCESS` 日志
- 检查 `g_ota_success_notified` 是否被错误清除
- 服务器订阅 `ota/notify` 用 QoS 1

**Q: 设备升级后不重启**
- 看日志最后是否有 `Will reboot in 2s to apply...`
- 如果有但没重启:`NVIC_SystemReset()` 可能被钩住
- 如果没有:`ota_trigger_upgrade()` 提前 return,看前面 stage 是什么

### 8.4 VTOR 相关

**Q: B→A 升级擦 sector 3 死机**
- 99% 是 VTOR 没修正
- 确认 `main.c` 第一行有 `ota_fix_vtor()`
- 确认 `system_stm32f4xx.c` 已被重编(删 `.o` 后重 F7)

**Q: 重启后日志显示 VTOR 不对**
- 删 `MDK-ARM\w5500_project\system_stm32f4xx.o` 重编
- 检查 main.c 头部 `#include "ota_config.h"` 是否在

### 8.5 服务器相关

**Q: 升级时间太长**
- 调小 `ACK_TIMEOUT_MS`(默认 2s)
- 调小 `MAX_RETRY_COUNT`(默认 5)

**Q: 网络不好时容易失败**
- 减小块大小(1KB → 512B)
- 增大 `MAX_RETRY_COUNT`

**Q: 已经停止却还在发包**
- 升级前确认 `_clear_ack_timeout_timer()` 修复已生效
- 检查 `ack_timeout_timer` 是否在所有路径都清理了

---

## 📚 相关文档

- 架构设计: [`OTA_Architecture.md`](OTA_Architecture.md)
- 协议规范: [`OTA_Protocol_Specification.md`](OTA_Protocol_Specification.md)
- 1 页速查: [`OTA_Quick_Reference.md`](OTA_Quick_Reference.md)
