/**
 * @file net_manager.h
 * @brief NetManagerTask接口 - 负责PHY检测、DHCP维护、IP状态
 */

#ifndef NET_MANAGER_H_
#define NET_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "network_types.h"

/*============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 初始化网络管理器
 * @note 必须在其他网络任务之前调用
 */
void net_manager_init(void);

/**
 * @brief 网络管理器任务主函数
 * @param argument 任务参数
 */
void net_manager_task(void const * argument);

/**
 * @brief 获取当前网络状态
 * @return 网络状态
 */
net_state_t net_manager_get_state(void);

/**
 * @brief 获取当前网络状态字符串描述
 * @return 状态字符串
 */
const char* net_manager_get_state_str(void);

/**
 * @brief 检查PHY Link状态
 * @return 1 Link UP, 0 Link DOWN
 */
int net_manager_check_phy_link(void);

/**
 * @brief 获取当前IP地址
 * @param ip_out 输出缓冲区 (4字节)
 * @return 0成功, -1失败
 */
int net_manager_get_ip(uint8_t ip_out[4]);

/**
 * @brief 标记需要重新获取IP
 * @note DHCP模式下触发重新DHCP
 */
void net_manager_request_ip_renew(void);

/**
 * @brief 获取自以来网络恢复后的运行时间
 * @return 毫秒数
 */
uint32_t net_manager_get_uptime_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_MANAGER_H_ */

