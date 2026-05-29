/**
 * @file mqtt_client.h
 * @brief MQTT客户端接口
 */

#ifndef MQTT_CLIENT_H_
#define MQTT_CLIENT_H_

#include <stdint.h>

/* MQTT配置 */
#define MQTT_CLIENT_ID       "STM32_W5500"
#define MQTT_KEEPALIVE       60
#define MQTT_QOS_0           0
#define MQTT_QOS_1           1
#define MQTT_QOS_2           2

/* MQTT状态 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_SUBSCRIBED,
    MQTT_STATE_ERROR
} mqtt_state_t;

/* 订阅消息回调 */
typedef void (*mqtt_msg_callback_t)(const char* topic, uint8_t* payload, uint16_t len);

/**
 * @brief 初始化MQTT客户端
 */
void mqtt_client_init(void);

/**
 * @brief 连接MQTT Broker
 * @return 0成功, 负值失败
 */
int mqtt_client_connect(void);

/**
 * @brief 订阅主题
 * @param topic 主题名
 * @param qos QoS级别
 * @return 0成功, 负值失败
 */
int mqtt_client_subscribe(const char* topic, uint8_t qos);

/**
 * @brief 发布消息
 * @param topic 主题名
 * @param payload 数据
 * @param len 数据长度
 * @param qos QoS级别
 * @param retained 保留标志
 * @return 0成功, 负值失败
 */
int mqtt_client_publish(const char* topic, uint8_t* payload, uint16_t len, uint8_t qos, uint8_t retained);

/**
 * @brief 设置消息回调
 * @param callback 回调函数
 */
void mqtt_client_set_callback(mqtt_msg_callback_t callback);

/**
 * @brief 处理MQTT消息循环 (需要在主循环中调用)
 * @return 0正常, 负值断开
 */
int mqtt_client_loop(void);

/**
 * @brief 断开MQTT连接
 */
void mqtt_client_disconnect(void);

/**
 * @brief 获取MQTT状态
 */
mqtt_state_t mqtt_client_get_state(void);

#endif /* MQTT_CLIENT_H_ */

