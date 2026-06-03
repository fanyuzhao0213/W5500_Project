/**
 * @file netconf.h
 * @brief 网络配置头文件
 */

#ifndef _NETCONF_H_
#define _NETCONF_H_

#include <stdint.h>

/* 网络配置状态 */
typedef enum {
    NET_STATUS_DISCONNECTED = 0,
    NET_STATUS_CONNECTED    = 1
} net_status_t;

/* 网络参数结构体 */
typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
    uint8_t dns[4];
} wiz_NetInfo_t;

extern net_status_t g_net_status;

/**
 * @brief 初始化网络配置 (静态IP)
 */
void NetConfig_Init(void);

/**
 * @brief 处理网络状态
 */
void NetConfig_Process(void);

/**
 * @brief 获取当前网络状态
 * @return 网络状态
 */
net_status_t NetConfig_GetStatus(void);

#endif /* _NETCONF_H_ */


