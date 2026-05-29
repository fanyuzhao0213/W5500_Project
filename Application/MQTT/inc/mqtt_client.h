#ifndef _MQTT_CLIENT_H_
#define _MQTT_CLIENT_H_

#include "mqtt_config.h"
#include "MQTTClient.h"

/* 外部变量声明 - MQTT客户端实例 */
extern MQTTClient mqtt_client;
extern Network mqtt_network;
extern unsigned char mqtt_send_buf[MQTT_SEND_BUF_SIZE];
extern unsigned char mqtt_read_buf[MQTT_READ_BUF_SIZE];

/* MQTT初始化 */
void mqtt_client_init(void);

/* MQTT连接 */
int mqtt_client_connect(void);

/* MQTT订阅 */
int mqtt_client_subscribe(const char* topicFilter, enum QoS qos, messageHandler handler);

/* MQTT发布 */
int mqtt_client_publish(const char* topicName, const char* payload, size_t payloadlen, enum QoS qos);

/* MQTT循环处理 */
int mqtt_client_loop(int timeout_ms);

/* MQTT断开连接 */
int mqtt_client_disconnect(void);

#endif /* _MQTT_CLIENT_H_ */

