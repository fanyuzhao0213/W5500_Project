#ifndef __OTA_CONFIG_H
#define __OTA_CONFIG_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

// OTA魔数
#define OTA_PARAMS_MAGIC           0x4F544153  // "OTAS"
#define FIRMWARE_MAGIC             0x46574D53  // "FWMS"

// Flash分区定义 (STM32F405 1MB Flash)
#define FLASH_BASE_ADDR            0x08000000
#define FLASH_SIZE                 0x00100000  // 1MB

// Bootloader分区
#define BOOTLOADER_ADDR            0x08000000
#define BOOTLOADER_SIZE            0x0000C000  // 48KB

// App-A分区 (主应用)
#define APP_A_ADDR                 0x0800C000
#define APP_A_SIZE                 0x00078000  // 480KB

// App-B分区 (备份/升级区)
#define APP_B_ADDR                 0x08084000
#define APP_B_SIZE                 0x00078000  // 480KB

// 参数区
#define OTA_PARAMS_ADDR            0x080FC000
#define OTA_PARAMS_SIZE            0x00004000  // 16KB

// 参数区备份 (防止写入失败)
#define OTA_PARAMS_BACKUP_ADDR     0x080F8000

// OTA标志定义
#define OTA_FLAG_NORMAL            0
#define OTA_FLAG_PENDING           1
#define OTA_FLAG_SUCCESS           2
#define OTA_FLAG_ROLLBACK          3

// 应用标识
#define APP_A                      0
#define APP_B                      1

// 验证次数
#define OTA_MAX_BOOT_COUNT         5

// Flash扇区大小 (STM32F4: 16KB/sector 1-11, 64KB/sector12-16)
#define FLASH_SECTOR_SIZE_16KB     0x4000
#define FLASH_SECTOR_SIZE_64KB     0x10000

#ifdef __cplusplus
}
#endif

#endif
