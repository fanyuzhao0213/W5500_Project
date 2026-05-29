/**
 * @file watchdog_manager.h
 * @brief 独立看门狗 (IWDG) 管理器接口
 *
 * 功能:
 * - IWDG初始化
 * - 条件喂狗 (仅在MQTT正常运行时间隔喂狗)
 * - 防止卡死情况下继续喂狗
 *
 * @note 工业级设计: 禁止在系统异常时继续喂狗
 */

#ifndef WATCHDOG_MANAGER_H_
#define WATCHDOG_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================================================================
 * 宏定义
 *============================================================================*/
#define IWDG_TIMEOUT_SECONDS     10      /**< 看门狗超时时间 (秒) */

/*============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 初始化看门狗
 * @note 必须在系统正常运行时调用一次
 */
void watchdog_manager_init(void);

/**
 * @brief 喂狗 (条件喂狗)
 * @param system_ok 系统运行正常标志
 * @param interval_ms 自上次调用以来的毫秒数
 *
 * @note 只有当system_ok=true且interval_ms在合理范围内时才喂狗
 *       这确保了如果系统卡在异常状态,看门狗会超时复位
 */
void watchdog_manager_feed(uint8_t system_ok, uint32_t interval_ms);

/**
 * @brief 喂狗 (自动喂狗)
 * @note 自动计算上次喂狗到现在的时间间隔,简化调用
 *       适用于在主循环中定期调用
 */
void watchdog_manager_feed_auto(void);

/**
 * @brief 获取看门狗是否已初始化
 * @return 1已初始化, 0未初始化
 */
uint8_t watchdog_manager_is_init(void);

/**
 * @brief 获取看门狗状态字符串
 * @return 状态描述
 */
const char* watchdog_manager_get_status_str(void);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_MANAGER_H_ */

