/**
 * @file ota_client.c
 * @brief OTA 客户端实现 - 负责固件下载、验证和升级触发
 *
 * 主要功能：
 * 1. 版本检查 - 判断是否需要升级
 * 2. 固件下载 - 从服务器下载新固件到备份分区
 * 3. 固件验证 - CRC32 校验确保固件完整性
 * 4. 升级触发 - 设置升级标志，等待重启
 * 5. 命令解析 - 解析 MQTT 下发的 OTA 命令
 * 6. 状态上报 - 通过 MQTT 上报升级进度和状态
 */

#include "ota_client.h"
#include "flash_driver.h"
#include "ota_params.h"
#include "mqtt_config.h"
#include "mqtt_client.h"
#include "mqtt_queue.h"
#include "mqtt_task.h"
#include "cmsis_os.h"
#include "LOG.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== 全局变量 ==================== */
static ota_status_t g_ota_status;                    // OTA 状态信息
static ota_firmware_info_t g_firmware_info;          // 固件信息
static ota_progress_callback_t g_progress_callback = NULL;  // 进度回调函数
static uint32_t g_target_app = APP_B;                // 目标升级分区
static char g_current_version[32] = "1.0.0";         // 当前版本号

/**
 * @brief OTA 客户端初始化
 *
 * 功能：
 * 1. 初始化 OTA 状态
 * 2. 从 Flash 读取当前版本号
 */
void ota_client_init(void)
{
    LOGI("ota_client_init: started");

    memset(&g_ota_status, 0, sizeof(ota_status_t));
    memset(&g_firmware_info, 0, sizeof(ota_firmware_info_t));
    g_ota_status.stage = OTA_STAGE_IDLE;
    g_progress_callback = NULL;

    LOGI("ota_client_init: before ota_params_read");

    // 从 OTA 参数区读取当前版本号
    ota_params_t params;
    if (ota_params_read(&params) == OTA_PARAMS_OK) {
        snprintf(g_current_version, sizeof(g_current_version), "%d", params.app_a_version);
        LOGI("ota_client_init: ota_params_read OK, version=%d", params.app_a_version);
    } else {
        LOGI("ota_client_init: ota_params_read failed, using default version");
    }

    LOGI("OTA Client initialized, current version: %s", g_current_version);
}

/**
 * @brief 检查版本是否需要升级
 * @param new_version 新版本号字符串
 * @return OTA_ERR_OK - 需要升级, OTA_ERR_ALREADY_LATEST - 已是最新版本
 *
 * 逻辑：
 * 1. 解析版本号（数字格式，如 "1.0.1" -> 101）
 * 2. 比较版本号大小
 */
int ota_check_version(const char *new_version)
{
    if (new_version == NULL) {
        return OTA_ERR_INVALID_PARAM;
    }

    uint32_t current_ver = 0;
    uint32_t new_ver = 0;

    // 解析版本号（简化处理：直接转换为数字）
    sscanf(g_current_version, "%lu", &current_ver);
    sscanf(new_version, "%lu", &new_ver);

    // 版本比较：新版本号必须大于当前版本号
    if (new_ver <= current_ver) {
        LOGI("OTA: Already latest version (current: %lu, new: %lu)", current_ver, new_ver);
        return OTA_ERR_ALREADY_LATEST;
    }

    LOGI("OTA: New version available (current: %lu, new: %lu)", current_ver, new_ver);
    return OTA_ERR_OK;
}

/**
 * @brief 开始 OTA 升级流程
 * @param info 固件信息（版本、URL、大小、CRC32）
 * @return 错误码
 *
 * 流程：
 * 1. 参数校验（固件大小、版本检查）
 * 2. 确定目标分区（App-A 或 App-B）
 * 3. 初始化 OTA 状态
 */
int ota_start_upgrade(const ota_firmware_info_t *info)
{
    if (info == NULL) {
        return OTA_ERR_INVALID_PARAM;
    }

    // 检查固件大小是否超过分区限制
    if (info->size > OTA_FIRMWARE_MAX_SIZE) {
        LOGE("OTA: Firmware too large (%d > %d)", info->size, OTA_FIRMWARE_MAX_SIZE);
        return OTA_ERR_INVALID_FIRMWARE;
    }

    // 检查版本是否需要升级
    int ret = ota_check_version(info->version);
    if (ret == OTA_ERR_ALREADY_LATEST) {
        return ret;
    }

    // 保存固件信息
    memcpy(&g_firmware_info, info, sizeof(ota_firmware_info_t));

    // 读取当前激活的分区，确定目标升级分区
    ota_params_t params;
    if (ota_params_read(&params) != OTA_PARAMS_OK) {
        LOGE("OTA: Failed to read params");
        return OTA_ERR_FLASH_WRITE;
    }

    // 目标分区 = 当前运行分区的备份分区
    // 例如：当前运行 App-A，则升级到 App-B
    g_target_app = (params.active_app == APP_A) ? APP_B : APP_A;
    LOGI("OTA: Target app: %s", g_target_app == APP_A ? "App-A" : "App-B");

    // 初始化 OTA 状态
    g_ota_status.stage = OTA_STAGE_CHECKING;
    g_ota_status.progress = 0;
    g_ota_status.error_code = OTA_ERR_OK;
    g_ota_status.downloaded_size = 0;
    g_ota_status.total_size = info->size;

    ota_report_status();

    return OTA_ERR_OK;
}

/**
 * @brief 下载固件并擦除 Flash
 * @param url 固件下载地址
 * @param size 固件大小
 * @param expected_crc 预期的 CRC32 值
 * @return 错误码
 *
 * 流程：
 * 1. 设置下载状态
 * 2. 解锁 Flash
 * 3. 擦除目标分区
 * 4. 准备写入
 */
int ota_download_firmware(const char *url, uint32_t size, uint32_t expected_crc)
{
    // 设置下载状态
    g_ota_status.stage = OTA_STAGE_DOWNLOADING;
    g_ota_status.progress = 0;
    g_ota_status.downloaded_size = 0;
    g_ota_status.total_size = size;

    LOGI("OTA: Starting download from %s", url);
    LOGI("OTA: Expected size: %lu, CRC32: 0x%08lX", size, expected_crc);

    // 获取目标分区地址
    uint32_t target_addr = (g_target_app == APP_A) ? APP_A_ADDR : APP_B_ADDR;

    // 解锁 Flash
    flash_err_t err = flash_unlock();
    if (err != FLASH_OK) {
        LOGE("OTA: Failed to unlock flash");
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_FLASH_WRITE;
    }

    // 擦除目标分区的 Flash 扇区
    LOGI("OTA: Erasing flash sectors...");
    err = flash_erase_range(target_addr, size);
    if (err != FLASH_OK) {
        LOGE("OTA: Failed to erase flash");
        flash_lock();
        g_ota_status.error_code = OTA_ERR_FLASH_ERASE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_FLASH_ERASE;
    }

    flash_lock();
    LOGI("OTA: Flash erase complete");

    return OTA_ERR_OK;
}

/**
 * @brief 写入固件数据块
 * @param offset 写入偏移量
 * @param data 数据指针
 * @param len 数据长度
 * @return 错误码
 *
 * 流程：
 * 1. 计算写入地址
 * 2. 解锁 Flash
 * 3. 写入数据
 * 4. 更新进度
 * 5. 上报状态
 */
int ota_write_firmware_chunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return OTA_ERR_INVALID_PARAM;
    }

    // 计算写入地址
    uint32_t target_addr = (g_target_app == APP_A) ? APP_A_ADDR : APP_B_ADDR;
    uint32_t write_addr = target_addr + offset;

    // 解锁 Flash
    flash_err_t err = flash_unlock();
    if (err != FLASH_OK) {
        LOGE("OTA: Failed to unlock flash for writing");
        return OTA_ERR_FLASH_WRITE;
    }

    // 写入数据到 Flash
    err = flash_write(write_addr, data, len);
    flash_lock();

    if (err != FLASH_OK) {
        LOGE("OTA: Failed to write flash at 0x%08lX", write_addr);
        return OTA_ERR_FLASH_WRITE;
    }

    // 更新下载进度
    g_ota_status.downloaded_size += len;
    g_ota_status.progress = (g_ota_status.downloaded_size * 100) / g_ota_status.total_size;

    // 调用进度回调函数（如果有）
    if (g_progress_callback != NULL) {
        g_progress_callback(&g_ota_status);
    }

    // 每 10% 上报一次状态
    if (g_ota_status.progress % OTA_PROGRESS_INTERVAL == 0) {
        ota_report_status();
    }

    return OTA_ERR_OK;
}

/**
 * @brief 验证固件完整性
 * @param expected_crc 预期的 CRC32 值
 * @return 错误码
 *
 * 流程：
 * 1. 设置验证状态
 * 2. 计算固件 CRC32
 * 3. 比对 CRC32 值
 */
int ota_verify_firmware(uint32_t expected_crc)
{
    // 设置验证状态
    g_ota_status.stage = OTA_STAGE_VERIFYING;
    g_ota_status.progress = 95;
    ota_report_status();

    // 获取目标分区地址
    uint32_t target_addr = (g_target_app == APP_A) ? APP_A_ADDR : APP_B_ADDR;

    // 计算固件的 CRC32
    LOGI("OTA: Calculating CRC32...");
    uint32_t calculated_crc = flash_calc_crc32(target_addr, g_ota_status.total_size);

    LOGI("OTA: Expected CRC32: 0x%08lX", expected_crc);
    LOGI("OTA: Calculated CRC32: 0x%08lX", calculated_crc);

    // 比对 CRC32 值
    if (calculated_crc != expected_crc) {
        LOGE("OTA: CRC32 mismatch!");
        g_ota_status.error_code = OTA_ERR_CRC_MISMATCH;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_CRC_MISMATCH;
    }

    LOGI("OTA: CRC32 verification passed!");
    return OTA_ERR_OK;
}

/**
 * @brief 触发升级 - 设置升级标志并准备重启
 * @return 错误码
 *
 * 流程：
 * 1. 设置安装状态
 * 2. 读取 OTA 参数
 * 3. 设置 ota_flag = PENDING
 * 4. 更新固件信息（版本、大小、CRC32）
 * 5. 写入 OTA 参数
 * 6. 设置成功状态
 */
int ota_trigger_upgrade(void)
{
    // 设置安装状态
    g_ota_status.stage = OTA_STAGE_INSTALLING;
    g_ota_status.progress = 98;
    ota_report_status();

    // 读取 OTA 参数
    ota_params_t params;
    if (ota_params_read(&params) != OTA_PARAMS_OK) {
        LOGE("OTA: Failed to read params");
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_FLASH_WRITE;
    }

    // 设置升级标志为 PENDING（待升级）
    params.ota_flag = OTA_FLAG_PENDING;
    params.boot_count = 0;  // 重置启动计数

    // 解析版本号
    uint32_t version = 0;
    sscanf(g_firmware_info.version, "%lu", &version);

    // 更新目标分区的固件信息
    if (g_target_app == APP_A) {
        params.app_a_version = version;
        params.app_a_size = g_ota_status.total_size;
        params.app_a_crc32 = g_firmware_info.crc32;
        params.app_a_valid = 1;  // 标记为有效
    } else {
        params.app_b_version = version;
        params.app_b_size = g_ota_status.total_size;
        params.app_b_crc32 = g_firmware_info.crc32;
        params.app_b_valid = 1;  // 标记为有效
    }

    // 写入 OTA 参数到 Flash
    if (ota_params_write(&params) != OTA_PARAMS_OK) {
        LOGE("OTA: Failed to write params");
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_FLASH_WRITE;
    }

    // 设置成功状态
    g_ota_status.stage = OTA_STAGE_SUCCESS;
    g_ota_status.progress = 100;
    ota_report_status();

    LOGI("OTA: Upgrade triggered! Will apply on next reboot.");

    return OTA_ERR_OK;
}

/**
 * @brief 取消升级
 *
 * 功能：
 * 1. 清空固件信息
 * 2. 重置 OTA 状态
 */
void ota_cancel_upgrade(void)
{
    LOGW("OTA: Cancelling upgrade...");

    memset(&g_firmware_info, 0, sizeof(ota_firmware_info_t));
    g_ota_status.stage = OTA_STAGE_IDLE;
    g_ota_status.progress = 0;
    g_ota_status.error_code = OTA_ERR_OK;
    g_ota_status.downloaded_size = 0;
    g_ota_status.total_size = 0;

    ota_report_status();
}

/**
 * @brief 获取 OTA 状态
 * @param status 状态输出指针
 */
void ota_get_status(ota_status_t *status)
{
    if (status != NULL) {
        memcpy(status, &g_ota_status, sizeof(ota_status_t));
    }
}

/**
 * @brief 设置进度回调函数
 * @param callback 回调函数指针
 */
void ota_set_progress_callback(ota_progress_callback_t callback)
{
    g_progress_callback = callback;
}

/**
 * @brief 上报 OTA 状态
 *
 * 功能：
 * 1. 构建 JSON 格式的状态消息
 * 2. 通过 MQTT 发送队列发布到 OTA_TOPIC_STATUS 主题
 */
void ota_report_status(void)
{
    mqtt_msg_t pub_msg;
    int len;

    // 初始化消息结构
    memset(&pub_msg, 0, sizeof(pub_msg));

    // 构建 JSON 格式的状态消息
    // 使用 g_current_version 而不是 g_firmware_info.version
    // g_firmware_info.version 只在升级过程中有效
    len = snprintf(pub_msg.payload, sizeof(pub_msg.payload),
        "{\"stage\":%d,\"progress\":%d,\"error\":%d,\"downloaded\":%d,\"total\":%d,\"version\":\"%s\"}",
        g_ota_status.stage,
        g_ota_status.progress,
        g_ota_status.error_code,
        g_ota_status.downloaded_size,
        g_ota_status.total_size,
        g_current_version);

    // 设置主题
    snprintf(pub_msg.topic, sizeof(pub_msg.topic), "%s", OTA_TOPIC_STATUS);
    pub_msg.len = strlen(pub_msg.payload);
    pub_msg.qos = QOS1;

    // 放入发送队列
    if (mqtt_tx_queue_put(&pub_msg, 0) == 0) {
        LOGI("OTA: Status queued - stage=%d, progress=%d%%",
             g_ota_status.stage, g_ota_status.progress);
    } else {
        LOGE("OTA: Failed to queue status");
    }
}

/**
 * @brief 解析 JSON 字符串中的字段值
 * @param json JSON 字符串
 * @param field 字段名
 * @param value 输出缓冲区
 * @param max_len 最大长度
 * @return 0 成功, -1 失败
 */
static int parse_json_string(const char *json, const char *field, char *value, int max_len)
{
    char search_str[64];
    char *start, *end;
    int len;

    // 构建搜索字符串："field":"
    snprintf(search_str, sizeof(search_str), "\"%s\":\"", field);

    // 查找字段
    start = strstr(json, search_str);
    if (start == NULL) {
        return -1;
    }

    // 跳过 "field":"
    start += strlen(search_str);

    // 查找结束引号
    end = strchr(start, '"');
    if (end == NULL) {
        return -1;
    }

    // 复制值
    len = end - start;
    if (len >= max_len) {
        len = max_len - 1;
    }
    memcpy(value, start, len);
    value[len] = '\0';

    return 0;
}

/**
 * @brief 解析 JSON 数字字段
 * @param json JSON 字符串
 * @param field 字段名
 * @return 字段值（未找到返回 0）
 */
static uint32_t parse_json_number(const char *json, const char *field)
{
    char search_str[64];
    char *start;

    // 构建搜索字符串："field":
    snprintf(search_str, sizeof(search_str), "\"%s\":", field);

    // 查找字段
    start = strstr(json, search_str);
    if (start == NULL) {
        return 0;
    }

    // 跳过 "field":
    start += strlen(search_str);

    // 跳过空格
    while (*start == ' ') start++;

    // 转换为数字
    return (uint32_t)strtoul(start, NULL, 10);
}

/**
 * @brief 解析 OTA 命令并执行相应操作
 * @param payload JSON 格式的命令字符串
 *
 * 支持的命令格式：
 * 1. 开始升级：
 *    {"cmd":"ota_start","version":"1.0.1","url":"http://xxx/firmware.bin","size":66528,"crc32":12345678}
 *
 * 2. 取消升级：
 *    {"cmd":"ota_cancel"}
 *
 * 3. 查询状态：
 *    {"cmd":"ota_status"}
 *
 * 4. 触发回滚：
 *    {"cmd":"ota_rollback"}
 */
void ota_parse_command(const char *payload)
{
    char cmd[32];
    int ret;

    if (payload == NULL) {
        LOGE("OTA: Invalid command payload (NULL)");
        return;
    }

    LOGI("OTA: Parsing command: %s", payload);

    // 解析命令字段
    if (parse_json_string(payload, "cmd", cmd, sizeof(cmd)) != 0) {
        LOGE("OTA: Failed to parse 'cmd' field");
        return;
    }

    LOGI("OTA: Command received: %s", cmd);

    // 处理不同的命令
    if (strcmp(cmd, "ota_start") == 0) {
        // 开始 OTA 升级
        ota_firmware_info_t info = {0};

        // 解析固件信息
        if (parse_json_string(payload, "version", info.version, sizeof(info.version)) != 0) {
            LOGE("OTA: Missing 'version' field");
            g_ota_status.error_code = OTA_ERR_INVALID_PARAM;
            g_ota_status.stage = OTA_STAGE_FAILED;
            ota_report_status();
            return;
        }

        info.size = parse_json_number(payload, "size");
        if (info.size == 0) {
            LOGE("OTA: Missing or invalid 'size' field");
            g_ota_status.error_code = OTA_ERR_INVALID_PARAM;
            g_ota_status.stage = OTA_STAGE_FAILED;
            ota_report_status();
            return;
        }

        info.crc32 = parse_json_number(payload, "crc32");
        if (info.crc32 == 0) {
            LOGW("OTA: Missing 'crc32' field, verification will be skipped");
        }

        LOGI("OTA: Firmware info - version=%s, size=%d, crc32=0x%08d",
             info.version, info.size, info.crc32);

        // 开始升级（准备接收分包数据）
        ret = ota_start_upgrade(&info);
        if (ret != OTA_ERR_OK) {
            LOGE("OTA: Failed to start upgrade (error: %d)", ret);
            ota_report_status();
            return;
        }

        // 擦除目标分区Flash
        ret = ota_download_firmware(NULL, info.size, info.crc32);
        if (ret != OTA_ERR_OK) {
            LOGE("OTA: Failed to prepare flash for download (error: %d)", ret);
            g_ota_status.error_code = OTA_ERR_FLASH_ERASE;
            g_ota_status.stage = OTA_STAGE_FAILED;
            ota_report_status();
            return;
        }

        // 设置为下载状态，等待接收数据块
        g_ota_status.stage = OTA_STAGE_DOWNLOADING;
        g_ota_status.progress = 0;
        ota_report_status();

        LOGI("OTA: Ready to receive data chunks...");
        LOGI("OTA: Waiting for %d bytes (%d chunks)",
             info.size, (info.size + OTA_CHUNK_SIZE - 1) / OTA_CHUNK_SIZE);

    } else if (strcmp(cmd, "ota_cancel") == 0) {
        // 取消升级
        ota_cancel_upgrade();
        LOGI("OTA: Upgrade cancelled");
        ota_report_status();

    } else if (strcmp(cmd, "ota_status") == 0) {
        // 查询状态
        ota_report_status();

    } else if (strcmp(cmd, "ota_rollback") == 0) {
        // 触发回滚（需要设置回滚标志）
        LOGI("OTA: Rollback command received");
        // TODO: 实现回滚逻辑
        ota_report_status();

    } else {
        LOGW("OTA: Unknown command: %s", cmd);
    }
}

/**
 * @brief 创建 OTA 客户端任务
 */
void ota_task_create(void)
{
    osThreadDef(otaClientTask, ota_client_task, OTA_TASK_PRIORITY, 0, OTA_TASK_STACK);
    LOGI("OTA Task: osThreadDef created, priority=%d, stack=%d", OTA_TASK_PRIORITY, OTA_TASK_STACK);
    
    osThreadId handle = osThreadCreate(osThread(otaClientTask), NULL);
    if (handle == NULL) {
        LOGE("OTA Task: create failed - osThreadCreate returned NULL");
    } else {
        LOGI("OTA Task: created successfully, handle=0x%08X", (unsigned int)handle);
    }
}

/**
 * @brief OTA 客户端任务
 * @param argument 任务参数（未使用）
 *
 * 功能：
 * 1. 初始化 OTA 客户端
 * 2. 从 OTA 命令队列等待并处理 OTA 消息
 */
void ota_client_task(void const *argument)
{
    mqtt_msg_t msg;
    osStatus status;

    LOGI("OTA Client task started - entering");
    LOGI("OTA Client task - calling ota_client_init...");

    ota_client_init();

    LOGI("OTA Client task - init completed");

    while (1) {
        /* 等待 OTA 命令 */
        status = ota_cmd_queue_get(&msg, 500);
        if (status != osOK) {
            continue;
        }

        LOGI("OTA Task: received message on topic: %s", msg.topic);

        /* 检查是否是 OTA 命令主题 */
        if (strstr(msg.topic, "/ota/cmd") != NULL) {
            LOGI("OTA Task: processing ota command...");
            ota_parse_command(msg.payload);
            continue;
        }

        /* 检查是否是 OTA 数据主题 */
        if (strstr(msg.topic, "/ota/data") != NULL) {
            LOGI("OTA Task: processing ota data...");
            ota_receive_data_chunk(msg.payload, msg.len);
            continue;
        }
    }
}

/* ==================== Base64 解码 ==================== */

static const unsigned char base64_decode_table[256] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

/**
 * @brief Base64 解码
 * @param input Base64 编码的输入字符串
 * @param input_len 输入长度
 * @param output 输出缓冲区
 * @param output_len 输出长度
 * @return 0 成功, -1 失败
 */
static int base64_decode(const char *input, int input_len, uint8_t *output, int *output_len)
{
    int i, j;
    unsigned char a, b, c, d;
    int padding = 0;

    if (input == NULL || output == NULL || output_len == NULL) {
        return -1;
    }

    // 检查输入长度
    if (input_len % 4 != 0) {
        return -1;
    }

    // 计算输出长度
    *output_len = (input_len / 4) * 3;

    // 检查填充
    if (input[input_len - 1] == '=') padding++;
    if (input[input_len - 2] == '=') padding++;
    *output_len -= padding;

    // 解码
    for (i = 0, j = 0; i < input_len; i += 4) {
        a = base64_decode_table[(unsigned char)input[i]];
        b = base64_decode_table[(unsigned char)input[i + 1]];
        c = base64_decode_table[(unsigned char)input[i + 2]];
        d = base64_decode_table[(unsigned char)input[i + 3]];

        if (a == 255 || b == 255 || c == 255 || d == 255) {
            return -1;
        }

        output[j++] = (a << 2) | (b >> 4);
        if (input[i + 2] != '=') {
            output[j++] = (b << 4) | (c >> 2);
        }
        if (input[i + 3] != '=') {
            output[j++] = (c << 6) | d;
        }
    }

    return 0;
}

/* ==================== 分包接收功能 ==================== */

/**
 * @brief 发送 ACK 确认
 * @param index 数据块索引
 * @param success 是否成功
 */
static void ota_send_ack(uint32_t index, int success)
{
    mqtt_msg_t pub_msg;
    int len;

    // 初始化消息结构
    memset(&pub_msg, 0, sizeof(pub_msg));

    len = snprintf(pub_msg.payload, sizeof(pub_msg.payload),
        "{\"index\":%d,\"success\":%s}",
        index, success ? "true" : "false");

    // 设置主题
    snprintf(pub_msg.topic, sizeof(pub_msg.topic), "%s", OTA_TOPIC_ACK);
    pub_msg.len = strlen(pub_msg.payload);
    pub_msg.qos = QOS0;

    // 放入发送队列
    if (mqtt_tx_queue_put(&pub_msg, 0) == 0) {
        LOGI("OTA: ACK queued - index=%d, success=%d", index, success);
    } else {
        LOGE("OTA: Failed to queue ACK");
    }
}

/**
 * @brief 接收并处理固件数据块
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
void ota_receive_data_chunk(const char *payload, int payload_len)
{
    uint32_t index, total, size;
    char *data_start, *data_end;
    char data_base64[OTA_CHUNK_SIZE * 2];  // Base64 编码后约为原数据的 4/3
    uint8_t data_binary[OTA_CHUNK_SIZE];
    int data_base64_len, data_binary_len;
    int ret;
    uint32_t offset;

    if (payload == NULL || payload_len <= 0) {
        LOGE("OTA: Invalid data chunk payload");
        return;
    }

    // 检查当前状态
    if (g_ota_status.stage != OTA_STAGE_DOWNLOADING) {
        LOGW("OTA: Not in downloading stage, ignoring data chunk");
        return;
    }

    // 解析 JSON 字段
    index = parse_json_number(payload, "index");
    total = parse_json_number(payload, "total");
    size = parse_json_number(payload, "size");

    if (index == 0 && total == 0) {
        LOGE("OTA: Failed to parse data chunk fields");
        ota_send_ack(index, 0);
        return;
    }

    LOGI("OTA: Data chunk received - index=%lu/%lu, size=%lu", index + 1, total, size);

    // 查找 data 字段
    data_start = strstr(payload, "\"data\":\"");
    if (data_start == NULL) {
        LOGE("OTA: Missing 'data' field");
        ota_send_ack(index, 0);
        return;
    }
    data_start += 8;  // 跳过 "data":"

    data_end = strchr(data_start, '"');
    if (data_end == NULL) {
        LOGE("OTA: Invalid 'data' field format");
        ota_send_ack(index, 0);
        return;
    }

    data_base64_len = data_end - data_start;
    if (data_base64_len >= sizeof(data_base64)) {
        LOGE("OTA: Data too large (%d bytes)", data_base64_len);
        ota_send_ack(index, 0);
        return;
    }

    // 复制 Base64 数据
    memcpy(data_base64, data_start, data_base64_len);
    data_base64[data_base64_len] = '\0';

    // Base64 解码
    ret = base64_decode(data_base64, data_base64_len, data_binary, &data_binary_len);
    if (ret != 0) {
        LOGE("OTA: Base64 decode failed");
        ota_send_ack(index, 0);
        return;
    }

    LOGI("OTA: Decoded %d bytes from Base64", data_binary_len);

    // 计算写入偏移量
    offset = index * OTA_CHUNK_SIZE;

    // 写入 Flash
    ret = ota_write_firmware_chunk(offset, data_binary, data_binary_len);
    if (ret != OTA_ERR_OK) {
        LOGE("OTA: Flash write failed at offset %lu", offset);
        ota_send_ack(index, 0);
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        ota_report_status();
        return;
    }

    // 更新进度
    g_ota_status.downloaded_size = offset + data_binary_len;
    g_ota_status.progress = (g_ota_status.downloaded_size * 100) / g_ota_status.total_size;

    // 发送 ACK 确认
    ota_send_ack(index, 1);

    // 上报进度（每 10% 上报一次）
    if (g_ota_status.progress % OTA_PROGRESS_INTERVAL == 0) {
        ota_report_status();
    }

    // 检查是否是最后一块
    if (index + 1 >= total) {
        LOGI("OTA: All data chunks received, verifying...");

        // 验证固件
        g_ota_status.stage = OTA_STAGE_VERIFYING;
        g_ota_status.progress = 90;
        ota_report_status();

        ret = ota_verify_firmware(g_firmware_info.crc32);
        if (ret != OTA_ERR_OK) {
            LOGE("OTA: Firmware verification failed");
            g_ota_status.error_code = OTA_ERR_CRC_MISMATCH;
            g_ota_status.stage = OTA_STAGE_FAILED;
            ota_report_status();
            return;
        }

        // 触发升级
        ret = ota_trigger_upgrade();
        if (ret != OTA_ERR_OK) {
            LOGE("OTA: Failed to trigger upgrade");
            ota_report_status();
            return;
        }

        LOGI("OTA: Upgrade triggered successfully, will reboot...");
    }
}
