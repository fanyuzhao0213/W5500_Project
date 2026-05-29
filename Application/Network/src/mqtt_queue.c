/**
  ******************************************************************************
  * @file    mqtt_queue.c
  * @author  Network Team
  * @brief   MQTT消息队列实现
  * @details 实现MQTT消息的线程安全队列，用于业务层与MQTT层的解耦
  ******************************************************************************
  */

#include "mqtt_queue.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "LOG.h"
#include "MQTTClient.h"

/* MQTT发送队列句柄 */
QueueHandle_t g_mqtt_tx_queue = NULL;

/* MQTT接收队列句柄 */
QueueHandle_t g_mqtt_rx_queue = NULL;

/**
 * @brief 初始化MQTT消息队列
 * @note 创建发送队列和接收队列
 */
void mqtt_queue_init(void) {
    g_mqtt_tx_queue = xQueueCreate(MQTT_TX_QUEUE_SIZE, sizeof(mqtt_msg_t));
    g_mqtt_rx_queue = xQueueCreate(MQTT_RX_QUEUE_SIZE, sizeof(mqtt_msg_t));

    if (g_mqtt_tx_queue && g_mqtt_rx_queue) {
        LOGI("MQTT Queue: initialized (TX:%d, RX:%d)", MQTT_TX_QUEUE_SIZE, MQTT_RX_QUEUE_SIZE);
    } else {
        LOGE("MQTT Queue: initialization failed - queue creation error");
    }
}

/**
 * @brief 推送消息到发送队列
 * @param topic 消息主题
 * @param payload 消息负载
 * @param len 负载长度
 * @param qos QoS级别
 * @return 0表示成功，-1表示失败
 * @note 非阻塞调用，队列满时返回失败
 */
int mqtt_queue_push(const char* topic, const uint8_t* payload, uint16_t len, uint8_t qos) {
    if (!g_mqtt_tx_queue || !topic || !payload) {
        LOGE("MQTT Queue: Invalid parameters for push");
        return -1;
    }

    mqtt_msg_t msg = {0};

    /* 复制topic（截断过长的topic）*/
    uint16_t topic_len = strlen(topic);
    if (topic_len >= sizeof(msg.topic)) {
        topic_len = sizeof(msg.topic) - 1;
        LOGW("MQTT Queue: Topic truncated from %d to %d chars", strlen(topic), topic_len);
    }
    memcpy(msg.topic, topic, topic_len);
    msg.topic[topic_len] = '\0';

    /* 复制payload（截断过长的payload）*/
    if (len > sizeof(msg.payload)) {
        len = sizeof(msg.payload);
        LOGW("MQTT Queue: Payload truncated from %d to %d bytes", len, sizeof(msg.payload));
    }
    memcpy(msg.payload, payload, len);
    msg.len = len;
    msg.qos = qos;

    /* 非阻塞方式发送到队列 */
    BaseType_t status = xQueueSend(g_mqtt_tx_queue, &msg, 0);

    if (status == pdPASS) {
        LOGD("MQTT Queue: Message queued for topic %s (len=%d)", topic, len);
        return 0;
    } else {
        LOGW("MQTT Queue: TX queue full - message dropped");
        return -1;
    }
}

/**
 * @brief 从发送队列读取消息
 * @param msg 消息结构体指针
 * @return 0表示成功，-1表示失败（队列为空）
 * @note 非阻塞调用
 */
int mqtt_queue_pop(mqtt_msg_t* msg) {
    if (!msg) {
        LOGE("MQTT Queue: Invalid parameters for pop");
        return -1;
    }

    /* 如果队列未初始化，返回空 */
    if (!g_mqtt_tx_queue) {
        return -1;
    }

    /* 非阻塞方式从队列读取 */
    BaseType_t status = xQueueReceive(g_mqtt_tx_queue, msg, 0);

    if (status == pdPASS) {
        return 0;
    }

    return -1;
}

/**
 * @brief 推送消息到接收队列
 * @param topic 消息主题
 * @param payload 消息负载
 * @param len 负载长度
 * @return 0表示成功，-1表示失败
 * @note 非阻塞调用，队列满时返回失败
 */
int mqtt_queue_push_rx(const char* topic, const uint8_t* payload, uint16_t len) {
    if (!g_mqtt_rx_queue || !topic || !payload) {
        LOGE("MQTT Queue: Invalid parameters for push_rx");
        return -1;
    }

    mqtt_msg_t msg = {0};

    /* 复制topic（截断过长的topic）*/
    uint16_t topic_len = strlen(topic);
    if (topic_len >= sizeof(msg.topic)) {
        topic_len = sizeof(msg.topic) - 1;
    }
    memcpy(msg.topic, topic, topic_len);
    msg.topic[topic_len] = '\0';

    /* 复制payload（截断过长的payload）*/
    if (len > sizeof(msg.payload)) {
        len = sizeof(msg.payload);
    }
    memcpy(msg.payload, payload, len);
    msg.len = len;
    msg.qos = QOS0;

    /* 非阻塞方式发送到队列 */
    BaseType_t status = xQueueSend(g_mqtt_rx_queue, &msg, 0);

    if (status == pdPASS) {
        LOGD("MQTT Queue: RX message queued for topic %s (len=%d)", topic, len);
        return 0;
    } else {
        LOGW("MQTT Queue: RX queue full - message dropped");
        return -1;
    }
}

/**
 * @brief 从接收队列读取消息
 * @param msg 消息结构体指针
 * @return 0表示成功，-1表示失败（队列为空）
 * @note 非阻塞调用
 */
int mqtt_queue_pop_rx(mqtt_msg_t* msg) {
    if (!msg) {
        LOGE("MQTT Queue: Invalid parameters for pop_rx");
        return -1;
    }

    /* 如果队列未初始化，返回空 */
    if (!g_mqtt_rx_queue) {
        return -1;
    }

    /* 非阻塞方式从队列读取 */
    BaseType_t status = xQueueReceive(g_mqtt_rx_queue, msg, 0);

    if (status == pdPASS) {
        return 0;
    }

    return -1;
}
