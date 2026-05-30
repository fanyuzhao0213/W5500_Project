/**
 * @file mqtt_task.h
 * @brief MQTT 任务声明
 */

#ifndef _MQTT_TASK_H_
#define _MQTT_TASK_H_

#include "cmsis_os.h"

/* 网络状态机状态 */
typedef enum {
    NET_STATE_INIT = 0,
    NET_STATE_W5500_CHECK,
    NET_STATE_PHY_LINK_CHECK,
    NET_STATE_DHCP_START,
    NET_STATE_DHCP_RUN,
    NET_STATE_NET_CONFIG,
    NET_STATE_MQTT_INIT,
    NET_STATE_MQTT_CONNECT,
    NET_STATE_MQTT_SUBSCRIBE,
    NET_STATE_RUNNING,
    NET_STATE_ERROR
} net_state_t;

/* 任务堆栈大小 */
#define NETWORK_TASK_STACK      512
#define MQTT_TX_TASK_STACK      1024
#define MQTT_RX_TASK_STACK      1024

/* 任务优先级 */
#define NETWORK_TASK_PRIORITY   osPriorityNormal
#define MQTT_TX_TASK_PRIORITY   osPriorityHigh
#define MQTT_RX_TASK_PRIORITY   osPriorityHigh

/* 任务句柄外部声明 */
extern osThreadId g_network_task_handle;
extern osThreadId g_mqtt_tx_task_handle;
extern osThreadId g_mqtt_rx_task_handle;

/**
 * @brief 创建所有 MQTT 相关任务
 * @note 在 FreeRTOS 初始化后调用
 */
void mqtt_task_create(void);

/**
 * @brief 获取当前网络状态
 * @return 网络状态
 */
net_state_t mqtt_task_get_state(void);

/**
 * @brief MQTT是否已连接并运行
 * @return 1 运行中, 0 未连接
 */
int mqtt_is_running(void);

/**
 * @brief 网络任务 - W5500初始化和MQTT协议循环
 */
void StartNetworkTask(void const * argument);

/**
 * @brief MQTT发送任务 - 从发送队列取数据并发布
 */
void StartMQTTTxTask(void const * argument);

/**
 * @brief MQTT接收任务 - 从接收队列取数据并处理
 */
void StartMQTTRxTask(void const * argument);

#endif /* _MQTT_TASK_H_ */

