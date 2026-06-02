# OTA 升级测试指南

## 📋 测试准备

### 1. 硬件准备
- STM32F405RG 开发板
- W5500 以太网模块
- J-Link 调试器
- 网线连接到路由器

### 2. 软件准备
- Keil MDK 5.x
- J-Link Software
- MQTT 客户端工具 (MQTTX 或 mosquitto_pub/sub)
- Python 3.x (用于生成测试固件)

---

## 🔧 配置说明

### MQTT Broker 配置
```c
// mqtt_config.h
#define MQTT_BROKER_IP         "47.74.187.120"  // EMQX Broker
#define MQTT_BROKER_PORT       1883
#define MQTT_CLIENT_ID         "stm32_w5500_client"
```

### OTA 主题配置
```c
// mqtt_config.h
#define OTA_DEVICE_ID          "w5500_001"

// 设备订阅 (接收命令)
#define OTA_TOPIC_CMD          "device/w5500_001/ota/cmd"

// 设备发布 (上报状态)
#define OTA_TOPIC_STATUS       "device/w5500_001/ota/status"

// 服务器响应
#define OTA_TOPIC_RESPONSE     "device/w5500_001/ota/response"
```

---

## 📝 测试步骤

### 步骤 1: 编译并烧录 Bootloader

1. 打开 `Bootloader` 工程
2. 编译 (Build → Rebuild all target files)
3. 烧录到 0x08000000:
   ```
   J-Link> loadfile Bootloader.hex 0x08000000
   ```

### 步骤 2: 编译并烧录 APP v1.0.0

1. 打开 `w5500_project` 工程
2. 修改版本号为 `1.0.0`:
   ```c
   // main.c
   #define FIRMWARE_VERSION "1.0.0"
   ```
3. 编译
4. 烧录到 0x0800C000:
   ```
   J-Link> loadfile w5500_project.hex 0x0800C000
   ```

### 步骤 3: 准备新固件 v1.0.1

1. 修改版本号为 `1.0.1`:
   ```c
   // main.c
   #define FIRMWARE_VERSION "1.0.1"
   ```
2. 修改 LED 闪烁频率 (便于区分):
   ```c
   // main.c
   for (int i = 0; i < 5; i++) {
       HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);
       for (volatile uint32_t j = 0; j < 4000000; j++);  // 改为 4000000
       HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
       for (volatile uint32_t j = 0; j < 4000000; j++);  // 改为 4000000
   }
   ```
3. 编译
4. 计算 CRC32 (使用在线工具或本地工具):
   - 在线工具: https://www.lammertbies.nl/comm/info/crc-calculation.html
   - 或使用 7-Zip: 右键文件 → CRC-SHA → CRC-32
   - 或使用命令行工具: `crc32 w5500_project.bin`

   记录:
   - 固件大小: 从编译输出查看 (例如: 66528 bytes)
   - CRC32 值: 例如 0xA1B2C3D4

### 步骤 4: 上传固件到服务器

1. 将 `w5500_project.bin` 上传到 HTTP 服务器:
   ```
   http://your-server/firmware/w5500_v1.0.1.bin
   ```

### 步骤 5: 发送 OTA 升级命令

使用 MQTT 客户端工具发送升级命令:

#### 使用 MQTTX:
1. 连接到 `47.74.187.120:1883`
2. 订阅主题: `device/w5500_001/ota/status`
3. 发布消息到 `device/w5500_001/ota/cmd`:
   ```json
   {
       "cmd": "ota_start",
       "version": "1.0.1",
       "url": "http://your-server/firmware/w5500_v1.0.1.bin",
       "size": 66528,
       "crc32": "0xA1B2C3D4"
   }
   ```

#### 使用 mosquitto_pub:
```bash
mosquitto_pub -h 47.74.187.120 -p 1883 \
  -t "device/w5500_001/ota/cmd" \
  -m '{"cmd":"ota_start","version":"1.0.1","url":"http://your-server/firmware/w5500_v1.0.1.bin","size":66528,"crc32":"0xA1B2C3D4"}'
```

### 步骤 6: 观察升级过程

设备串口输出:
```
[OTA] New version available (current: 1.0.0, new: 1.0.1)
[OTA] Target app: App-B
[OTA] Starting download from http://your-server/firmware/w5500_v1.0.1.bin
[OTA] Expected size: 66528, CRC32: 0xA1B2C3D4
[OTA] Erasing flash sectors...
[OTA] Flash erase complete
[OTA] Downloaded: 6652 / 66528 bytes (10%)
[OTA] Downloaded: 13305 / 66528 bytes (20%)
...
[OTA] Downloaded: 66528 / 66528 bytes (100%)
[OTA] Calculating CRC32...
[OTA] Expected CRC32: 0xA1B2C3D4
[OTA] Calculated CRC32: 0xA1B2C3D4
[OTA: CRC32 verification passed!
[OTA] Upgrade triggered! Will apply on next reboot.
```

### 步骤 7: 设备重启验证

设备自动重启，Bootloader 验证新固件:
```
====================================
Bootloader :V-01-01-00
STM32F405RG - OTA Bootloader
====================================

[BOOT] Reading OTA parameters...
[INFO] OTA params loaded:
       OTA Flag: 1
       Active App: 1
       Boot Count: 0/5
       App-A Valid: 1
       App-B Valid: 1
[INFO] Selected App-B (0x08084000)
[BOOT] Validating firmware...
[BOOT] Valid ARM application found
[INFO] ARM application validated (no header)
[OTA] Upgrade pending, verifying new firmware...
[OTA] Boot count: 1/5
[OTA] Continuing verification...
[BOOT] Jumping to application at 0x08084000...
====================================

=====================================
Product:     W5500_MQTT_Gateway
Version:     v1.0.1
...
```

### 步骤 8: 验证升级成功

连续重启 5 次后，Bootloader 确认升级成功:
```
[OTA] Boot count: 5/5
[OTA] New firmware verified successfully!
[OTA] Upgrade completed!
```

---

## 🔄 回滚测试

### 触发回滚

1. 发送回滚命令:
   ```json
   {
       "cmd": "ota_rollback"
   }
   ```

2. 或者手动设置 OTA 参数:
   ```c
   ota_params_t params;
   params.ota_flag = OTA_FLAG_ROLLBACK;
   ota_params_write(&params);
   ```

3. 重启设备，观察回滚过程:
   ```
   [OTA] Rollback triggered!
   [OTA] Rolling back to App-A...
   [BOOT] Jumping to application at 0x0800C000...
   ```

---

## 📊 测试检查清单

### 基本功能测试
- [ ] Bootloader 正常启动
- [ ] APP v1.0.0 正常运行
- [ ] MQTT 连接成功
- [ ] 订阅 OTA 主题成功

### OTA 升级测试
- [ ] 接收 OTA 命令成功
- [ ] 版本检查正确
- [ ] 固件下载成功
- [ ] CRC32 校验通过
- [ ] OTA 参数写入成功
- [ ] 设备重启
- [ ] Bootloader 验证新固件
- [ ] APP v1.0.1 正常运行
- [ ] 连续 5 次启动验证成功
- [ ] 升级完成标志设置正确

### 回滚测试
- [ ] 触发回滚成功
- [ ] 回滚到 App-A 成功
- [ ] APP v1.0.0 正常运行

### 异常测试
- [ ] 固件下载失败处理
- [ ] CRC32 校验失败处理
- [ ] 网络断开处理
- [ ] Flash 写入失败处理
- [ ] 固件损坏回滚

---

## 🐛 常见问题

### 问题 1: 设备不响应 OTA 命令
**检查**:
1. MQTT 连接是否正常
2. 是否订阅了正确的主题
3. JSON 格式是否正确

### 问题 2: 固件下载失败
**检查**:
1. HTTP 服务器是否可访问
2. 固件 URL 是否正确
3. 固件大小是否超过限制 (480KB)

### 问题 3: CRC32 校验失败
**检查**:
1. 固件是否完整下载
2. CRC32 计算是否正确
3. Flash 写入是否成功

### 问题 4: 升级后无法启动
**检查**:
1. VTOR 是否正确设置
2. 栈指针是否有效
3. Reset Handler 是否有效

---

## 📝 测试日志示例

### 成功的升级日志
```
[2026-05-30 10:00:00] Bootloader started
[2026-05-30 10:00:01] App-A v1.0.0 running
[2026-05-30 10:00:05] MQTT connected
[2026-05-30 10:01:00] OTA command received
[2026-05-30 10:01:01] Downloading firmware...
[2026-05-30 10:01:15] Firmware downloaded (66528 bytes)
[2026-05-30 10:01:16] CRC32 verification passed
[2026-05-30 10:01:17] Upgrade triggered, rebooting...
[2026-05-30 10:01:20] Bootloader started
[2026-05-30 10:01:21] App-B v1.0.1 running (boot count: 1/5)
[2026-05-30 10:01:25] MQTT connected
...
[2026-05-30 10:05:20] Bootloader started
[2026-05-30 10:05:21] App-B v1.0.1 running (boot count: 5/5)
[2026-05-30 10:05:22] Upgrade completed!
```

---

## 🎯 下一步

1. **实现 HTTP 下载功能**
   - 集成 HTTP 客户端库
   - 实现分块下载
   - 实现断点续传

2. **完善 MQTT 命令处理**
   - 解析 JSON 命令
   - 实现命令回调
   - 实现状态上报

3. **添加安全验证**
   - 固件签名验证
   - 设备认证
   - 加密传输

4. **优化用户体验**
   - 升级进度 LED 指示
   - 升级失败自动重试
   - 远程日志上报
