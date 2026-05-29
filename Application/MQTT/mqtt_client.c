/**
 * @file mqtt_client.c
 * @brief MQTT客户端实现 - 基于Paho MQTT库
 */

#include "mqtt_client.h"
#include "transport.h"
#include "MQTTPacket.h"
#include "MQTTConnect.h"
#include "MQTTSubscribe.h"
#include "MQTTPublish.h"
#include "LOG.h"
#include "tcp_client.h"

#include <string.h>
#include <stdio.h>

/* MQTT发送/接收缓冲区 */
#define MQTT_BUF_SIZE     512
static unsigned char g_mqtt_send_buf[MQTT_BUF_SIZE];
static unsigned char g_mqtt_recv_buf[MQTT_BUF_SIZE];

/* MQTT状态 */
static mqtt_state_t g_mqtt_state = MQTT_STATE_DISCONNECTED;

/* 消息回调 */
static mqtt_msg_callback_t g_msg_callback = NULL;

/* Packet ID */
static uint16_t g_packet_id = 1;

/**
 * @brief 获取新的Packet ID
 */
static uint16_t mqtt_get_packet_id(void)
{
    if (++g_packet_id == 0) {
        g_packet_id = 1;
    }
    return g_packet_id;
}

/**
 * @brief 初始化MQTT客户端
 */
void mqtt_client_init(void)
{
    g_mqtt_state = MQTT_STATE_DISCONNECTED;
    g_msg_callback = NULL;
    g_packet_id = 1;
    LOGI("MQTT: Client initialized");
}

/**
 * @brief 连接MQTT Broker
 */
int mqtt_client_connect(void)
{
    int ret;
    MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
    unsigned char sessionPresent = 0;
    unsigned char connack_rc = 0;

    if (!tcp_client_is_connected()) {
        LOGE("MQTT: TCP not connected!");
        return -1;
    }

    g_mqtt_state = MQTT_STATE_CONNECTING;

    /* 打开transport (W5500已连接,仅保存socket) */
    transport_open(NULL, 0);

    /* 构造CONNECT报文 */
    options.MQTTVersion = 4;  /* MQTT 3.1.1 */
    options.clientID.cstring = MQTT_CLIENT_ID;
    options.keepAliveInterval = MQTT_KEEPALIVE;
    options.cleansession = 1;

    int len = MQTTSerialize_connect(g_mqtt_send_buf, MQTT_BUF_SIZE, &options);
    if (len <= 0) {
        LOGE("MQTT: Serialize connect failed!");
        g_mqtt_state = MQTT_STATE_ERROR;
        return -2;
    }

    /* 发送CONNECT报文 */
    ret = transport_sendPacketBuffer(0, g_mqtt_send_buf, len);
    if (ret != len) {
        LOGE("MQTT: Send connect failed!");
        g_mqtt_state = MQTT_STATE_ERROR;
        return -3;
    }
    LOGI("MQTT: CONNECT sent");

    /* 等待CONNACK响应 */
    len = transport_getdata(g_mqtt_recv_buf, MQTT_BUF_SIZE);
    if (len <= 0) {
        LOGE("MQTT: No connack received!");
        g_mqtt_state = MQTT_STATE_ERROR;
        return -4;
    }

    /* 解析CONNACK */
    ret = MQTTDeserialize_connack(&sessionPresent, &connack_rc, g_mqtt_recv_buf, len);
    if (ret != 1) {
        LOGE("MQTT: Deserialize connack failed!");
        g_mqtt_state = MQTT_STATE_ERROR;
        return -5;
    }

    if (connack_rc != MQTT_CONNECTION_ACCEPTED) {
        LOGE("MQTT: Connection refused, rc=%d", connack_rc);
        g_mqtt_state = MQTT_STATE_ERROR;
        return -6;
    }

    g_mqtt_state = MQTT_STATE_CONNECTED;
    LOGI("MQTT: Connected! (sessionPresent=%d)", sessionPresent);
    return 0;
}

/**
 * @brief 订阅主题
 */
int mqtt_client_subscribe(const char* topic, uint8_t qos)
{
    int ret;
    int len;
    MQTTString topicFilters[1];
    int requestedQoSs[1];
    unsigned short packet_id;
    int grantedQoS = 0;
    int count = 1;

    if (g_mqtt_state != MQTT_STATE_CONNECTED && g_mqtt_state != MQTT_STATE_SUBSCRIBED) {
        LOGE("MQTT: Not connected, cannot subscribe");
        return -1;
    }

    topicFilters[0].cstring = (char*)topic;
    requestedQoSs[0] = qos;

    packet_id = mqtt_get_packet_id();
    len = MQTTSerialize_subscribe(g_mqtt_send_buf, MQTT_BUF_SIZE, 0, packet_id, count, topicFilters, requestedQoSs);
    if (len <= 0) {
        LOGE("MQTT: Serialize subscribe failed!");
        return -2;
    }

    ret = transport_sendPacketBuffer(0, g_mqtt_send_buf, len);
    if (ret != len) {
        LOGE("MQTT: Send subscribe failed!");
        return -3;
    }
    LOGI("MQTT: SUBSCRIBE sent: %s (QoS %d)", topic, qos);

    /* 等待SUBACK */
    int timeout = 5000;
    while (timeout > 0) {
        len = transport_getdata(g_mqtt_recv_buf, MQTT_BUF_SIZE);
        if (len > 0) {
            break;
        }
        HAL_Delay(10);
        timeout -= 10;
    }

    if (len <= 0) {
        LOGE("MQTT: No suback received!");
        return -4;
    }

    ret = MQTTDeserialize_suback(&packet_id, count, &count, &grantedQoS, g_mqtt_recv_buf, len);
    if (ret != 1) {
        LOGE("MQTT: Deserialize suback failed!");
        return -5;
    }

    LOGI("MQTT: SUBSCRIBE acked, granted QoS %d", grantedQoS);
    g_mqtt_state = MQTT_STATE_SUBSCRIBED;
    return 0;
}

/**
 * @brief 发布消息
 */
int mqtt_client_publish(const char* topic, uint8_t* payload, uint16_t len, uint8_t qos, uint8_t retained)
{
    int ret;
    MQTTString topicName;
    unsigned short packet_id = 0;

    if (g_mqtt_state != MQTT_STATE_CONNECTED && g_mqtt_state != MQTT_STATE_SUBSCRIBED) {
        LOGE("MQTT: Not connected, cannot publish");
        return -1;
    }

    topicName.cstring = (char*)topic;

    if (qos > 0) {
        packet_id = mqtt_get_packet_id();
    }

    int send_len = MQTTSerialize_publish(g_mqtt_send_buf, MQTT_BUF_SIZE, 0, qos, retained,
                                          packet_id, topicName, payload, len);
    if (send_len <= 0) {
        LOGE("MQTT: Serialize publish failed!");
        return -2;
    }

    ret = transport_sendPacketBuffer(0, g_mqtt_send_buf, send_len);
    if (ret != send_len) {
        LOGE("MQTT: Send publish failed!");
        return -3;
    }

    LOGI("MQTT: PUBLISH sent: topic=%s, len=%d", topic, len);
    return 0;
}

/**
 * @brief 设置消息回调
 */
void mqtt_client_set_callback(mqtt_msg_callback_t callback)
{
    g_msg_callback = callback;
}

/**
 * @brief 处理MQTT消息循环
 */
int mqtt_client_loop(void)
{
    int len;
    unsigned char dup;
    int qos;
    unsigned char retained;
    unsigned short packet_id;
    MQTTString topicName;
    unsigned char* payload = NULL;
    int payloadlen = 0;

    if (g_mqtt_state == MQTT_STATE_DISCONNECTED || g_mqtt_state == MQTT_STATE_ERROR) {
        return -1;
    }

    /* 非阻塞读取数据 */
    len = transport_getdatanb(NULL, g_mqtt_recv_buf, MQTT_BUF_SIZE);
    if (len <= 0) {
        return 0;  /* 无数据 */
    }

    /* 解析PUBLISH报文 */
    if (MQTTDeserialize_publish(&dup, &qos, &retained, &packet_id, &topicName,
                                 &payload, &payloadlen, g_mqtt_recv_buf, len) != 1) {
        LOGW("MQTT: Deserialize publish failed!");
        return 0;
    }

    /* 调用消息回调 */
    if (g_msg_callback != NULL && payload != NULL && payloadlen > 0) {
        char* topic_str = topicName.cstring ? topicName.cstring : "";
        g_msg_callback(topic_str, payload, (uint16_t)payloadlen);
    }

    /* QoS 1 需要回复PUBACK */
    if (qos == 1) {
        int ack_len = MQTTSerialize_puback(g_mqtt_send_buf, MQTT_BUF_SIZE, packet_id);
        if (ack_len > 0) {
            transport_sendPacketBuffer(0, g_mqtt_send_buf, ack_len);
        }
    }

    return 0;
}

/**
 * @brief 断开MQTT连接
 */
void mqtt_client_disconnect(void)
{
    if (g_mqtt_state == MQTT_STATE_DISCONNECTED) {
        return;
    }

    int len = MQTTSerialize_disconnect(g_mqtt_send_buf, MQTT_BUF_SIZE);
    if (len > 0) {
        transport_sendPacketBuffer(0, g_mqtt_send_buf, len);
    }

    transport_close(0);
    g_mqtt_state = MQTT_STATE_DISCONNECTED;
    LOGI("MQTT: Disconnected");
}

/**
 * @brief 获取MQTT状态
 */
mqtt_state_t mqtt_client_get_state(void)
{
    return g_mqtt_state;
}
