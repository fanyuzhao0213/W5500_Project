#include "watchdog.h"
#include "LOG.h"
#include "mqtt_fsm.h"

IWDG_HandleTypeDef hiwdg;

/**
 *==============================================================================
 * 看门狗 (IWDG) 配置
 *==============================================================================
 *
 * IWDG 超时计算公式:
 *   Timeout = (Prescaler × Reload) / LSI_Frequency
 *
 * IWDG 预分频设置:
 *   IWDG_PRESCALER_4   = 0x00  -> 分频 4
 *   IWDG_PRESCALER_8   = 0x01  -> 分频 8
 *   IWDG_PRESCALER_16  = 0x02  -> 分频 16
 *   IWDG_PRESCALER_32  = 0x03  -> 分频 32
 *   IWDG_PRESCALER_64  = 0x04  -> 分频 64
 *   IWDG_PRESCALER_128 = 0x05  -> 分频 128
 *   IWDG_PRESCALER_256 = 0x06  -> 分频 256  (常用)
 *
 * LSI 频率: 约 32kHz (实际范围 30kHz ~ 40kHz)
 *
 * 计算示例 - 目标 10秒超时:
 *   Reload = (Timeout × LSI_Freq) / Prescaler
 *         = (10 × 32000) / 256
 *         = 1250
 *
 * 常用超时配置:
 *   1秒:  Reload = (1 × 32000) / 256 = 125
 *   5秒:  Reload = (5 × 32000) / 256 = 625
 *   10秒: Reload = (10 × 32000) / 256 = 1250
 *   20秒: Reload = (20 × 32000) / 256 = 2500 (超过最大值 0xFFF)
 *
 *==============================================================================
 */
void watchdog_init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload = 1250; 
    
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        LOGE("Watchdog: initialization failed");
    } else {
        LOGI("Watchdog: initialized");
    }
}

void watchdog_feed(void) {
    HAL_IWDG_Refresh(&hiwdg);
}

void watchdog_check_and_feed(void) {
    /* 只有MQTT正常运行时才喂狗 */
    if (mqtt_fsm_get_state() == MQTT_STATE_RUNNING) {
        watchdog_feed();
    } else {
        LOGW("Watchdog: MQTT not running, not feeding");
    }
}


