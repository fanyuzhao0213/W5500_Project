#ifndef _MQTT_FSM_H_
#define _MQTT_FSM_H_

#include "net_types.h"
#include "net_config.h"
#include "mqtt_config.h"

/* MQTT状态机状态 */
extern volatile mqtt_state_t g_mqtt_state;

/* MQTT状态机初始化 */
void mqtt_fsm_init(void);

/* 获取当前MQTT状态 */
mqtt_state_t mqtt_fsm_get_state(void);

/* MQTT状态机处理 */
void mqtt_fsm_process(void);

/* MQTT连接断开处理 */
void mqtt_fsm_disconnect(void);

#endif /* _MQTT_FSM_H_ */
