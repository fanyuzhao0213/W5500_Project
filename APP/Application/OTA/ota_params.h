#ifndef __OTA_PARAMS_H
#define __OTA_PARAMS_H

#include "stdint.h"
#include "ota_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// OTA参数结构
#pragma pack(push, 1)
typedef struct {
    uint32_t magic_number;          // 魔数 OTA_PARAMS_MAGIC
    uint32_t ota_flag;             // OTA标志: 0=正常, 1=待升级, 2=升级成功, 3=升级失败回滚
    uint32_t active_app;           // 当前激活的App: 0=App-A, 1=App-B
    uint32_t boot_count;           // 启动计数（用于验证新固件稳定性）
    uint32_t max_boot_count;       // 最大验证次数
    
    // App-A 信息
    uint32_t app_a_version;        // App-A版本号
    uint32_t app_a_size;           // App-A大小
    uint32_t app_a_crc32;          // App-A CRC32校验值
    uint8_t  app_a_valid;          // App-A是否有效 (0=无效, 1=有效)
    
    // App-B 信息
    uint32_t app_b_version;        // App-B版本号
    uint32_t app_b_size;           // App-B大小
    uint32_t app_b_crc32;          // App-B CRC32校验值
    uint8_t  app_b_valid;          // App-B是否有效 (0=无效, 1=有效)
    
    uint32_t reserved[16];         // 保留
} ota_params_t;
#pragma pack(pop)

// OTA参数错误码
typedef enum {
    OTA_PARAMS_OK = 0,
    OTA_PARAMS_ERROR,
    OTA_PARAMS_INVALID_MAGIC,
    OTA_PARAMS_FLASH_ERROR,
    OTA_PARAMS_CHECKSUM_ERROR
} ota_params_err_t;

// 初始化参数区
ota_params_err_t ota_params_init(void);

// 从Flash读取参数
ota_params_err_t ota_params_read(ota_params_t *params);

// 写入参数到Flash（包含备份机制）
ota_params_err_t ota_params_write(const ota_params_t *params);

// 设置OTA标志
ota_params_err_t ota_params_set_flag(uint32_t flag);

// 获取OTA标志
uint32_t ota_params_get_flag(void);

// 设置激活的App
ota_params_err_t ota_params_set_active_app(uint32_t app);

// 获取激活的App
uint32_t ota_params_get_active_app(void);

// 递增启动计数
ota_params_err_t ota_params_inc_boot_count(void);

// 重置启动计数
ota_params_err_t ota_params_reset_boot_count(void);

// 获取启动计数
uint32_t ota_params_get_boot_count(void);

// 更新App-A信息
ota_params_err_t ota_params_update_app_a(uint32_t version, uint32_t size, uint32_t crc32, uint8_t valid);

// 更新App-B信息
ota_params_err_t ota_params_update_app_b(uint32_t version, uint32_t size, uint32_t crc32, uint8_t valid);

// 获取默认参数
void ota_params_get_default(ota_params_t *params);

// 打印参数信息（调试用）
void ota_params_print(const ota_params_t *params);

#ifdef __cplusplus
}
#endif

#endif
