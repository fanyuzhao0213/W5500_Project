#ifndef __PROJECT_CONFIG_H
#define __PROJECT_CONFIG_H

#include "stdint.h"

/* ================ 工程配置 ================= */

/* 产品名称 */
#define PRODUCT_NAME         "W5500_MQTT_Gateway"

/* 硬件版本 */
#define HARDWARE_VERSION     "V1.0"

/* 固件版本 */
#define FIRMWARE_VERSION_MAJOR    1
#define FIRMWARE_VERSION_MINOR    1
#define FIRMWARE_VERSION_PATCH    88
#define FIRMWARE_VERSION_BUILD    20260530

/* 固件版本字符串 */
#define FIRMWARE_VERSION_STR      "1.1.66.20260530"

/* 编译时间 */
#define BUILD_TIME           __DATE__ " " __TIME__

/* ================ Flash分区配置 ================= */

/* Bootloader分区 */
#define BOOTLOADER_ADDR       0x08000000
#define BOOTLOADER_SIZE       0x0000C000  // 48KB

/* App-A分区 (当前运行的主应用) */
#define APP_A_ADDR            0x0800C000
#define APP_A_SIZE            0x00078000  // 480KB

/* App-B分区 (备份/升级区) */
#define APP_B_ADDR            0x08084000
#define APP_B_SIZE            0x00078000  // 480KB

/* OTA参数区 */
#define OTA_PARAMS_ADDR       0x080FC000
#define OTA_PARAMS_SIZE       0x00004000  // 16KB

/* ================ 功能配置 ================= */

/* 启用调试日志 */
#define ENABLE_DEBUG_LOG      1

/* 启用OTA升级 */
#define ENABLE_OTA_UPGRADE    1

/* 启用MQTT */
#define ENABLE_MQTT           1

/* 启用看门狗 */
#define ENABLE_WATCHDOG       1

/* ================ 版本信息结构体 ================= */

typedef struct {
    uint8_t  major;
    uint8_t  minor;
    uint8_t  patch;
    uint32_t build;
    char     version_str[32];
    char     product_name[32];
    char     hardware_version[16];
    char     build_time[32];
} firmware_info_t;

/* 获取固件信息 */
static inline void get_firmware_info(firmware_info_t *info) {
    if (info == NULL) return;

    info->major = FIRMWARE_VERSION_MAJOR;
    info->minor = FIRMWARE_VERSION_MINOR;
    info->patch = FIRMWARE_VERSION_PATCH;
    info->build = FIRMWARE_VERSION_BUILD;

    snprintf(info->version_str, sizeof(info->version_str), "%s", FIRMWARE_VERSION_STR);
    snprintf(info->product_name, sizeof(info->product_name), "%s", PRODUCT_NAME);
    snprintf(info->hardware_version, sizeof(info->hardware_version), "%s", HARDWARE_VERSION);
    snprintf(info->build_time, sizeof(info->build_time), "%s", BUILD_TIME);
}

#endif /* __PROJECT_CONFIG_H */
