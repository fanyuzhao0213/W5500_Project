/**
 * @file watchdog_manager.c
 * @brief 独立看门狗 (IWDG) 管理器实现
 *
 * 功能:
 * - IWDG初始化
 * - 条件喂狗 (仅在MQTT正常运行时间隔喂狗)
 * - 防止卡死情况下继续喂狗
 *
 * @note 工业级设计: 禁止在系统异常时继续喂狗
 *
 * 使用方法:
 * 1. 系统初始化时调用 watchdog_manager_init()
 * 2. 在主循环或定时任务中定期调用 watchdog_manager_feed_auto()
 * 3. 仅当系统正常运行时喂狗
 */

#include "watchdog_manager.h"
#include "stm32f4xx_hal.h"
#include "LOG.h"

/*============================================================================
 * 常量定义
 *============================================================================*/
/** LSI频率 (Hz) */
#define LSI_FREQ_HZ            32000

/**
 * IWDG超时配置
 *
 * Timeout = (Prescaler * Reload) / LSI_Freq
 * 目标: 10秒超时
 * Reload = (Timeout * LSI_Freq) / Prescaler
 *      = (10 * 32000) / 256 = 1250
 */
#define IWDG_PRESCALER         IWDG_PRESCALER_256
#define IWDG_RELOAD_VALUE      1250    /* 10秒超时 @32kHz */

/** 最大喂狗间隔 (毫秒) - 超过此值说明可能卡死 */
#define MAX_FEED_INTERVAL_MS   5000

/** 最小喂狗间隔 (毫秒) - 防止过于频繁 */
#define MIN_FEED_INTERVAL_MS   100

/*============================================================================
 * 静态变量
 *============================================================================*/
/** IWDG句柄 */
static IWDG_HandleTypeDef g_hiwdg;

/** 初始化标志 */
static uint8_t g_watchdog_init = 0;

/** 上次喂狗时间戳 */
static uint32_t g_last_feed_tick = 0;

/** 连续异常计数 (用于判断是否真正异常) */
static uint8_t g_anomaly_count = 0;

/*============================================================================
 * 公共函数
 *============================================================================*/
/**
 * @brief 初始化看门狗
 */
void watchdog_manager_init(void)
{
    /* 检查是否已初始化 */
    if (g_watchdog_init) {
        LOGW("Watchdog: already initialized");
        return;
    }

    /* 初始化IWDG句柄 */
    g_hiwdg.Instance = IWDG;
    g_hiwdg.Init.Prescaler = IWDG_PRESCALER;
    g_hiwdg.Init.Reload = IWDG_RELOAD_VALUE;

    /* 初始化IWDG */
    if (HAL_IWDG_Init(&g_hiwdg) != HAL_OK) {
        LOGE("Watchdog: init failed");
        return;
    }

    g_watchdog_init = 1;
    g_last_feed_tick = HAL_GetTick();
    g_anomaly_count = 0;

    LOGI("Watchdog: initialized (timeout=10s)");
}

/**
 * @brief 喂狗 (条件喂狗)
 *
 * @param system_ok 系统运行正常标志
 * @param interval_ms 自上次调用以来的毫秒数
 *
 * @note 只有当system_ok=true且interval_ms在合理范围内时才喂狗
 */
void watchdog_manager_feed(uint8_t system_ok, uint32_t interval_ms)
{
    /* 检查初始化 */
    if (!g_watchdog_init) {
        return;
    }

    /* 检查间隔是否合理 */
    if (interval_ms < MIN_FEED_INTERVAL_MS) {
        /* 间隔太短,忽略 */
        return;
    }

    if (interval_ms > MAX_FEED_INTERVAL_MS) {
        /* 间隔太长,说明可能有问题,记录异常 */
        g_anomaly_count++;
        LOGW("Watchdog: long interval %ums (anomaly_count=%d)", interval_ms, g_anomaly_count);

        /* 如果连续多次间隔异常,不喂狗让系统复位 */
        if (g_anomaly_count >= 3) {
            LOGE("Watchdog: too many anomalies, stop feeding");
            return;
        }
    } else {
        /* 间隔正常,重置异常计数 */
        g_anomaly_count = 0;
    }

    /* 只有系统正常时才喂狗 */
    if (system_ok) {
        HAL_IWDG_Refresh(&g_hiwdg);
        g_last_feed_tick = HAL_GetTick();
    } else {
        /* 系统异常,不喂狗,让看门狗复位 */
        LOGW("Watchdog: system not OK, not feeding");
    }
}

/**
 * @brief 喂狗 (自动喂狗)
 */
void watchdog_manager_feed_auto(void)
{
    uint32_t current_tick;
    uint32_t interval_ms;

    if (!g_watchdog_init) {
        return;
    }

    current_tick = HAL_GetTick();

    /* 首次调用时初始化时间戳 */
    if (g_last_feed_tick == 0) {
        g_last_feed_tick = current_tick;
        return;
    }

    interval_ms = current_tick - g_last_feed_tick;

    if (interval_ms < MIN_FEED_INTERVAL_MS) {
        return;
    }

    if (interval_ms > MAX_FEED_INTERVAL_MS) {
        g_anomaly_count++;
        LOGW("Watchdog: long interval %ums (anomaly_count=%d)", interval_ms, g_anomaly_count);
        if (g_anomaly_count >= 3) {
            LOGE("Watchdog: too many anomalies, stop feeding");
            return;
        }
    } else {
        g_anomaly_count = 0;
    }

    HAL_IWDG_Refresh(&g_hiwdg);
    g_last_feed_tick = current_tick;
}

/**
 * @brief 获取看门狗是否已初始化
 */
uint8_t watchdog_manager_is_init(void)
{
    return g_watchdog_init;
}

/**
 * @brief 获取看门狗状态字符串
 */
const char* watchdog_manager_get_status_str(void)
{
    if (!g_watchdog_init) {
        return "NOT_INIT";
    }

    if (g_anomaly_count > 0) {
        return "ANOMALY";
    }

    return "OK";
}