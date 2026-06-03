/**
 * @file netconf.c
 * @brief 网络配置实现
 */

#include "netconf.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "dhcp.h"
#include "LOG.h"

#include "LOG.h"

/* 默认网络参数 (静态IP) */
static wiz_NetInfo_t g_net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
    .ip  = {192, 168, 1, 88},
    .sn  = {255, 255, 255, 0},
    .gw  = {192, 168, 1, 1},
    .dns = {8, 8, 8, 8}
};

net_status_t g_net_status = NET_STATUS_DISCONNECTED;

/**
 * @brief 初始化网络配置 (静态IP)
 */
void NetConfig_Init(void)
{
    wiz_NetInfo_t net_info;

    /* 设置网络参数 */
    ctlnetwork(CN_SET_NETINFO, (void*)&g_net_info);

    /* 读取并验证配置 */
    ctlnetwork(CN_GET_NETINFO, (void*)&net_info);

    LOGI("Network Config:");
    LOGI("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
          net_info.mac[0], net_info.mac[1], net_info.mac[2],
          net_info.mac[3], net_info.mac[4], net_info.mac[5]);
    LOGI("  IP:  %d.%d.%d.%d", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    LOGI("  SN:  %d.%d.%d.%d", net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3]);
    LOGI("  GW:  %d.%d.%d.%d", net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);

    g_net_status = NET_STATUS_CONNECTED;
}

/**
 * @brief 处理网络状态
 */
void NetConfig_Process(void)
{
    /* 静态IP模式，不需要 DHCP 处理 */
    /* 预留扩展 */
}

/**
 * @brief 获取当前网络状态
 */
net_status_t NetConfig_GetStatus(void)
{
    return g_net_status;
}

