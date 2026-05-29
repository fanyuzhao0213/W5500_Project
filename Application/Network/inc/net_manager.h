#ifndef _NET_MANAGER_H_
#define _NET_MANAGER_H_

#include "net_types.h"
#include "net_config.h"

/* 网络管理器状态 */
extern volatile net_state_t g_net_state;

/* 网络管理器初始化 */
void net_manager_init(void);

/* 获取当前网络状态 */
net_state_t net_manager_get_state(void);

/* 网络管理器任务 */
void StartNetManagerTask(void const *argument);

#endif /* _NET_MANAGER_H_ */
