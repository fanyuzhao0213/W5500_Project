# OTA 升级方案设计

## 📋 概述

基于现有的双分区架构 (App-A / App-B)，实现完整的 OTA 远程升级功能。

---

## 🔧 现有架构分析

### Flash 分区布局
```
STM32F405RG (1MB Flash)
+------------------------------------------------------------------+
| 0x08000000 | Bootloader      | 48KB  | Bootloader 区域            |
| 0x0800C000 | App-A           | 480KB | 主应用程序 (激活)           |
| 0x08084000 | App-B           | 480KB | 备份/升级区域               |
| 0x080FC000 | OTA Params      | 16KB  | OTA 参数存储                |
+------------------------------------------------------------------+
```

### OTA 参数结构 (ota_params_t)
```c
typedef struct {
    uint32_t magic_number;     // 魔数 0x4F544153 ("OTAS")
    uint32_t ota_flag;         // 0=正常, 1=待升级, 2=升级成功, 3=回滚
    uint32_t active_app;       // 0=App-A, 1=App-B
    uint32_t boot_count;       // 启动计数
    uint32_t max_boot_count;   // 最大验证次数 (5)

    uint32_t app_a_version;    // App-A 版本号
    uint32_t app_a_size;       // App-A 大小
    uint32_t app_a_crc32;      // App-A CRC32
    uint8_t  app_a_valid;      // App-A 有效性

    uint32_t app_b_version;    // App-B 版本号
    uint32_t app_b_size;       // App-B 大小
    uint32_t app_b_crc32;      // App-B CRC32
    uint8_t  app_b_valid;      // App-B 有效性
} ota_params_t;
```

---

## 🚀 OTA 升级流程

### 整体流程图
```
┌─────────────┐
│   设备启动   │
└──────┬──────┘
       │
       ▼
┌──────────────────┐
│ Bootloader 启动   │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│ 读取 OTA 参数     │
│ ota_flag = ?     │
└──────┬───────────┘
       │
       ├──────┐
       │      │
       ▼      ▼
   ┌─────┐ ┌─────────┐
   │ OTA │ │ 正常启动 │
   │ FLAG│ │          │
   │ =1  │ │ active   │
   └──┬──┘ │ _app     │
      │   └────┬─────┘
      │        │
      ▼        │
┌─────────────┐│
│ 检查新固件   ││
│ (backup区)  ││
└──────┬──────┘│
       │       │
       ▼       │
┌─────────────┐│
│ boot_count++││
│             ││
└──────┬──────┘│
       │       │
       ├───────┤
       │       │
       ▼       ▼
   ┌───────────────┐
   │ boot_count >=  │
   │ max_boot_count│
   └───────┬───────┘
           │
      ┌────┴────┐
      │         │
      ▼         ▼
  ┌───────┐ ┌────────┐
  │ 成功   │ │ 失败   │
  │ boot_  │ │ 触发   │
  │ count=0│ │ 回滚   │
  └────┬───┘ └───┬────┘
       │         │
       ▼         ▼
   ┌─────────────┐
   │ 设置为正式  │
   │ 激活分区   │
   └─────────────┘
```

### Bootloader 升级逻辑

#### 1. 启动阶段
```c
void bootloader_main(void)
{
    // 1. 读取 OTA 参数
    ota_params_read(&params);

    // 2. 检查是否有待升级固件
    if (params.ota_flag == OTA_FLAG_PENDING) {
        // 升级流程
        handle_upgrade(&params);
    } else {
        // 正常启动流程
        boot_normal(&params);
    }
}
```

#### 2. 升级处理
```c
void handle_upgrade(ota_params_t *params)
{
    uint32_t target_app = (params->active_app == APP_A) ? APP_B : APP_A;

    // 验证新固件
    if (verify_firmware(target_app) == SUCCESS) {
        params->boot_count++;

        if (params->boot_count >= params->max_boot_count) {
            // 固件稳定，升级成功
            params->ota_flag = OTA_FLAG_SUCCESS;
            params->active_app = target_app;
            params->boot_count = 0;
            update_firmware_validity(params);
        }
    } else {
        // 固件无效，触发回滚
        params->ota_flag = OTA_FLAG_ROLLBACK;
    }

    ota_params_write(params);
}
```

#### 3. 正常启动
```c
void boot_normal(ota_params_t *params)
{
    // 验证当前固件
    if (verify_firmware(params->active_app) != SUCCESS) {
        // 固件损坏，尝试回滚
        params->ota_flag = OTA_FLAG_ROLLBACK;
        ota_params_write(params);
        // 复位重启
        NVIC_SystemReset();
    }

    // 跳转到固件
    bootloader_jump_to_app(get_app_addr(params->active_app));
}
```

---

### APP OTA 客户端逻辑

#### MQTT 主题设计
```
OTA 主题:
  device/{device_id}/ota/status     - 设备上报 OTA 状态
  device/{device_id}/ota/response  - 设备接收 OTA 响应
  server/ota/command                - 服务器下发 OTA 命令
  server/ota/download              - 服务器提供固件下载地址
```

#### OTA 命令格式 (JSON)
```json
// 服务器 -> 设备: OTA 开始命令
{
    "cmd": "ota_start",
    "version": "1.0.1",
    "url": "http://server/firmware/v1.0.1.bin",
    "size": 66528,
    "crc32": 0xA1B2C3D4
}

// 服务器 -> 设备: OTA 确认/取消
{
    "cmd": "ota_confirm",
    "accept": true/false
}

// 设备 -> 服务器: OTA 状态上报
{
    "cmd": "ota_status",
    "stage": "downloading",  // checking, downloading, verifying, installing, success, failed
    "progress": 50,          // 0-100
    "error_code": 0,
    "error_msg": ""
}

// 设备 -> 服务器: OTA 完成上报
{
    "cmd": "ota_complete",
    "success": true/false,
    "new_version": "1.0.1"
}
```

#### APP OTA 任务流程
```
┌──────────────┐
│ OTA 任务创建  │
└──────┬───────┘
       │
       ▼
┌──────────────────┐
│ 订阅 OTA 主题   │
└──────┬──────────┘
       │
       ▼
┌──────────────────┐
│ 等待 OTA 命令    │
└──────┬──────────┘
       │
       ├──────────────────────────────────┐
       │                                  │
       ▼                                  ▼
┌─────────────┐                  ┌──────────────┐
│ 收到命令    │                  │ 无命令       │
│ ota_start   │                  │ 继续等待     │
└──────┬──────┘                  └──────────────┘
       │
       ▼
┌──────────────────┐
│ 检查版本是否    │
│ 需要升级        │
└──────┬──────────┘
       │
       ├────────────┐
       │            │
       ▼            ▼
   ┌───────┐    ┌────────┐
   │ 需要   │    │ 已是最新 │
   │ 升级   │    │ 忽略    │
   └───┬───┘    └────────┘
       │
       ▼
┌──────────────────┐
│ 下载固件到      │
│ App-B 区域      │
└──────┬──────────┘
       │
       ▼
┌──────────────────┐
│ 计算 CRC32      │
│ 验证完整性      │
└──────┬──────────┘
       │
       ├──────────────┐
       │              │
       ▼              ▼
   ┌───────┐      ┌────────┐
   │ 验证成功│      │ 验证失败│
   │ 设置   │      │ 上报    │
   │ ota_flag│      │ 错误    │
   │ =PENDING│      │ 完成    │
   └───┬───┘      └────────┘
       │
       ▼
┌──────────────────┐
│ 上报升级成功    │
│ 等待服务器确认  │
└──────┬──────────┘
       │
       ├──────────────────┐
       │                  │
       ▼                  ▼
   ┌──────────┐     ┌───────────┐
   │ 服务器确认│     │ 服务器取消 │
   │ 重启设备 │     │ 清除标志   │
   └──────────┘     └───────────┘
```

---

## 📝 实现计划

### Phase 1: Bootloader OTA 逻辑
- [ ] 完善 `ota_verify_firmware()` 函数
- [ ] 实现 `handle_upgrade()` 升级处理
- [ ] 实现 `handle_rollback()` 回滚处理
- [ ] 添加 OTA 升级日志

### Phase 2: APP OTA 客户端
- [ ] 创建 `ota_client.c` OTA 客户端模块
- [ ] 实现 MQTT OTA 命令处理
- [ ] 实现固件下载功能 (HTTP)
- [ ] 实现固件写入 Flash
- [ ] 实现 CRC32 校验
- [ ] 实现 OTA 状态上报

### Phase 3: 服务器端 (可选)
- [ ] 固件版本管理
- [ ] 固件存储服务
- [ ] OTA 命令下发
- [ ] 升级状态监控

---

## 🔧 关键函数接口

### Bootloader 侧
```c
// OTA 参数管理
ota_params_err_t ota_params_init(void);
ota_params_err_t ota_params_read(ota_params_t *params);
ota_params_err_t ota_params_write(const ota_params_t *params);

// 固件验证
int verify_firmware(uint32_t app);
uint32_t get_app_addr(uint32_t app);

// 升级处理
void handle_upgrade(ota_params_t *params);
void handle_rollback(ota_params_t *params);
void boot_normal(ota_params_t *params);
```

### APP 侧
```c
// OTA 客户端
void ota_client_init(void);
void ota_client_task(void *params);
int ota_check_version(const char *version);
int ota_download_firmware(const char *url, uint32_t size, uint32_t expected_crc);
int ota_write_firmware(uint32_t addr, const uint8_t *data, uint32_t len);
int ota_trigger_upgrade(void);
void ota_report_status(const char *stage, uint8_t progress, int error_code);
```

---

## 📊 固件升级状态机

```
         ┌──────────────────────────────────────────────────────┐
         │                                                      │
         ▼                                                      │
    ┌─────────┐    ┌───────────┐    ┌───────────┐    ┌────────┐ │
    │ CHECKING│───▶│DOWNLOADING│───▶│ VERIFYING │───▶│PENDING │ │
    └─────────┘    └───────────┘    └───────────┘    └────────┘ │
         │                                                      │
         │              ┌──────────────────────┐               │
         └─────────────▶│        ERROR         │◀──────────────┘
                        └──────────────────────┘
                                                         │
                                                         ▼
                                                   ┌────────┐
                                                   │REBOOT  │
                                                   └────────┘
```

---

## 🛡️ 安全考虑

### 1. 固件签名验证 (可选)
```c
typedef struct {
    uint32_t magic;           // 固件魔数
    uint32_t version;         // 版本号
    uint32_t size;           // 固件大小
    uint32_t crc32;          // CRC32 校验
    uint8_t  signature[64];  // RSA 签名
} firmware_header_t;
```

### 2. 安全启动 (Secure Boot)
- Bootloader 验证 APP 签名
- APP 验证新固件签名
- 防止恶意固件注入

### 3. 回滚保护
- 最大连续启动失败次数限制
- 紧急回滚机制

---

## 📡 MQTT OTA 消息示例

### 1. 设备查询最新版本
```json
Topic: device/w5500_001/ota/status
{
    "cmd": "ota_status",
    "stage": "idle",
    "current_version": "1.0.0"
}
```

### 2. 服务器下发升级命令
```json
Topic: device/w5500_001/ota/response
{
    "cmd": "ota_start",
    "version": "1.0.1",
    "url": "http://iot.server/firmware/w5500_v1.0.1.bin",
    "size": 66528,
    "crc32": 0xA1B2C3D4
}
```

### 3. 设备下载进度上报
```json
Topic: device/w5500_001/ota/status
{
    "cmd": "ota_status",
    "stage": "downloading",
    "progress": 45,
    "error_code": 0
}
```

### 4. 设备升级完成
```json
Topic: device/w5500_001/ota/status
{
    "cmd": "ota_complete",
    "success": true,
    "new_version": "1.0.1"
}
```

---

## 🎯 下一步行动

1. **确认服务器端 OTA 接口**
   - MQTT Broker 地址
   - OTA 主题名称
   - 固件下载地址格式

2. **准备测试固件**
   - 生成测试固件 (v1.0.1)
   - 上传到固件服务器
   - 记录 CRC32 值

3. **实现 Bootloader OTA 逻辑**
   - 在 `bootloader_main()` 中添加 OTA 标志检查
   - 实现固件验证
   - 实现升级和回滚逻辑

4. **实现 APP OTA 客户端**
   - 创建 `ota_client.c`
   - 实现 MQTT OTA 命令处理
   - 实现固件下载和写入

5. **端到端测试**
   - 测试正常升级流程
   - 测试回滚机制
   - 测试网络异常处理
