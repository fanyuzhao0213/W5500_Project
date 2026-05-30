/**
 * @file ota_example.c
 * @brief OTA模块使用示例
 */

#include "ota.h"
#include <stdio.h>

// 示例1：Flash驱动基本操作
void example_flash_driver(void)
{
    flash_err_t err;
    uint8_t test_data[32] = "Hello, OTA Flash!";
    uint8_t read_buf[32] = {0};
    uint32_t crc;
    
    printf("=== Flash Driver Example ===\n");
    
    // 解锁Flash
    err = flash_unlock();
    if (err != FLASH_OK) {
        printf("Flash unlock failed: %d\n", err);
        return;
    }
    
    // 擦除测试区域（App-B开头的一个扇区）
    printf("Erasing Flash sector...\n");
    err = flash_erase_range(APP_B_ADDR, FLASH_SECTOR_SIZE_16KB);
    if (err != FLASH_OK) {
        printf("Flash erase failed: %d\n", err);
        flash_lock();
        return;
    }
    printf("Flash erase success!\n");
    
    // 写入测试数据
    printf("Writing data...\n");
    err = flash_write(APP_B_ADDR, test_data, sizeof(test_data));
    if (err != FLASH_OK) {
        printf("Flash write failed: %d\n", err);
        flash_lock();
        return;
    }
    printf("Flash write success!\n");
    
    // 读取验证
    printf("Reading data...\n");
    flash_read(APP_B_ADDR, read_buf, sizeof(read_buf));
    
    // 比较数据
    if (memcmp(test_data, read_buf, sizeof(test_data)) == 0) {
        printf("Data verification success! Read: %s\n", read_buf);
    } else {
        printf("Data verification failed!\n");
    }
    
    // 计算CRC32
    printf("Calculating CRC32...\n");
    crc = flash_calc_crc32(APP_B_ADDR, sizeof(test_data));
    printf("CRC32: 0x%08lX\n", crc);
    
    // 加锁Flash
    flash_lock();
    printf("Flash locked.\n");
}

// 示例2：OTA参数读写操作
void example_ota_params(void)
{
    ota_params_err_t err;
    ota_params_t params;
    uint32_t flag, active_app, boot_count;
    
    printf("\n=== OTA Params Example ===\n");
    
    // 初始化OTA参数
    printf("Initializing OTA params...\n");
    err = ota_params_init();
    if (err != OTA_PARAMS_OK) {
        printf("OTA params init failed: %d\n", err);
        return;
    }
    printf("OTA params init success!\n");
    
    // 读取并打印当前参数
    ota_params_read(&params);
    ota_params_print(&params);
    
    // 测试设置OTA标志
    printf("\nSetting OTA flag to PENDING...\n");
    err = ota_params_set_flag(OTA_FLAG_PENDING);
    if (err != OTA_PARAMS_OK) {
        printf("Set flag failed: %d\n", err);
        return;
    }
    flag = ota_params_get_flag();
    printf("OTA Flag now: %lu\n", flag);
    
    // 测试设置激活App
    printf("\nSetting active app to App-B...\n");
    err = ota_params_set_active_app(APP_B);
    if (err != OTA_PARAMS_OK) {
        printf("Set active app failed: %d\n", err);
        return;
    }
    active_app = ota_params_get_active_app();
    printf("Active app now: %lu\n", active_app);
    
    // 测试启动计数
    printf("\nTesting boot count...\n");
    for (int i = 0; i < 3; i++) {
        err = ota_params_inc_boot_count();
        boot_count = ota_params_get_boot_count();
        printf("Boot count: %lu\n", boot_count);
    }
    
    // 重置启动计数
    printf("\nResetting boot count...\n");
    ota_params_reset_boot_count();
    boot_count = ota_params_get_boot_count();
    printf("Boot count now: %lu\n", boot_count);
    
    // 测试更新App-A信息
    printf("\nUpdating App-A info...\n");
    err = ota_params_update_app_a(20240530, 102400, 0x12345678, 1);
    if (err != OTA_PARAMS_OK) {
        printf("Update App-A failed: %d\n", err);
        return;
    }
    printf("App-A info updated!\n");
    
    // 读取最终状态
    printf("\nFinal params:\n");
    ota_params_read(&params);
    ota_params_print(&params);
    
    // 恢复正常状态
    ota_params_set_flag(OTA_FLAG_NORMAL);
    ota_params_set_active_app(APP_A);
}

// 示例3：模拟升级准备流程
void example_upgrade_prepare(void)
{
    ota_params_err_t err;
    uint32_t new_version = 20240530;
    uint32_t firmware_size = 245760;
    uint32_t firmware_crc32 = 0xA1B2C3D4;
    
    printf("\n=== Upgrade Prepare Example ===\n");
    
    // 假设我们已经下载好固件到App-B
    printf("Download completed to App-B!\n");
    
    // 更新App-B信息
    printf("Updating App-B info...\n");
    err = ota_params_update_app_b(new_version, firmware_size, firmware_crc32, 1);
    if (err != OTA_PARAMS_OK) {
        printf("Update App-B failed: %d\n", err);
        return;
    }
    printf("App-B info updated!\n");
    
    // 设置升级标志
    printf("Setting upgrade pending flag...\n");
    err = ota_params_set_flag(OTA_FLAG_PENDING);
    if (err != OTA_PARAMS_OK) {
        printf("Set OTA flag failed: %d\n", err);
        return;
    }
    
    // 设置下次启动到App-B
    printf("Setting active app to App-B...\n");
    err = ota_params_set_active_app(APP_B);
    if (err != OTA_PARAMS_OK) {
        printf("Set active app failed: %d\n", err);
        return;
    }
    
    // 重置启动计数
    printf("Resetting boot count...\n");
    ota_params_reset_boot_count();
    
    printf("\nUpgrade prepared! Ready for reboot.\n");
    printf("On next boot, Bootloader will verify and run App-B.\n");
}

// 运行所有示例
void ota_example_run_all(void)
{
    printf("\n");
    printf("================================\n");
    printf("  OTA Module Examples\n");
    printf("================================\n");
    printf("\n");
    
    // 示例1：Flash驱动
    example_flash_driver();
    
    // 示例2：OTA参数
    example_ota_params();
    
    // 示例3：升级准备流程
    example_upgrade_prepare();
    
    printf("\n");
    printf("================================\n");
    printf("  Examples finished!\n");
    printf("================================\n");
    printf("\n");
}
