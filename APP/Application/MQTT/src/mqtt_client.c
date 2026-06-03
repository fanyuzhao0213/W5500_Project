#include "mqtt_client.h"
#include "LOG.h"
#include "tcp_client.h"

/* MQTT客户端实例 */
MQTTClient mqtt_client;
Network mqtt_network;
unsigned char mqtt_send_buf[MQTT_SEND_BUF_SIZE];
unsigned char mqtt_read_buf[MQTT_READ_BUF_SIZE];

void mqtt_client_init(void) {
    NetworkInit(&mqtt_network);
    MQTTClientInit(&mqtt_client, &mqtt_network, MQTT_COMMAND_TIMEOUT,
                   mqtt_send_buf, MQTT_SEND_BUF_SIZE,
                   mqtt_read_buf, MQTT_READ_BUF_SIZE);
}

int mqtt_client_connect(void) {
    int rc;

    LOGI("MQTT: Connecting to broker %s:%d", MQTT_BROKER_HOSTNAME, MQTT_BROKER_PORT);

    if (tcp_client_connect() != 0) {
        LOGE("MQTT: TCP connection failed");
        return MQTT_FAILURE;
    }

    MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
    options.clientID.cstring = (char*)MQTT_CLIENT_ID;
    options.keepAliveInterval = MQTT_KEEP_ALIVE;
    options.cleansession = MQTT_CLEAN_SESSION;

    if (strlen(MQTT_USERNAME) > 0) {
        options.username.cstring = (char*)MQTT_USERNAME;
    }
    if (strlen(MQTT_PASSWORD) > 0) {
        options.password.cstring = (char*)MQTT_PASSWORD;
    }

    rc = MQTTConnect(&mqtt_client, &options);

    if (rc == MQTT_CONNECTION_ACCEPTED) {
        LOGI("MQTT CONNECT OK");
        return MQTT_SUCCESS;
    } else {
        LOGE("MQTT: Connect failed, rc=%d", rc);
        tcp_client_disconnect();
        return MQTT_FAILURE;
    }
}

int mqtt_client_subscribe(const char* topicFilter, enum QoS qos, messageHandler handler) {
    int rc;

    if (!mqtt_client.isconnected) {
        LOGE("MQTT: Not connected");
        return MQTT_FAILURE;
    }

    rc = MQTTSubscribe(&mqtt_client, topicFilter, qos, handler);

    if (rc == MQTT_SUCCESS) {
        LOGI("MQTT: Subscribe OK");
        return MQTT_SUCCESS;
    } else {
        LOGE("MQTT: Subscribe failed, rc=%d", rc);
        return MQTT_FAILURE;
    }
}

int mqtt_client_publish(const char* topicName, const char* payload, size_t payloadlen, enum QoS qos) {
    int rc;
    MQTTMessage message;
    memset(&message, 0, sizeof(MQTTMessage));

    if (!mqtt_client.isconnected) {
        LOGE("MQTT: Not connected");
        return MQTT_FAILURE;
    }

    message.qos = qos;
    message.retained = 0;
    message.payload = (void*)payload;
    message.payloadlen = payloadlen;

    rc = MQTTPublish(&mqtt_client, topicName, &message);

    if (rc == MQTT_SUCCESS) {
        LOGI("MQTT Publish: topic : %s, payload : %s, qos : %d", topicName, payload, qos);
        return MQTT_SUCCESS;
    } else {
        LOGE("MQTT: Publish failed, rc=%d", rc);
        return MQTT_FAILURE;
    }
}

int mqtt_client_loop(int timeout_ms) {
    return MQTTYield(&mqtt_client, timeout_ms);
}

/**
 * @brief 非阻塞 MQTT 循环处理
 * @param timeout_ms 超时时间（毫秒）
 * @return MQTT 成功/失败码
 *
 * @note 此函数用于在 Flash 擦除等阻塞操作期间调用，保持 MQTT 连接
 */
int mqtt_client_loop_nonblocking(int timeout_ms) {
    int rc = MQTTYield(&mqtt_client, timeout_ms);

    // 如果检测到连接断开，立即返回错误
    if (rc < 0 || !mqtt_client.isconnected) {
        return MQTT_FAILURE;
    }

    return MQTT_SUCCESS;
}

int mqtt_client_disconnect(void) {
    int rc = MQTTDisconnect(&mqtt_client);
    tcp_client_disconnect();
    LOGI("MQTT: Disconnected");
    return rc;
}

int mqtt_client_unsubscribe(const char* topicFilter) {
    int rc;

    if (!mqtt_client.isconnected) {
        LOGW("MQTT: Not connected, skip unsubscribe");
        return MQTT_SUCCESS;
    }

    rc = MQTTUnsubscribe(&mqtt_client, topicFilter);

    if (rc == MQTT_SUCCESS) {
        LOGI("MQTT: Unubscribe OK: %s", topicFilter);
        return MQTT_SUCCESS;
    } else {
        LOGE("MQTT: Unsubscribe failed, rc=%d", rc);
        return MQTT_FAILURE;
    }
}


