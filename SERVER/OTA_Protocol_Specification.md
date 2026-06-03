# OTA 升级通信协议文档

## 1. 概述

### 1.1 协议版本
- 文档版本：V1.0
- 创建日期：2026-06-02

### 1.2 主题命名规范
```
设备订阅主题：
  - {device_id}/ota/cmd      : 接收服务器 OTA 命令
  - {device_id}/ota/data     : 接收固件数据块（分包传输）

设备发布主题：
  - {device_id}/ota/status   : 上报 OTA 状态和进度
  - {device_id}/ota/ack      : 发送数据块接收确认
  - {device_id}/ota/response : 通用响应（保留）
```

### 1.3 设备标识
- `{device_id}` : 设备唯一标识，格式如 `w5500_001`

---

## 2. 数据结构定义

### 2.1 MQTT 消息结构
```json
{
  "topic": "string",
  "payload": "string (JSON)"
}
```

### 2.2 通用状态字段
所有状态上报消息都包含以下字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| stage | int | 当前阶段 (见 2.3) |
| progress | int | 进度百分比 (0-100) |
| error | int | 错误码 (0=正常) |
| downloaded | int | 已下载字节数 |
| total | int | 总字节数 |
| version | string | 当前固件版本 |

### 2.3 OTA 阶段定义 (stage)

| 值 | 阶段 | 说明 |
|----|------|------|
| 0 | IDLE | 空闲状态，无升级任务 |
| 1 | CHECKING | 正在检查版本和参数 |
| 2 | DOWNLOADING | 正在下载固件 |
| 3 | VERIFYING | 正在验证固件 CRC32 |
| 4 | INSTALLING | 正在安装固件 |
| 5 | SUCCESS | 升级成功 |
| 6 | FAILED | 升级失败 |

### 2.4 错误码定义 (error)

| 值 | 错误类型 | 说明 |
|----|----------|------|
| 0 | OK | 正常，无错误 |
| 1 | INVALID_PARAM | 参数无效 |
| 2 | NO_MEMORY | 内存不足 |
| 3 | FLASH_WRITE | Flash 写入失败 |
| 4 | FLASH_ERASE | Flash 擦除失败 |
| 5 | CRC_MISMATCH | CRC32 校验失败 |
| 6 | DOWNLOAD_FAILED | 下载失败 |
| 7 | INVALID_FIRMWARE | 固件无效 |
| 8 | ALREADY_LATEST | 已是最新版本 |
| 9 | NETWORK_ERROR | 网络错误 |

---

## 3. 正常升级流程

### 3.1 流程概览
```
服务器                                    设备
   |                                        |
   |-------- ota_start 命令 ---------------->| 开始 OTA
   |<------- ota_status (stage=1) ---------| 准备就绪
   |                                        |
   |-------- ota_data (chunk 0) ---------->|
   |<------- ota_ack (index=0, success) ---| ACK 确认
   |                                        |
   |-------- ota_data (chunk 1) ---------->|
   |<------- ota_ack (index=1, success) ---| ACK 确认
   |                                        |
   |                ...                     |
   |                                        |
   |-------- ota_data (chunk N) ---------->|
   |<------- ota_ack (index=N, success) ---| ACK 确认
   |                                        |
   |<------- ota_status (stage=3) ---------| 验证中
   |<------- ota_status (stage=4) ---------| 安装中
   |<------- ota_status (stage=5) ---------| 成功
   |                                        |
   |              重启设备                   |
   |                                        |
```

### 3.2 详细命令格式

#### 3.2.1 OTA 开始命令 (服务器 → 设备)

**主题**: `{device_id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_start",
  "version": "1.0.2",
  "size": 82832,
  "crc32": 3686533208,
  "chunks": 21
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| cmd | string | 是 | 命令类型: "ota_start" |
| version | string | 是 | 新固件版本号，如 "1.0.2" |
| size | number | 是 | 固件总大小（字节） |
| crc32 | number | 否 | CRC32 校验值（如果为0则跳过验证） |
| chunks | number | 是 | 分包数量 |

---

#### 3.2.2 OTA 数据块 (服务器 → 设备)

**主题**: `{device_id}/ota/data`

**Payload**:
```json
{
  "index": 0,
  "total": 21,
  "size": 4096,
  "data": "base64encodedbinarydata..."
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| index | number | 是 | 数据块索引 (从 0 开始) |
| total | number | 是 | 总块数 |
| size | number | 是 | 本块数据实际字节数 |
| data | string | 是 | Base64 编码的二进制数据 |

---

#### 3.2.3 OTA ACK 确认 (设备 → 服务器)

**主题**: `{device_id}/ota/ack`

**Payload**:
```json
{
  "index": 0,
  "success": true
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| index | number | 是 | 数据块索引 |
| success | boolean | 是 | true=成功, false=失败 |

---

#### 3.2.4 OTA 状态上报 (设备 → 服务器)

**主题**: `{device_id}/ota/status`

**Payload**:
```json
{
  "stage": 2,
  "progress": 50,
  "error": 0,
  "downloaded": 41416,
  "total": 82832,
  "version": "1.0.0"
}
```

---

#### 3.2.5 OTA 取消命令 (服务器 → 设备)

**主题**: `{device_id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_cancel"
}
```

---

#### 3.2.6 OTA 状态查询命令 (服务器 → 设备)

**主题**: `{device_id}/ota/cmd`

**Payload**:
```json
{
  "cmd": "ota_status"
}
```

---

## 4. 异常处理流程

### 4.1 设备端异常

#### 4.1.1 Flash 写入失败
```
设备接收数据块后写入 Flash 失败
  -> 发送 ACK: {"index": X, "success": false}
  -> 上报状态: {"stage": 6, "error": 3}
  -> 等待服务器重试或取消
```

#### 4.1.2 CRC32 校验失败
```
设备接收完所有数据块后验证 CRC 失败
  -> 上报状态: {"stage": 6, "error": 5}
  -> 等待服务器重发固件
```

#### 4.1.3 参数解析失败
```
设备无法解析命令 JSON
  -> 上报状态: {"stage": 6, "error": 1}
```

---

### 4.2 服务器端异常

#### 4.2.1 设备未响应 ACK
```
服务器发送数据块后 5 秒内未收到 ACK
  -> 重试发送同一数据块 (最多 3 次)
  -> 3 次失败后终止升级
```

#### 4.2.2 设备报告失败
```
服务器收到 success=false 的 ACK
  -> 重试发送同一数据块 (最多 3 次)
  -> 3 次失败后终止升级
```

---

## 5. 完整交互时序图

### 5.1 成功升级流程
```
客户端                          服务器                            设备
   |                              |                                |
   |                              |--- ota_start ------------------>|
   |                              |                                |-- 检查版本
   |                              |                                |-- 擦除 Flash
   |                              |<-- ota_status (stage=1) ------|
   |                              |                                |
   |                              |--- ota_data[0] -------------->|
   |                              |                                |-- 写入 Flash
   |                              |<-- ota_ack[0] ----------------|
   |                              |                                |
   |                              |--- ota_data[1] -------------->|
   |                              |                                |-- 写入 Flash
   |                              |<-- ota_ack[1] ----------------|
   |                              |                                |
   |                              |         ...                    |
   |                              |                                |
   |                              |--- ota_data[N] -------------->|
   |                              |                                |-- 写入 Flash
   |                              |<-- ota_ack[N] ----------------|
   |                              |                                |-- 验证 CRC32
   |                              |<-- ota_status (stage=3) ------|
   |                              |                                |
   |                              |<-- ota_status (stage=5) ------|  升级成功
   |                              |                                |
   |                              |       重启设备                  |
```

---

## 6. JSON 格式说明

### 6.1 字段命名规范
- 所有字段名使用 **小写字母 + 下划线**
- 冒号后可以有空格也可以没有空格，设备端需兼容两种格式

### 6.2 兼容格式示例
```json
// 格式1：无空格
{"cmd":"ota_start","version":"1.0.2"}

// 格式2：有空格
{"cmd": "ota_start", "version": "1.0.2"}

// 设备端解析器应同时支持上述两种格式
```

### 6.3 Base64 编码说明
- 固件数据块使用 **Base64 编码**传输
- 每帧数据大小建议 4KB (4096 bytes)
- Base64 编码后约增加 33% 长度

---

## 7. 服务器实现建议

### 7.1 状态机设计
```c
typedef enum {
    OTA_STATE_IDLE = 0,           // 空闲
    OTA_STATE_START_SENT,         // 开始命令已发送
    OTA_STATE_DOWNLOADING,        // 正在下载
    OTA_STATE_VERIFYING,          // 正在验证
    OTA_STATE_SUCCESS,            // 成功
    OTA_STATE_FAILED              // 失败
} ota_server_state_t;
```

### 7.2 关键检查点

#### 7.2.1 开始升级前检查
```
1. 设备当前版本 (version 字段)
2. 设备状态 (stage=0 表示空闲)
3. 设备错误码 (error=0 表示正常)
```

#### 7.2.2 继续下载检查
```
1. 设备状态必须是 stage=1 (CHECKING) 或 stage=2 (DOWNLOADING)
2. 设备错误码必须为 0
3. progress 应该递增
```

#### 7.2.3 完成下载检查
```
1. 等待设备 stage=3 (VERIFYING)
2. 等待设备 stage=5 (SUCCESS) 或 stage=6 (FAILED)
3. 如果 stage=6，检查 error 字段判断失败原因
```

### 7.3 超时处理
```
- 发送命令后 10 秒内未收到响应 -> 重试 (最多 3 次)
- 发送数据块后 5 秒内未收到 ACK -> 重试 (最多 3 次)
- 整个升级流程超时 5 分钟 -> 终止升级
```

---

## 8. 设备端实现建议

### 8.1 错误处理
```
- 所有函数必须设置 g_ota_status.error_code
- 失败时必须上报 ota_status (stage=6, error=X)
- 重要操作前必须先报告当前状态
```

### 8.2 状态转换规则
```
1. 只有在 stage=IDLE 时才能接受 ota_start
2. 下载过程中收到 ota_cancel 必须停止并清理
3. 任何阶段失败都要设置 stage=FAILED 并报告
```

### 8.3 内存管理
```
- 静态分配消息缓冲区，禁止使用 malloc
- 消息队列大小建议 10 条
- 固件数据块直接写入 Flash，不做内存缓冲
```

---

## 9. 版本兼容性

### 9.1 协议版本
- 当前协议版本: 1.0
- 后续升级应保持向后兼容

### 9.2 字段兼容性
- 新增字段应该是可选的，不影响旧版本解析
- 不使用的字段可以省略
- 不应删除已有的字段

---

## 10. 附录

### 10.1 完整错误码列表

| 值 | 常量 | 说明 |
|----|------|------|
| 0 | OTA_ERR_OK | 成功 |
| 1 | OTA_ERR_INVALID_PARAM | 参数无效 |
| 2 | OTA_ERR_NO_MEMORY | 内存不足 |
| 3 | OTA_ERR_FLASH_WRITE | Flash 写入失败 |
| 4 | OTA_ERR_FLASH_ERASE | Flash 擦除失败 |
| 5 | OTA_ERR_CRC_MISMATCH | CRC32 校验失败 |
| 6 | OTA_ERR_DOWNLOAD_FAILED | 下载失败 |
| 7 | OTA_ERR_INVALID_FIRMWARE | 固件无效 |
| 8 | OTA_ERR_ALREADY_LATEST | 已是最新版本 |
| 9 | OTA_ERR_NETWORK_ERROR | 网络错误 |

### 10.2 完整阶段列表

| 值 | 阶段 | 说明 |
|----|------|------|
| 0 | OTA_STAGE_IDLE | 空闲状态 |
| 1 | OTA_STAGE_CHECKING | 正在检查 |
| 2 | OTA_STAGE_DOWNLOADING | 正在下载 |
| 3 | OTA_STAGE_VERIFYING | 正在验证 |
| 4 | OTA_STAGE_INSTALLING | 正在安装 |
| 5 | OTA_STAGE_SUCCESS | 升级成功 |
| 6 | OTA_STAGE_FAILED | 升级失败 |

---

## 11. 更新记录

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| 1.0 | 2026-06-02 | 初始版本 |