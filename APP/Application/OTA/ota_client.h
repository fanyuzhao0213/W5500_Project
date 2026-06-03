/**
 * @file ota_client.h
 * @brief OTA 客户端接口定义
 *
 * 主要功能：
 * 1. 版本检查
 * 2. 固件下载
 * 3. 固件验证
 * 4. 升级触发
 */

#ifndef __OTA_CLIENT_H
#define __OTA_CLIENT_H

#include "stdint.h"
#include "ota_config.h"
#include "cmsis_os.h"

/* OTA 任务定义 */
#define OTA_TASK_PRIORITY       osPriorityHigh
#define OTA_TASK_STACK          8192   /* 需要支持 8KB Base64 + 4KB Binary 局部变量 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA 升级阶段枚举
 */
typedef enum {
    OTA_STAGE_IDLE = 0,         // 空闲状态
    OTA_STAGE_PREPARING,        // 准备中（擦除Flash等准备工作）
    OTA_STAGE_DOWNLOADING,      // 下载中
    OTA_STAGE_VERIFYING,        // 验证中（CRC32 校验）
    OTA_STAGE_INSTALLING,       // 安装中（设置升级标志）
    OTA_STAGE_SUCCESS,          // 升级成功
    OTA_STAGE_FAILED            // 升级失败
} ota_stage_t;

/**
 * @brief OTA 错误码枚举
 */
typedef enum {
    OTA_ERR_OK = 0,                 // 成功
    OTA_ERR_INVALID_PARAM,          // 参数无效
    OTA_ERR_NO_MEMORY,              // 内存不足
    OTA_ERR_FLASH_WRITE,            // Flash 写入失败
    OTA_ERR_FLASH_ERASE,            // Flash 擦除失败
    OTA_ERR_CRC_MISMATCH,           // CRC32 校验失败
    OTA_ERR_DOWNLOAD_FAILED,        // 下载失败
    OTA_ERR_INVALID_FIRMWARE,       // 固件无效
    OTA_ERR_ALREADY_LATEST,         // 已是最新版本
    OTA_ERR_NETWORK_ERROR           // 网络错误
} ota_error_t;

/**
 * @brief 固件信息结构体
 */
typedef struct {
    char version[32];       // 版本号（如 "1.0.1"）
    char url[256];          // 下载地址（HTTP URL）
    uint32_t size;          // 固件大小（字节）
    uint32_t crc32;         // CRC32 校验值
} ota_firmware_info_t;

/**
 * @brief OTA 状态结构体
 */
typedef struct {
    ota_stage_t stage;          // 当前阶段
    uint8_t progress;           // 进度（0-100）
    ota_error_t error_code;     // 错误码
    char error_msg[64];         // 错误信息
    uint32_t downloaded_size;   // 已下载大小（字节）
    uint32_t total_size;        // 总大小（字节）
} ota_status_t;

/**
 * @brief 进度回调函数类型
 * @param status OTA 状态指针
 */
typedef void (*ota_progress_callback_t)(ota_status_t *status);

/* ==================== 函数接口 ==================== */

/**
 * @brief OTA 客户端初始化
 */
void ota_client_init(void);

/**
 * @brief OTA 客户端任务
 * @param argument 任务参数（未使用）
 */
void ota_client_task(void const *argument);

/**
 * @brief 创建 OTA 客户端任务
 */
void ota_task_create(void);

/**
 * @brief 检查版本是否需要升级
 * @param new_version 新版本号字符串
 * @return OTA_ERR_OK - 需要升级, OTA_ERR_ALREADY_LATEST - 已是最新版本
 */
int ota_check_version(const char *new_version);

/**
 * @brief 开始 OTA 升级流程
 * @param info 固件信息（版本、URL、大小、CRC32）
 * @return 错误码
 */
int ota_start_upgrade(const ota_firmware_info_t *info);

/**
 * @brief 下载固件并擦除 Flash
 * @param url 固件下载地址
 * @param size 固件大小
 * @param expected_crc 预期的 CRC32 值
 * @return 错误码
 */
int ota_download_firmware(const char *url, uint32_t size, uint32_t expected_crc);

/**
 * @brief 写入固件数据块
 * @param offset 写入偏移量
 * @param data 数据指针
 * @param len 数据长度
 * @return 错误码
 */
int ota_write_firmware_chunk(uint32_t offset, const uint8_t *data, uint32_t len);

/**
 * @brief 验证固件完整性
 * @param expected_crc 预期的 CRC32 值
 * @return 错误码
 */
int ota_verify_firmware(uint32_t expected_crc);

/**
 * @brief 触发升级 - 设置升级标志并准备重启
 * @return 错误码
 */
int ota_trigger_upgrade(void);

/**
 * @brief 取消升级
 */
void ota_cancel_upgrade(void);

/**
 * @brief 获取 OTA 状态
 * @param status 状态输出指针
 */
void ota_get_status(ota_status_t *status);

/**
 * @brief 设置进度回调函数
 * @param callback 回调函数指针
 */
void ota_set_progress_callback(ota_progress_callback_t callback);

/**
 * @brief 上报 OTA 状态
 */
void ota_report_status(void);

/**
 * @brief 解析 OTA 命令并执行相应操作
 * @param payload JSON 格式的命令字符串
 *
 * 支持的命令：
 * - ota_start: 开始升级
 * - ota_cancel: 取消升级
 * - ota_status: 查询状态
 * - ota_rollback: 触发回滚
 */
void ota_parse_command(const char *payload);

/**
 * @brief 接收并处理固件数据块（分包传输）
 * @param payload JSON 格式的数据块消息
 * @param payload_len 消息长度
 *
 * JSON 格式：
 * {
 *   "index": 0,           // 数据块索引（从 0 开始）
 *   "total": 17,          // 总块数
 *   "size": 4096,         // 本块数据大小
 *   "data": "base64..."   // Base64 编码的数据
 * }
 */
void ota_receive_data_chunk(const char *payload, int payload_len);

#ifdef __cplusplus
}
#endif

#endif
