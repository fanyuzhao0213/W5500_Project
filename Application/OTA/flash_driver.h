#ifndef __FLASH_DRIVER_H
#define __FLASH_DRIVER_H

#include "stm32f4xx_hal.h"
#include "ota_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Flash驱动错误码
typedef enum {
    FLASH_OK = 0,
    FLASH_ERROR,
    FLASH_BUSY,
    FLASH_TIMEOUT,
    FLASH_PROTECTED,
    FLASH_INVALID_ADDR
} flash_err_t;

// Flash解锁/加锁
flash_err_t flash_unlock(void);
flash_err_t flash_lock(void);

// Flash擦除操作
flash_err_t flash_erase_sector(uint32_t sector);
flash_err_t flash_erase_range(uint32_t start_addr, uint32_t size);

// Flash读写操作
flash_err_t flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
void flash_read(uint32_t addr, uint8_t *data, uint32_t len);

// 按字/半字/字节写入
flash_err_t flash_write_word(uint32_t addr, uint32_t data);
flash_err_t flash_write_halfword(uint32_t addr, uint16_t data);
flash_err_t flash_write_byte(uint32_t addr, uint8_t data);

// CRC32计算
uint32_t flash_calc_crc32(uint32_t addr, uint32_t size);
uint32_t crc32_calc(const uint8_t *data, uint32_t len);

// Flash检查
int flash_is_erased(uint32_t addr, uint32_t size);

// 地址检查
int flash_is_valid_addr(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif
