/**
 * @file watchdog_manager.h
 * @brief 独立看门狗 (IWDG) 管理器接口
 *
 * 功能:
 * - IWDG初始化 (直接寄存器操作)
 * - 条件喂狗 (防止卡死情况下继续喂狗)
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
 * @brief 喂狗 (自动喂狗)
 * @note 简化调用,适用于空闲任务钩子
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