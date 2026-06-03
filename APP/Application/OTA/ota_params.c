#include "ota_params.h"
#include "flash_driver.h"
#include <string.h>
#include <stdio.h>
#include "LOG.h"
// 静态参数缓存
static ota_params_t cached_params;
static int params_initialized = 0;

// 获取默认参数
void ota_params_get_default(ota_params_t *params)
{
    if (params == NULL) {
        return;
    }
    
    memset(params, 0, sizeof(ota_params_t));
    
    params->magic_number = OTA_PARAMS_MAGIC;
    params->ota_flag = OTA_FLAG_NORMAL;
    params->active_app = APP_A;
    params->boot_count = 0;
    params->max_boot_count = OTA_MAX_BOOT_COUNT;
    
    // App-A默认值
    params->app_a_version = 0;
    params->app_a_size = 0;
    params->app_a_crc32 = 0;
    params->app_a_valid = 0;
    
    // App-B默认值
    params->app_b_version = 0;
    params->app_b_size = 0;
    params->app_b_crc32 = 0;
    params->app_b_valid = 0;
}

// 验证参数有效性
static int ota_params_validate(const ota_params_t *params)
{
    if (params == NULL) {
        return 0;
    }
    
    if (params->magic_number != OTA_PARAMS_MAGIC) {
        return 0;
    }
    
    return 1;
}

// 从Flash读取参数
ota_params_err_t ota_params_read(ota_params_t *params)
{
    ota_params_t temp_params;
    uint32_t magic_check;

    if (params == NULL) {
        LOGE("ota_params_read: params is NULL");
        return OTA_PARAMS_ERROR;
    }

    // 先尝试读取主参数区
    LOGI("ota_params_read: reading from 0x%08X", OTA_PARAMS_ADDR);
    flash_read(OTA_PARAMS_ADDR, (uint8_t *)&temp_params, sizeof(ota_params_t));
    flash_read(OTA_PARAMS_ADDR, (uint8_t *)&magic_check, sizeof(uint32_t));
    LOGI("ota_params_read: main magic=0x%08X, expected=0x%08X", magic_check, OTA_PARAMS_MAGIC);

    if (ota_params_validate(&temp_params)) {
        memcpy(params, &temp_params, sizeof(ota_params_t));
        return OTA_PARAMS_OK;
    }

    // 主参数区无效，尝试读取备份区
    LOGI("ota_params_read: main invalid, reading from 0x%08X", OTA_PARAMS_BACKUP_ADDR);
    flash_read(OTA_PARAMS_BACKUP_ADDR, (uint8_t *)&temp_params, sizeof(ota_params_t));
    flash_read(OTA_PARAMS_BACKUP_ADDR, (uint8_t *)&magic_check, sizeof(uint32_t));
    LOGI("ota_params_read: backup magic=0x%08X, expected=0x%08X", magic_check, OTA_PARAMS_MAGIC);

    if (ota_params_validate(&temp_params)) {
        memcpy(params, &temp_params, sizeof(ota_params_t));
        return OTA_PARAMS_OK;
    }

    // 都无效，返回默认参数
    LOGW("ota_params_read: both main and backup invalid, using defaults");
    ota_params_get_default(params);
    return OTA_PARAMS_INVALID_MAGIC;
}

// 写入参数到Flash（包含备份机制）
ota_params_err_t ota_params_write(const ota_params_t *params)
{
    flash_err_t err;
    ota_params_t temp_params;
    
    if (params == NULL) {
        return OTA_PARAMS_ERROR;
    }
    
    // 复制参数并确保魔数正确
    memcpy(&temp_params, params, sizeof(ota_params_t));
    temp_params.magic_number = OTA_PARAMS_MAGIC;
    
    // 解锁Flash
    err = flash_unlock();
    if (err != FLASH_OK) {
        return OTA_PARAMS_FLASH_ERROR;
    }
    
    // 先擦除备份区
    err = flash_erase_range(OTA_PARAMS_BACKUP_ADDR, OTA_PARAMS_SIZE);
    if (err != FLASH_OK) {
        flash_lock();
        return OTA_PARAMS_FLASH_ERROR;
    }
    
    // 写入备份区
    err = flash_write(OTA_PARAMS_BACKUP_ADDR, (uint8_t *)&temp_params, sizeof(ota_params_t));
    if (err != FLASH_OK) {
        flash_lock();
        return OTA_PARAMS_FLASH_ERROR;
    }
    
    // 再擦除主参数区
    err = flash_erase_range(OTA_PARAMS_ADDR, OTA_PARAMS_SIZE);
    if (err != FLASH_OK) {
        flash_lock();
        return OTA_PARAMS_FLASH_ERROR;
    }
    
    // 写入主参数区
    err = flash_write(OTA_PARAMS_ADDR, (uint8_t *)&temp_params, sizeof(ota_params_t));
    if (err != FLASH_OK) {
        flash_lock();
        return OTA_PARAMS_FLASH_ERROR;
    }
    
    // 加锁Flash
    flash_lock();
    
    // 更新缓存
    memcpy(&cached_params, &temp_params, sizeof(ota_params_t));
    params_initialized = 1;
    
    return OTA_PARAMS_OK;
}

// 初始化参数区
ota_params_err_t ota_params_init(void)
{
    ota_params_err_t err;
    
    if (params_initialized) {
        return OTA_PARAMS_OK;
    }
    
    // 读取参数
    err = ota_params_read(&cached_params);
    
    // 如果参数无效，写入默认参数
    if (err == OTA_PARAMS_INVALID_MAGIC) {
        ota_params_get_default(&cached_params);
        err = ota_params_write(&cached_params);
    }
    
    if (err == OTA_PARAMS_OK) {
        params_initialized = 1;
    }
    
    return err;
}

// 设置OTA标志
ota_params_err_t ota_params_set_flag(uint32_t flag)
{
    ota_params_err_t err;
    
    if (!params_initialized) {
        err = ota_params_init();
        if (err != OTA_PARAMS_OK) {
            return err;
        }
    }
    
    cached_params.ota_flag = flag;
    
    return ota_params_write(&cached_params);
}

// 获取OTA标志
uint32_t ota_params_get_flag(void)
{
    if (!params_initialized) {
        ota_params_init();
    }
    
    return cached_params.ota_flag;
}

// 设置激活的App
ota_params_err_t ota_params_set_active_app(uint32_t app)
{
    ota_params_err_t err;
    
    if (app != APP_A && app != APP_B) {
        return OTA_PARAMS_ERROR;
    }
    
    if (!params_initialized) {
        err = ota_params_init();
        if (err != OTA_PARAMS_OK) {
            return err;
        }
    }
    
    cached_params.active_app = app;
    
    return ota_params_write(&cached_params);
}

// 获取激活的App
uint32_t ota_params_get_active_app(void)
{
    if (!params_initialized) {
        ota_params_init();
    }
    
    return cached_params.active_app;
}

// 递增启动计数
ota_params_err_t ota_params_inc_boot_count(void)
{
    ota_params_err_t err;
    
    if (!params_initialized) {
        err = ota_params_init();
        if (err != OTA_PARAMS_OK) {
            return err;
        }
    }
    
    cached_params.boot_count++;
    
    return ota_params_write(&cached_params);
}

// 重置启动计数
ota_params_err_t ota_params_reset_boot_count(void)
{
    ota_params_err_t err;
    
    if (!params_initialized) {
        err = ota_params_init();
        if (err != OTA_PARAMS_OK) {
            return err;
        }
    }
    
    cached_params.boot_count = 0;
    
    return ota_params_write(&cached_params);
}

// 获取启动计数
uint32_t ota_params_get_boot_count(void)
{
    if (!params_initialized) {
        ota_params_init();
    }
    
    return cached_params.boot_count;
}

// 更新App-A信息
ota_params_err_t ota_params_update_app_a(uint32_t version, uint32_t size, uint32_t crc32, uint8_t valid)
{
    ota_params_err_t err;
    
    if (!params_initialized) {
        err = ota_params_init();
        if (err != OTA_PARAMS_OK) {
            return err;
        }
    }
    
    cached_params.app_a_version = version;
    cached_params.app_a_size = size;
    cached_params.app_a_crc32 = crc32;
    cached_params.app_a_valid = valid ? 1 : 0;
    
    return ota_params_write(&cached_params);
}

// 更新App-B信息
ota_params_err_t ota_params_update_app_b(uint32_t version, uint32_t size, uint32_t crc32, uint8_t valid)
{
    ota_params_err_t err;
    
    if (!params_initialized) {
        err = ota_params_init();
        if (err != OTA_PARAMS_OK) {
            return err;
        }
    }
    
    cached_params.app_b_version = version;
    cached_params.app_b_size = size;
    cached_params.app_b_crc32 = crc32;
    cached_params.app_b_valid = valid ? 1 : 0;
    
    return ota_params_write(&cached_params);
}

// 打印参数信息（调试用）
void ota_params_print(const ota_params_t *params)
{
    if (params == NULL) {
        printf("OTA params: NULL\n");
        return;
    }
    
    printf("=== OTA Parameters ===\n");
    printf("Magic: 0x%08lX\n", params->magic_number);
    printf("OTA Flag: %lu\n", params->ota_flag);
    printf("Active App: %lu\n", params->active_app);
    printf("Boot Count: %lu / %lu\n", params->boot_count, params->max_boot_count);
    printf("\n");
    printf("App-A:\n");
    printf("  Version: %lu\n", params->app_a_version);
    printf("  Size: %lu\n", params->app_a_size);
    printf("  CRC32: 0x%08lX\n", params->app_a_crc32);
    printf("  Valid: %s\n", params->app_a_valid ? "Yes" : "No");
    printf("\n");
    printf("App-B:\n");
    printf("  Version: %lu\n", params->app_b_version);
    printf("  Size: %lu\n", params->app_b_size);
    printf("  CRC32: 0x%08lX\n", params->app_b_crc32);
    printf("  Valid: %s\n", params->app_b_valid ? "Yes" : "No");
    printf("======================\n");
}
