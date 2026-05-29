#ifndef _NET_TYPES_H_
#define _NET_TYPES_H_

#include <stdint.h>

/* 网络状态枚举 */
typedef enum {
    NET_STATE_DOWN = 0,
    NET_STATE_LINK_UP,
    NET_STATE_IP_OK,
} net_state_t;

/* MQTT状态枚举 */
typedef enum {
    MQTT_STATE_IDLE = 0,
    MQTT_STATE_TCP_CONNECT,
    MQTT_STATE_MQTT_CONNECT,
    MQTT_STATE_SUBSCRIBE,
    MQTT_STATE_RUNNING,
    MQTT_STATE_RECONNECT_WAIT,
} mqtt_state_t;

/* 网络事件枚举 */
typedef enum {
    NET_EVENT_NONE = 0,
    NET_EVENT_LINK_UP,
    NET_EVENT_LINK_DOWN,
    NET_EVENT_IP_OK,
    NET_EVENT_IP_LOST,
    NET_EVENT_TCP_CONNECTED,
    NET_EVENT_TCP_DISCONNECTED,
    NET_EVENT_MQTT_CONNECTED,
    NET_EVENT_MQTT_DISCONNECTED,
} net_event_t;

/* MQTT消息结构 */
typedef struct {
    char topic[64];
    uint8_t payload[256];
    uint16_t len;
    uint8_t qos;
} mqtt_msg_t;

#endif /* _NET_TYPES_H_ */
