#ifndef __BOOTLOADER_UTILS_H
#define __BOOTLOADER_UTILS_H

#include "bootloader_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// CRC32计算
uint32_t bootloader_crc32(const uint8_t *data, uint32_t len);
uint32_t bootloader_crc32_flash(uint32_t addr, uint32_t size);

// OTA参数操作
int bootloader_read_params(ota_params_t *params);
int bootloader_write_params(ota_params_t *params);

// 固件验证
int bootloader_validate_firmware(uint32_t app_addr, firmware_header_t *header);

// 应用跳转
void bootloader_jump_to_app(uint32_t app_addr);

// 延时函数
void bootloader_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif
