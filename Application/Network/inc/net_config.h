#ifndef _NET_CONFIG_H_
#define _NET_CONFIG_H_

/* 网络任务优先级 */
#define NET_MANAGER_TASK_PRIORITY    osPriorityHigh
#define MQTT_TASK_PRIORITY           osPriorityAboveNormal
#define APP_TASK_PRIORITY            osPriorityNormal

/* 网络任务栈大小 */
#define NET_MANAGER_TASK_STACK_SIZE  512
#define MQTT_TASK_STACK_SIZE         1024
#define APP_TASK_STACK_SIZE          512

/* MQTT队列配置 */
#define MQTT_TX_QUEUE_SIZE           16
#define MQTT_RX_QUEUE_SIZE           16

/* 重连退避时间 (毫秒) */
#define RECONNECT_DELAY_1            1000
#define RECONNECT_DELAY_2            2000
#define RECONNECT_DELAY_3            5000
#define RECONNECT_DELAY_4            10000
#define RECONNECT_DELAY_5            30000
#define RECONNECT_DELAY_6            60000

/* KeepAlive配置 */
#define MQTT_KEEP_ALIVE_INTERVAL     60
#define MQTT_PING_TIMEOUT            10000

/* 网络检测周期 */
#define NET_LINK_CHECK_INTERVAL      1000
#define NET_IP_CHECK_INTERVAL        5000

#endif /* _NET_CONFIG_H_ */
