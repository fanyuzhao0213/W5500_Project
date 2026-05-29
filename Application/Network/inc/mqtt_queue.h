#ifndef _MQTT_QUEUE_H_
#define _MQTT_QUEUE_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "net_types.h"
#include "net_config.h"

/* MQTT发送队列句柄 */
extern QueueHandle_t g_mqtt_tx_queue;

/* MQTT接收队列句柄 */
extern QueueHandle_t g_mqtt_rx_queue;

/* MQTT队列初始化 */
void mqtt_queue_init(void);

/* 推送消息到发送队列 */
int mqtt_queue_push(const char* topic, const uint8_t* payload, uint16_t len, uint8_t qos);

/* 从发送队列读取消息 */
int mqtt_queue_pop(mqtt_msg_t* msg);

/* 推送消息到接收队列 */
int mqtt_queue_push_rx(const char* topic, const uint8_t* payload, uint16_t len);

/* 从接收队列读取消息 */
int mqtt_queue_pop_rx(mqtt_msg_t* msg);

#endif /* _MQTT_QUEUE_H_ */
