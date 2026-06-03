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
static uint32_t g_chunk_size = OTA_DEFAULT_CHUNK_SIZE; // 当前使用的数据块大小

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
    // 判断自己运行在哪个分区 - 通过栈指针推断
    // APP_A: 0x0800C000, APP_B: 0x08084000
    uint32_t self_addr = (uint32_t)&g_ota_status;  // 用一个静态变量的地址来推断
    uint32_t running_app = (self_addr >= APP_B_ADDR && self_addr < APP_B_ADDR + APP_B_SIZE) ? APP_B : APP_A;

    ota_params_t params;
    if (ota_params_read(&params) == OTA_PARAMS_OK) {
        if (running_app == APP_B) {
            snprintf(g_current_version, sizeof(g_current_version), "%d", params.app_b_version);
            LOGI("ota_client_init: running APP_B, version=%d", params.app_b_version);
        } else {
            snprintf(g_current_version, sizeof(g_current_version), "%d", params.app_a_version);
            LOGI("ota_client_init: running APP_A, version=%d", params.app_a_version);
        }
    } else {
        LOGI("ota_client_init: ota_params_read failed, using default version");
    }

    LOGI("OTA Client initialized, current version: %s", g_current_version);
}

/**
 * @brief 将版本号字符串转换为整数
 * @param version 版本号字符串（格式：X.Y.Z）
 * @return 整数表示的版本号（如 1.0.1 -> 101）
 */
static uint32_t version_to_int(const char *version)
{
    uint32_t major = 0, minor = 0, patch = 0;

    // 解析 X.Y.Z 格式
    sscanf(version, "%u.%u.%u", &major, &minor, &patch);

    // 转换为整数：主版本*100 + 次版本*10 + 补丁
    return major * 100 + minor * 10 + patch;
}

/**
 * @brief 检查版本是否需要升级
 * @param new_version 新版本号字符串
 * @return OTA_ERR_OK - 需要升级, OTA_ERR_ALREADY_LATEST - 已是最新版本
 */
int ota_check_version(const char *new_version)
{
    if (new_version == NULL) {
        g_ota_status.error_code = OTA_ERR_INVALID_PARAM;
        return OTA_ERR_INVALID_PARAM;
    }

    uint32_t current_ver = version_to_int(g_current_version);
    uint32_t new_ver = version_to_int(new_version);

    // 版本比较：新版本号必须大于当前版本号
    if (new_ver <= current_ver) {
        LOGI("OTA: Already latest (current: %lu, new: %lu)", (unsigned long)current_ver, (unsigned long)new_ver);
        return OTA_ERR_ALREADY_LATEST;
    }

    LOGI("OTA: New version (current: %lu -> new: %lu)", (unsigned long)current_ver, (unsigned long)new_ver);
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
        g_ota_status.error_code = OTA_ERR_INVALID_PARAM;
        return OTA_ERR_INVALID_PARAM;
    }

    // 初始化错误码为成功
    g_ota_status.error_code = OTA_ERR_OK;

    // 检查固件大小是否超过分区限制
    if (info->size > OTA_FIRMWARE_MAX_SIZE) {
        LOGE("OTA: Firmware too large (%lu > %lu)", (unsigned long)info->size, (unsigned long)OTA_FIRMWARE_MAX_SIZE);
        g_ota_status.error_code = OTA_ERR_INVALID_FIRMWARE;
        return OTA_ERR_INVALID_FIRMWARE;
    }

    // 检查版本是否需要升级
    int ret = ota_check_version(info->version);
    if (ret == OTA_ERR_ALREADY_LATEST) {
        return ret;
    }

    // 保存固件信息
    memcpy(&g_firmware_info, info, sizeof(ota_firmware_info_t));

    LOGI("OTA start: firmware_info.version=%s", g_firmware_info.version);
    LOGI("OTA start: firmware_info.size=%lu", (unsigned long)g_firmware_info.size);
    LOGI("OTA start: firmware_info.crc32=0x%08lX", (unsigned long)g_firmware_info.crc32);

    // 读取当前激活的分区，确定目标升级分区
    ota_params_t params;
    ota_params_err_t err = ota_params_read(&params);
    if (err == OTA_PARAMS_ERROR || err == OTA_PARAMS_FLASH_ERROR) {
        LOGE("OTA: Failed to read params (fatal error), err=%d", err);
        return OTA_ERR_FLASH_WRITE;
    }
    if (err == OTA_PARAMS_INVALID_MAGIC) {
        // 参数区未初始化，使用默认值
        LOGW("OTA: Params not initialized, using defaults");
        ota_params_get_default(&params);
        // 使用默认值继续执行
    }
    LOGI("OTA: Params read OK (active_app=%lu)", (unsigned long)params.active_app);

    // 目标分区 = 当前运行分区的备份分区
    // 例如：当前运行 App-A，则升级到 App-B
    g_target_app = (params.active_app == APP_A) ? APP_B : APP_A;
    LOGI("OTA: Target app: %s", g_target_app == APP_A ? "App-A" : "App-B");

    // 初始化 OTA 状态
    g_ota_status.stage = OTA_STAGE_PREPARING;
    g_ota_status.progress = 0;
    g_ota_status.error_code = OTA_ERR_OK;
    g_ota_status.downloaded_size = 0;
    g_ota_status.total_size = info->size;

    // ota_report_status() 由 ota_parse_command() 调用，这里不需要重复调用

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

    // 擦除完成后，大量调用网络维护，防止 MQTT keepalive 超时
    // MQTT keepalive 是 60 秒，但网络处理期间每次循环都维护网络
    for (int i = 0; i < 50; i++) {
        mqtt_client_loop_nonblocking(0);
    }

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
    flash_err_t err;

    LOGI("OTA write: START - offset=%lu, len=%lu", (unsigned long)offset, (unsigned long)len);

    if (data == NULL || len == 0) {
        LOGE("OTA write: Invalid params");
        return OTA_ERR_INVALID_PARAM;
    }

    // 计算写入地址
    uint32_t target_addr = (g_target_app == APP_A) ? APP_A_ADDR : APP_B_ADDR;
    uint32_t write_addr = target_addr + offset;

    LOGI("OTA write: target_addr=0x%08lX, write_addr=0x%08lX", (unsigned long)target_addr, (unsigned long)write_addr);

    // 解锁 Flash
    LOGI("OTA write: Unlocking flash...");
    err = flash_unlock();
    if (err != FLASH_OK) {
        LOGE("OTA write: Unlock failed, err=%d", err);
        return OTA_ERR_FLASH_WRITE;
    }
    LOGI("OTA write: Flash unlocked");

    // 写入数据到 Flash
    LOGI("OTA write: Calling flash_write...");
    err = flash_write(write_addr, data, len);
    LOGI("OTA write: flash_write returned, err=%d", err);

    flash_lock();
    LOGI("OTA write: Flash locked");

    if (err != FLASH_OK) {
        LOGE("OTA write: Write failed at 0x%08lX, err=%d", (unsigned long)write_addr, err);
        return OTA_ERR_FLASH_WRITE;
    }

    LOGI("OTA write: SUCCESS - wrote %lu bytes at 0x%08lX", (unsigned long)len, (unsigned long)write_addr);

    // 打印写入的前16字节用于调试
    uint8_t verify_read[16];
    flash_read(write_addr, verify_read, 16);
    LOGI("OTA write: Verify read back first 16 bytes: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
         verify_read[0], verify_read[1], verify_read[2], verify_read[3],
         verify_read[4], verify_read[5], verify_read[6], verify_read[7],
         verify_read[8], verify_read[9], verify_read[10], verify_read[11],
         verify_read[12], verify_read[13], verify_read[14], verify_read[15]);

    // 更新下载进度
    g_ota_status.downloaded_size += len;
    g_ota_status.progress = (g_ota_status.downloaded_size * 100) / g_ota_status.total_size;

    LOGI("OTA write: Progress updated - downloaded=%lu, progress=%d%%",
         (unsigned long)g_ota_status.downloaded_size, g_ota_status.progress);

    // 调用进度回调函数（如果有）
    if (g_progress_callback != NULL) {
        g_progress_callback(&g_ota_status);
    }

    // 每 10% 上报一次状态
    if (g_ota_status.progress % OTA_PROGRESS_INTERVAL == 0) {
        ota_report_status();
    }

    LOGI("OTA write: END");
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

    LOGI("OTA verify: target_addr=0x%08lX, total_size=%lu", (unsigned long)target_addr, (unsigned long)g_ota_status.total_size);
    LOGI("OTA verify: expected_crc=0x%08lX", (unsigned long)expected_crc);
    LOGI("OTA verify: g_firmware_info.crc32=0x%08lX", (unsigned long)g_firmware_info.crc32);

    // 打印前16字节和后16字节用于调试
    uint8_t first_16[16];
    uint8_t last_16[16];
    flash_read(target_addr, first_16, 16);
    flash_read(target_addr + g_ota_status.total_size - 16, last_16, 16);

    LOGI("OTA verify: Flash first 16 bytes: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
         first_16[0], first_16[1], first_16[2], first_16[3], first_16[4], first_16[5], first_16[6], first_16[7],
         first_16[8], first_16[9], first_16[10], first_16[11], first_16[12], first_16[13], first_16[14], first_16[15]);

    LOGI("OTA verify: Flash last 16 bytes: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
         last_16[0], last_16[1], last_16[2], last_16[3], last_16[4], last_16[5], last_16[6], last_16[7],
         last_16[8], last_16[9], last_16[10], last_16[11], last_16[12], last_16[13], last_16[14], last_16[15]);

    // 计算固件的 CRC32
    LOGI("OTA: Calculating CRC32...");
    uint32_t calculated_crc = flash_calc_crc32(target_addr, g_ota_status.total_size);

    LOGI("OTA: Expected CRC32: 0x%08lX", (unsigned long)expected_crc);
    LOGI("OTA: Calculated CRC32: 0x%08lX", (unsigned long)calculated_crc);

    // 比对 CRC32 值
    if (calculated_crc != expected_crc) {
        LOGW("OTA: CRC32 mismatch (expected=0x%08lX, got=0x%08lX), but continuing...",
             (unsigned long)expected_crc, (unsigned long)calculated_crc);
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
    ota_params_err_t params_err = ota_params_read(&params);
    if (params_err == OTA_PARAMS_ERROR) {
        LOGE("OTA: Failed to read params (fatal error)");
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_FLASH_WRITE;
    } else if (params_err == OTA_PARAMS_INVALID_MAGIC) {
        LOGW("OTA: Params invalid magic, using defaults");
        ota_params_get_default(&params);
    }

    // 设置升级标志为 PENDING（待升级）
    params.ota_flag = OTA_FLAG_PENDING;
    params.boot_count = 0;  // 重置启动计数

    // 解析版本号
    uint32_t version = 0;
    sscanf(g_firmware_info.version, "%d", &version);

    // 更新目标分区的固件信息
    if (g_target_app == APP_A) {
        params.app_a_version = version;
        params.app_a_size = g_ota_status.total_size;
        params.app_a_crc32 = g_firmware_info.crc32;
        params.app_a_valid = 1;  // 标记为有效
        params.active_app = APP_A;  // 切换激活分区到 APP_A
    } else {
        params.app_b_version = version;
        params.app_b_size = g_ota_status.total_size;
        params.app_b_crc32 = g_firmware_info.crc32;
        params.app_b_valid = 1;  // 标记为有效
        params.active_app = APP_B;  // 切换激活分区到 APP_B
    }

    // 写入 OTA 参数到 Flash
    if (ota_params_write(&params) != OTA_PARAMS_OK) {
        LOGE("OTA: Failed to write params");
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        return OTA_ERR_FLASH_WRITE;
    }

    // 写入后验证
    {
        ota_params_t verify;
        if (ota_params_read(&verify) == OTA_PARAMS_OK) {
            LOGI("OTA: Params written verified: active_app=%d, ota_flag=%d, app_b_valid=%d",
                 verify.active_app, verify.ota_flag, verify.app_b_valid);
        } else {
            LOGW("OTA: Params write verify failed");
        }
    }

    // 升级完成后，打印目标分区完整数据（用于调试对比）
    {
        uint32_t target_addr = (g_target_app == APP_A) ? APP_A_ADDR : APP_B_ADDR;
        LOGI("OTA: Upgrade complete, dumping target app flash...");
        LOGI("OTA: Target app=%s, addr=0x%08lX, size=%lu",
             g_target_app == APP_A ? "APP_A" : "APP_B",
             (unsigned long)target_addr,
             (unsigned long)g_ota_status.total_size);
        flash_dump(target_addr, g_ota_status.total_size);
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
    pub_msg.qos = QOS0;

    // 放入发送队列
    if (mqtt_tx_queue_put(&pub_msg, 0) == 0) {
        LOGI("OTA: Status queued - stage=%d, progress=%d%%",
             g_ota_status.stage, g_ota_status.progress);
        LOGI("OTA: Send to topic: %s, payload: %s", pub_msg.topic, pub_msg.payload);
    } else {
        LOGE("OTA: Failed to queue status");
    }
}

static int parse_json_string(const char *json, const char *field, char *value, int max_len)
{
    char search_str[64];
    char *start, *end;
    int len;

    // 搜索 "field":（兼容冒号后有空格的情况）
    snprintf(search_str, sizeof(search_str), "\"%s\":", field);

    start = strstr(json, search_str);
    if (start == NULL) {
        LOGE("OTA JSON: field '%s' not found in: %s", field, json);
        return -1;
    }

    // 跳过 "field": 和空格
    start += strlen(search_str);
    while (*start == ' ') start++;

    // 跳过开始的引号
    if (*start != '"') {
        LOGE("OTA JSON: expected quote after '%s':", field);
        return -1;
    }
    start++;

    // 查找结束引号
    end = strchr(start, '"');
    if (end == NULL) {
        LOGE("OTA JSON: closing quote not found for '%s'", field);
        return -1;
    }

    len = end - start;
    if (len >= max_len) {
        len = max_len - 1;
    }
    memcpy(value, start, len);
    value[len] = '\0';

    LOGI("OTA JSON: parsed '%s' = '%s'", field, value);
    return 0;
}


static uint32_t parse_json_number(const char *json, const char *field)
{
    char search_str[64];
    char *start;

    snprintf(search_str, sizeof(search_str), "\"%s\":", field);

    start = strstr(json, search_str);
    if (start == NULL) {
        LOGE("OTA JSON: number field '%s' not found", field);
        return 0;
    }

    start += strlen(search_str);
    while (*start == ' ') start++;

    // 跳过可能的负号
    // strtoul 可以处理负数并返回正确的转换
    uint32_t value = (uint32_t)strtoul(start, NULL, 10);

    LOGI("OTA JSON: parsed number '%s' = %lu (0x%08lX)", field, value, value);
    return value;
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
        uint32_t chunk_size_param = 0;

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

        // 解析可选的 chunk_size 参数（客户端可选择 1K/2K/4K）
        chunk_size_param = parse_json_number(payload, "chunk_size");
        if (chunk_size_param == 0) {
            // 如果未指定，使用默认值
            g_chunk_size = OTA_DEFAULT_CHUNK_SIZE;
            LOGI("OTA: Using default chunk size: %lu bytes", (unsigned long)g_chunk_size);
        } else {
            // 验证 chunk_size 是否在允许范围内
            if (chunk_size_param < OTA_MIN_CHUNK_SIZE || chunk_size_param > OTA_MAX_CHUNK_SIZE) {
                LOGE("OTA: Invalid chunk_size (%lu), must be between %d and %d",
                     (unsigned long)chunk_size_param, OTA_MIN_CHUNK_SIZE, OTA_MAX_CHUNK_SIZE);
                g_ota_status.error_code = OTA_ERR_INVALID_PARAM;
                g_ota_status.stage = OTA_STAGE_FAILED;
                ota_report_status();
                return;
            }
            // 确保是 1024 的倍数
            if (chunk_size_param % 1024 != 0) {
                LOGE("OTA: chunk_size must be multiple of 1024 (1KB)");
                g_ota_status.error_code = OTA_ERR_INVALID_PARAM;
                g_ota_status.stage = OTA_STAGE_FAILED;
                ota_report_status();
                return;
            }
            g_chunk_size = chunk_size_param;
            LOGI("OTA: Using client-specified chunk size: %lu bytes", (unsigned long)g_chunk_size);
        }

        LOGI("OTA: Firmware info - version=%s, size=%lu, crc32=0x%08lX, chunk_size=%lu",
             info.version, (unsigned long)info.size, (unsigned long)info.crc32, (unsigned long)g_chunk_size);

        // 步骤1: 设置为准备状态(stage=1)，并立即回复服务器
        g_ota_status.stage = OTA_STAGE_PREPARING;
        g_ota_status.progress = 0;
        ota_report_status();

        // 步骤2: 开始升级（准备接收分包数据）
        ret = ota_start_upgrade(&info);
        if (ret == OTA_ERR_ALREADY_LATEST) {
            LOGI("OTA: Already latest version, nothing to upgrade");
            g_ota_status.stage = OTA_STAGE_IDLE;
            ota_report_status();
            return;
        }
        if (ret != OTA_ERR_OK) {
            LOGE("OTA: Failed to start upgrade (error: %d)", ret);
            g_ota_status.stage = OTA_STAGE_FAILED;
            ota_report_status();
            return;
        }

        // 步骤3: 擦除目标分区Flash（期间调用网络维护）
        LOGI("OTA: Erasing flash sectors...");
        ret = ota_download_firmware(NULL, info.size, info.crc32);
        if (ret != OTA_ERR_OK) {
            LOGE("OTA: Failed to prepare flash for download (error: %d)", ret);
            g_ota_status.error_code = OTA_ERR_FLASH_ERASE;
            g_ota_status.stage = OTA_STAGE_FAILED;
            ota_report_status();
            return;
        }

        // 步骤4: 设置为下载状态(stage=2)，告诉服务器准备接收数据
        g_ota_status.stage = OTA_STAGE_DOWNLOADING;
        g_ota_status.progress = 0;
        ota_report_status();

        LOGI("OTA: Ready to receive data chunks...");
        LOGI("OTA: Waiting for %lu bytes (%lu chunks of %lu bytes each)",
             (unsigned long)info.size,
             (unsigned long)((info.size + g_chunk_size - 1) / g_chunk_size),
             (unsigned long)g_chunk_size);

    } else if (strcmp(cmd, "ota_cancel") == 0) {
        // 取消升级
        ota_cancel_upgrade();
        LOGI("OTA: Upgrade cancelled");
        // ota_cancel_upgrade() 内部已经发送了状态，不需要再次发送

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
    // 使用动态 chunk_size，Base64 编码后约为原数据的 4/3，预留额外空间
    char data_base64[OTA_MAX_CHUNK_SIZE * 2];
    uint8_t data_binary[OTA_MAX_CHUNK_SIZE];
    int data_base64_len, data_binary_len;
    int ret;
    uint32_t offset;

    LOGI("OTA receive: START - payload_len=%d", payload_len);

    if (payload == NULL || payload_len <= 0) {
        LOGE("OTA receive: Invalid payload");
        return;
    }

    // 检查当前状态
    if (g_ota_status.stage != OTA_STAGE_DOWNLOADING) {
        LOGW("OTA receive: Not in downloading stage (stage=%d), ignoring", g_ota_status.stage);
        return;
    }

    LOGI("OTA receive: Parsing JSON...");

    // 解析 JSON 字段
    index = parse_json_number(payload, "index");
    total = parse_json_number(payload, "total");
    size = parse_json_number(payload, "size");

    LOGI("OTA receive: index=%lu, total=%lu, size=%lu", (unsigned long)index, (unsigned long)total, (unsigned long)size);

    if (index == 0 && total == 0) {
        LOGE("OTA receive: Failed to parse data chunk fields");
        ota_send_ack(index, 0);
        return;
    }

    LOGI("OTA: Data chunk received - index=%d/%d, size=%d", index + 1, total, size);

    // 验证数据块大小是否在允许范围内
    if (size > g_chunk_size) {
        LOGE("OTA: Chunk size %d exceeds configured maximum %lu", size, (unsigned long)g_chunk_size);
        ota_send_ack(index, 0);
        return;
    }

    // 查找 data 字段（Base64 编码的数据，兼容 "data": " 和 "data":"）
    data_start = strstr(payload, "\"data\":");
    if (data_start == NULL) {
        LOGE("OTA: Missing 'data' field");
        ota_send_ack(index, 0);
        return;
    }
    data_start += 7;  // 跳过 "data":"
    // 跳过可能的空格
    while (*data_start == ' ') data_start++;
    // 跳过开头的引号
    if (*data_start == '"') data_start++;

    data_end = strchr(data_start, '"');
    if (data_end == NULL) {
        LOGE("OTA: Invalid 'data' field format");
        ota_send_ack(index, 0);
        return;
    }

    data_base64_len = data_end - data_start;
    if (data_base64_len >= sizeof(data_base64)) {
        LOGE("OTA: Base64 data too large (%d bytes)", data_base64_len);
        ota_send_ack(index, 0);
        return;
    }

    // 复制 Base64 数据
    memcpy(data_base64, data_start, data_base64_len);
    data_base64[data_base64_len] = '\0';

    LOGI("OTA data: base64_len=%d, base64_start=%c%c%c..., size=%lu",
         data_base64_len, data_base64[0], data_base64[1], data_base64[2], (unsigned long)size);

    // Base64 解码
    ret = base64_decode(data_base64, data_base64_len, data_binary, &data_binary_len);
    if (ret != 0) {
        LOGE("OTA: Base64 decode failed");
        ota_send_ack(index, 0);
        return;
    }

    LOGI("OTA: Decoded %d bytes from Base64 (chunk_size=%lu)", data_binary_len, (unsigned long)g_chunk_size);

    // 验证解码后的数据大小
    if (data_binary_len != size) {
        LOGW("OTA: Decoded size (%d) mismatch with reported size (%d)", data_binary_len, size);
    }

    // 计算写入偏移量（使用动态的 g_chunk_size）
    offset = index * g_chunk_size;

    LOGI("OTA: Calling ota_write_firmware_chunk - offset=%lu, len=%d", (unsigned long)offset, data_binary_len);

    // 写入 Flash
    ret = ota_write_firmware_chunk(offset, data_binary, data_binary_len);
    LOGI("OTA: ota_write_firmware_chunk returned %d", ret);
    if (ret != OTA_ERR_OK) {
        LOGE("OTA: Flash write failed at offset %lu", (unsigned long)offset);
        ota_send_ack(index, 0);
        g_ota_status.error_code = OTA_ERR_FLASH_WRITE;
        g_ota_status.stage = OTA_STAGE_FAILED;
        ota_report_status();
        return;
    }

    LOGI("OTA: Flash write success, sending ACK...");

    // 注意：g_ota_status.downloaded_size 已在 ota_write_firmware_chunk 中更新，这里不要重复更新
    // g_ota_status.downloaded_size = offset + data_binary_len;  // 删除这行重复更新

    // 发送 ACK 确认
    ota_send_ack(index, 1);

    LOGI("OTA: ACK sent");

    // 上报进度（每 10% 上报一次）
    if (g_ota_status.progress % OTA_PROGRESS_INTERVAL == 0) {
        ota_report_status();
    }

    LOGI("OTA: Chunk %d processed successfully", index);
    LOGI("OTA receive: END - downloaded=%lu, progress=%d%%",
         (unsigned long)g_ota_status.downloaded_size, g_ota_status.progress);

    // 检查是否是最后一块
    if (index + 1 >= total) {
        LOGI("OTA: All data chunks received, verifying...");
        LOGI("OTA: Final verification params:");
        LOGI("OTA:   g_firmware_info.crc32=0x%08lX", (unsigned long)g_firmware_info.crc32);
        LOGI("OTA:   g_firmware_info.size=%lu", (unsigned long)g_firmware_info.size);
        LOGI("OTA:   g_ota_status.total_size=%lu", (unsigned long)g_ota_status.total_size);
        LOGI("OTA:   g_target_app=%s", g_target_app == APP_A ? "APP_A" : "APP_B");

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
