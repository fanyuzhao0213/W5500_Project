/**
 * @file mqtt_exception.c
 * @brief 网络异常处理模块 - 独立线程实现
 */

#include "mqtt_exception.h"
#include "mqtt_task.h"
#include "mqtt_client.h"
#include "mqtt_config.h"
#include "wizchip_conf.h"
#include "netconf.h"
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
#include "dhcp.h"
#endif
#include "LOG.h"
#include "main.h"
#include "w5500_conf.h"

/* ============================================================
 * 外部变量声明
 * ============================================================ */
extern IWDG_HandleTypeDef hiwdg;

/* ============================================================
 * 模块内部变量
 * ============================================================ */
osThreadId g_exception_task_handle = NULL;
exception_status_t g_exception_status;

static uint8_t g_phy_check_count = 0;
static uint32_t g_last_watchdog_feed = 0;
static uint8_t g_dhcp_fail_count = 0;

/* ============================================================
 * 内部函数声明
 * ============================================================ */
static recovery_strategy_t determine_recovery_strategy(exception_type_t type);
static void execute_recovery(recovery_strategy_t strategy);
static void reset_exception_status(void);
static void feed_watchdog_safely(void);

/* ============================================================
 * 初始化模块
 * ============================================================ */
void mqtt_exception_init(void)
{
    reset_exception_status();
    g_phy_check_count = 0;
    g_last_watchdog_feed = 0;
    g_dhcp_fail_count = 0;

    LOGI("MQTT Exception: initialized");
}

/* ============================================================
 * 创建异常处理任务
 * ============================================================ */
void mqtt_exception_task_create(void)
{
    osThreadDef(exceptionTask, StartExceptionTask,
                EXCEPTION_TASK_PRIORITY, 0, EXCEPTION_TASK_STACK);
    g_exception_task_handle = osThreadCreate(osThread(exceptionTask), NULL);

    if (g_exception_task_handle == NULL) {
        LOGE("MQTT Exception: task create failed");
    } else {
        LOGI("MQTT Exception: task created");
    }
}

/* ============================================================
 * 上报异常
 * ============================================================ */
void mqtt_exception_report(exception_type_t type)
{
    if (type == EXCEPTION_NONE) {
        return;
    }

    /* 如果是同一类型异常，只增加计数 */
    if (g_exception_status.type == type) {
        g_exception_status.count++;
        g_exception_status.timestamp = HAL_GetTick();
        LOGW("MQTT Exception: %d repeated (count=%d)", type, g_exception_status.count);
    } else {
        /* 新的异常类型 */
        g_exception_status.type = type;
        g_exception_status.timestamp = HAL_GetTick();
        g_exception_status.count = 1;
        g_exception_status.in_recovery = 0;
        g_exception_status.recovery_attempts = 0;
        LOGE("MQTT Exception: type=%d occurred at %d", type, (int)HAL_GetTick());
    }
}

/* ============================================================
 * 清除异常
 * ============================================================ */
void mqtt_exception_clear(void)
{
    if (g_exception_status.type != EXCEPTION_NONE) {
        LOGI("MQTT Exception: cleared");
        reset_exception_status();
    }
}

/* ============================================================
 * 重置异常处理模块
 * ============================================================ */
void mqtt_exception_reset(void)
{
    LOGW("MQTT Exception: resetting all exception states");
    reset_exception_status();
    g_phy_check_count = 0;
    g_last_watchdog_feed = 0;
    LOGI("MQTT Exception: reset complete");
}

/* ============================================================
 * 重置 DHCP 失败计数
 * @note DHCP 成功获取 IP 后调用
 * ============================================================ */
void mqtt_exception_reset_dhcp_count(void)
{
    if (g_dhcp_fail_count > 0) {
        LOGI("MQTT Exception: DHCP success, resetting fail count from %d to 0", g_dhcp_fail_count);
        g_dhcp_fail_count = 0;
    }
}

/* ============================================================
 * 获取异常状态
 * ============================================================ */
exception_status_t* mqtt_exception_get_status(void)
{
    return &g_exception_status;
}

/* ============================================================
 * 异常处理任务主循环
 * ============================================================ */
void StartExceptionTask(void const * argument)
{
    uint32_t last_check = 0;
    uint8_t phy_link;

    (void)argument;
    LOGI("MQTT Exception: task started");

    for (;;) {
        /* 按检查间隔运行 */
        if (HAL_GetTick() - last_check >= EXCEPTION_CHECK_INTERVAL) {
            last_check = HAL_GetTick();

            /* =====================================================
             * 1. 监测 PHY 链路状态
             * ===================================================== */
            phy_link = wizphy_getphylink();

            if (phy_link != PHY_LINK_ON) {
                g_phy_check_count++;

                if (g_phy_check_count >= PHY_CHECK_THRESHOLD) {
                    if (g_exception_status.type != EXCEPTION_PHY_LINK_DOWN) {
                        mqtt_exception_report(EXCEPTION_PHY_LINK_DOWN);
                    }
                }

                LOGW("MQTT Exception: PHY link down (check=%d)", g_phy_check_count);
            } else {
                /* PHY 链路恢复 */
                if (g_phy_check_count > 0) {
                    LOGI("MQTT Exception: PHY link recovered");
                    g_phy_check_count = 0;

                    /* 如果是 PHY 异常，清除 */
                    if (g_exception_status.type == EXCEPTION_PHY_LINK_DOWN) {
                        mqtt_exception_clear();
                    }
                }
            }

            /* =====================================================
             * 2. 根据当前异常状态执行处理
             * ===================================================== */
            if (g_exception_status.type != EXCEPTION_NONE && !g_exception_status.in_recovery) {
                recovery_strategy_t strategy = determine_recovery_strategy(g_exception_status.type);

                if (strategy != RECOVERY_NONE) {
                    g_exception_status.in_recovery = 1;
                    LOGW("MQTT Exception: executing recovery strategy=%d", strategy);
                    execute_recovery(strategy);
                }
            }

            /* =====================================================
             * 3. 安全喂狗（只有 MQTT 正常运行才喂狗）
             * ===================================================== */
            feed_watchdog_safely();
        }

        osDelay(10);
    }
}

/* ============================================================
 * 内部：确定恢复策略
 * ============================================================ */
static recovery_strategy_t determine_recovery_strategy(exception_type_t type)
{
    switch (type) {
        case EXCEPTION_PHY_LINK_DOWN:
            /* PHY 断开，等 PHY 恢复后全复位 */
            return RECOVERY_FULL_RESET;

        case EXCEPTION_W5500_ERROR:
            return RECOVERY_RESET_W5500;

        case EXCEPTION_DHCP_FAILED:
        case EXCEPTION_DHCP_TIMEOUT:
            return RECOVERY_RESTART_DHCP;

        case EXCEPTION_MQTT_DISCONNECTED:
            return RECOVERY_RETRY_MQTT;

        case EXCEPTION_MQTT_PUBLISH_FAILED:
        case EXCEPTION_MQTT_SUBSCRIBE_FAILED:
            /* 发布/订阅失败多次才处理 */
            if (g_exception_status.count >= 5) {
                return RECOVERY_RETRY_MQTT;
            }
            return RECOVERY_NONE;

        case EXCEPTION_NETWORK_ERROR:
            return RECOVERY_FULL_RESET;

        default:
            return RECOVERY_NONE;
    }
}

/* ============================================================
 * 内部：执行恢复策略
 * ============================================================ */
static void execute_recovery(recovery_strategy_t strategy)
{
    g_exception_status.recovery_attempts++;

    if (g_exception_status.recovery_attempts > MAX_RECOVERY_ATTEMPTS) {
        LOGE("MQTT Exception: max recovery attempts reached, triggering watchdog reset");
        /* 复位 */
        HAL_NVIC_SystemReset();
        return;
    }

    /* 等待一段时间再恢复 */
    osDelay(RECOVERY_DELAY_MS);

    /* 检查：如果在等待期间问题已自行恢复（如 PHY 链路重新连接），
     * 则取消恢复操作，避免干扰正常的网络任务流程
     * 注意：此时 type 已被 mqtt_exception_clear() 清除为 EXCEPTION_NONE，
     *       但 in_recovery 仍保持为 1（因为 reset_exception_status 中已注释掉） */
    if (g_exception_status.type == EXCEPTION_NONE) {
        LOGW("MQTT Exception: recovery cancelled - issue resolved during delay");
        g_exception_status.in_recovery = 0;
        return;
    }

    switch (strategy) {
        case RECOVERY_RETRY_MQTT:
            LOGW("MQTT Exception: recovering - retry MQTT connection");
            mqtt_client_disconnect();
            mqtt_task_set_running(0);
            mqtt_exception_clear();
            mqtt_task_set_state(NET_STATE_MQTT_RECONNECT);
            break;

        case RECOVERY_RESTART_DHCP:
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
            g_dhcp_fail_count++;
            LOGW("MQTT Exception: recovering - restart DHCP (attempt %d)", g_dhcp_fail_count);

            /* DHCP 连续失败 3 次，触发系统复位 */
            if (g_dhcp_fail_count >= 3) {
                LOGE("MQTT Exception: DHCP failed %d times, triggering system reset!",
                     g_dhcp_fail_count);
                /* 停止喂狗，看门狗会在约 2 秒后复位系统 */
                HAL_NVIC_SystemReset();
            }

            DHCP_stop();
            mqtt_exception_clear();
            mqtt_task_set_state(NET_STATE_INIT);
#endif
            break;
        case RECOVERY_RESET_W5500:
            LOGW("MQTT Exception: recovering - reset W5500");
            wizchip_sw_reset();
            HAL_Delay(100);
            mqtt_client_disconnect();
            mqtt_exception_clear();
            mqtt_task_set_state(NET_STATE_INIT);
            break;

        case RECOVERY_FULL_RESET:
            LOGW("MQTT Exception: recovering - full reset");
            LOGW("MQTT Exception: resetting W5500 and restarting network...");
            /* 1. 停止 DHCP */
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
            DHCP_stop();
#endif
            /* 2. 断开 MQTT 连接 */
            mqtt_client_disconnect();
            /* 3. 软件复位 W5500 */
            wizchip_sw_reset();
            HAL_Delay(100);
            /* 4. 清除异常状态 */
            mqtt_exception_clear();
            /* 5. 通知网络任务回到初始状态重新开始 */
            mqtt_task_set_state(NET_STATE_INIT);
            LOGI("MQTT Exception: full reset complete, network will restart");
            break;

        case RECOVERY_WATCHDOG_RESET:
            LOGE("MQTT Exception: triggering watchdog reset");
            /* 停止喂狗，系统会复位 */
            break;

        default:
            break;
    }

    g_exception_status.in_recovery = 0;
}

/* ============================================================
 * 内部：重置异常状态
 * ============================================================ */
static void reset_exception_status(void)
{
    g_exception_status.type = EXCEPTION_NONE;
    g_exception_status.timestamp = 0;
    g_exception_status.count = 0;
    g_exception_status.in_recovery = 0;
    // g_exception_status.recovery_attempts = 0;
}

/* ============================================================
 * 内部：安全喂狗策略
 * ============================================================ */
static void feed_watchdog_safely(void)
{
    /* 只有 MQTT 正常运行，且无异常时才喂狗 */
    if (mqtt_is_running() && g_exception_status.type == EXCEPTION_NONE) {
        if (HAL_GetTick() - g_last_watchdog_feed >= 1000) {
            HAL_IWDG_Refresh(&hiwdg);
            g_last_watchdog_feed = HAL_GetTick();
        }
    } else {
        /* 异常情况下，喂狗间隔加倍，给恢复时间 */
        if (HAL_GetTick() - g_last_watchdog_feed >= 2000) {
            HAL_IWDG_Refresh(&hiwdg);
            g_last_watchdog_feed = HAL_GetTick();
        }
    }
}
