#ifndef __BOOTLOADER_CONFIG_H
#define __BOOTLOADER_CONFIG_H

#include "stm32f4xx_hal.h"

/* Bootloader版本 */
#define BOOTLOADER_VERSION    "V-01-01-00"

/* Flash分区定义 (STM32F405 1MB Flash) */
#define FLASH_BASE_ADDR       0x08000000
#define FLASH_SIZE            0x00100000

/* Bootloader分区 */
#define BOOTLOADER_ADDR       0x08000000
#define BOOTLOADER_SIZE       0x0000C000  // 48KB

/* App-A分区 (主应用) */
#define APP_A_ADDR            0x0800C000
#define APP_A_SIZE            0x00078000  // 480KB

/* App-B分区 (备份/升级区) */
#define APP_B_ADDR            0x08084000
#define APP_B_SIZE            0x00078000  // 480KB

/* 参数区 */
#define OTA_PARAMS_ADDR       0x080FC000
#define OTA_PARAMS_BACKUP_ADDR 0x080F8000
#define OTA_PARAMS_SIZE       0x00004000  // 16KB

/* 魔数定义 */
/*
OTA_PARAMS_MAGIC 0x4F544153   O T A S O TA P ar A m S （OTA参数区标识）
FIRMWARE_MAGIC 0x46574D53     F W M S F i W a M e S ignature（固件签名）
*/
#define OTA_PARAMS_MAGIC      0x4F544153  // "OTAS"
#define FIRMWARE_MAGIC        0x46574D53  // "FWMS"

/* OTA标志 */
#define OTA_FLAG_NORMAL       0    // 正常启动
#define OTA_FLAG_PENDING      1    // 待升级
#define OTA_FLAG_SUCCESS      2    // 升级成功
#define OTA_FLAG_ROLLBACK     3    // 回滚

/* App标识 */
#define APP_A                 0
#define APP_B                 1

/* 最大验证次数 */
#define OTA_MAX_BOOT_COUNT    5

/* LED状态定义 */
#define LED_BOOTING           0    // 快速闪烁（启动中）
#define LED_OK                1    // 常亮（正常）
#define LED_UPGRADE           2    // 慢闪烁（升级中）
#define LED_ERROR             3    // 熄灭（错误）

/* RAM范围检查 */
#define RAM_BASE_ADDR         0x20000000
#define RAM_SIZE              0x00020000  // 128KB (STM32F405)
#define RAM_END_ADDR          (RAM_BASE_ADDR + RAM_SIZE)

/* OTA参数结构 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic_number;          // 魔数 OTA_PARAMS_MAGIC
    uint32_t ota_flag;             // OTA标志
    uint32_t active_app;           // 当前激活的App
    uint32_t boot_count;           // 启动计数
    uint32_t max_boot_count;       // 最大验证次数

    // App-A 信息
    uint32_t app_a_version;        // App-A版本号
    uint32_t app_a_size;           // App-A大小
    uint32_t app_a_crc32;          // App-A CRC32校验值
    uint8_t  app_a_valid;          // App-A是否有效

    // App-B 信息
    uint32_t app_b_version;        // App-B版本号
    uint32_t app_b_size;           // App-B大小
    uint32_t app_b_crc32;          // App-B CRC32校验值
    uint8_t  app_b_valid;          // App-B是否有效

    uint32_t reserved[16];         // 保留
} ota_params_t;
#pragma pack(pop)

/* 固件头结构 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          // 魔数 FIRMWARE_MAGIC
    uint32_t version;        // 版本号
    uint32_t size;           // 固件大小（不含头部）
    uint32_t crc32;          // CRC32校验值（不含头部）
} firmware_header_t;
#pragma pack(pop)

#endif
