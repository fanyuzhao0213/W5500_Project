/**
  ******************************************************************************
  * @file    net_manager.c
  * @author  Network Team
  * @brief   网络管理器实现
  * @details 负责PHY层检测、DHCP管理、IP状态监控，实现网络状态机
  ******************************************************************************
  */

#include "net_manager.h"
#include "LOG.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "netconf.h"
#include "tcp_client.h"
#include "cmsis_os.h"
/* 网络状态机全局状态 */
volatile net_state_t g_net_state = NET_STATE_DOWN;

/* 检测定时器 */
static uint32_t last_link_check = 0;
static uint32_t last_ip_check = 0;

/**
 * @brief 初始化网络管理器
 * @note 将网络状态重置为DOWN
 */
void net_manager_init(void) {
    g_net_state = NET_STATE_DOWN;
    LOGI("NetManager: initialized");
}

/**
 * @brief 获取当前网络状态
 * @return 当前网络状态
 */
net_state_t net_manager_get_state(void) {
    return g_net_state;
}

/**
 * @brief 处理网络事件
 * @param event 网络事件类型
 * @details 根据事件类型更新网络状态，执行相应的处理逻辑
 */
static void net_manager_handle_event(net_event_t event) {
    switch(event) {
        case NET_EVENT_LINK_UP:
            LOGI("NET: LINK UP - Ethernet cable connected");
            LOGI("NET: Starting DHCP configuration...");
            g_net_state = NET_STATE_LINK_UP;
            /* 启动DHCP */
            NetConfig_Init();
            break;

        case NET_EVENT_LINK_DOWN:
            LOGW("NET: LINK DOWN - Ethernet cable disconnected");
            LOGW("NET: Closing all TCP connections");
            g_net_state = NET_STATE_DOWN;
            /* 关闭所有TCP连接 */
            tcp_client_disconnect();
            break;

        case NET_EVENT_IP_OK:
            LOGI("NET: IP OK - DHCP configuration successful");
            LOGI("NET: Network ready for MQTT connection");
            g_net_state = NET_STATE_IP_OK;
            break;

        case NET_EVENT_TCP_CONNECTED:
            LOGI("NET: TCP connection established");
            break;

        case NET_EVENT_TCP_DISCONNECTED:
            LOGW("NET: TCP connection lost");
            break;

        default:
            LOGW("NET: Unknown event %d", event);
            break;
    }
}

/**
 * @brief 网络管理器任务入口
 * @param argument 任务参数（未使用）
 * @details 周期性检测PHY连接状态和IP配置状态
 */
void StartNetManagerTask(void const *argument) {
    uint8_t link_status = 0;
    uint8_t prev_link_status = 0;

    LOGI("NetManagerTask: started");
    LOGI("NetManagerTask: monitoring PHY and IP status...");

    while(1) {
        /* 周期性检测PHY连接状态 */
        if (HAL_GetTick() - last_link_check >= NET_LINK_CHECK_INTERVAL) {
            last_link_check = HAL_GetTick();

            /* 读取PHY连接状态 */
            link_status = wizphy_getphylink();

            /* 检测状态变化 */
            if (link_status != prev_link_status) {
                prev_link_status = link_status;

                if (link_status) {
                    /* 网线连接 */
                    net_manager_handle_event(NET_EVENT_LINK_UP);
                } else {
                    /* 网线断开 */
                    net_manager_handle_event(NET_EVENT_LINK_DOWN);
                }
            }
        }

        /* 当链路UP时，检测IP状态 */
        if (g_net_state == NET_STATE_LINK_UP &&
            HAL_GetTick() - last_ip_check >= NET_IP_CHECK_INTERVAL) {
            last_ip_check = HAL_GetTick();

            /* 检查IP是否已配置 */
            if (NetConfig_CheckIP()) {
                net_manager_handle_event(NET_EVENT_IP_OK);
            } else {
                LOGD("NET: Waiting for IP address...");
            }
        }

        /* 降低任务频率 */
        osDelay(100);
    }
}

