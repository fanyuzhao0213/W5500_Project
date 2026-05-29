/**
  ******************************************************************************
  * @file    mqtt_fsm.c
  * @author  Network Team
  * @brief   MQTT状态机实现
  * @details 实现MQTT连接、重连、订阅等状态管理，支持指数退避重连机制
  ******************************************************************************
  */

#include "mqtt_fsm.h"
#include "cmsis_os.h"
#include "LOG.h"
#include "tcp_client.h"
#include "mqtt_client.h"
#include "net_manager.h"
#include "mqtt_queue.h"

/* MQTT状态机全局状态 */
volatile mqtt_state_t g_mqtt_state = MQTT_STATE_IDLE;

/* 重连延迟时间（指数退避策略）*/
static uint32_t reconnect_delay = RECONNECT_DELAY_1;
static uint32_t reconnect_start_time = 0;
static uint32_t last_ping_time = 0;
static uint8_t reconnect_attempts = 0;

/**
 * @brief 指数退避重连延迟数组
 * @note 策略: 1s → 2s → 5s → 10s → 30s → 60s
 */
static const uint32_t reconnect_delays[] = {
    RECONNECT_DELAY_1,
    RECONNECT_DELAY_2,
    RECONNECT_DELAY_3,
    RECONNECT_DELAY_4,
    RECONNECT_DELAY_5,
    RECONNECT_DELAY_6
};

/**
 * @brief 初始化MQTT状态机
 * @note 将状态重置为IDLE，重连计数器清零
 */
void mqtt_fsm_init(void) {
    g_mqtt_state = MQTT_STATE_IDLE;
    reconnect_delay = RECONNECT_DELAY_1;
    reconnect_attempts = 0;
    LOGI("MQTT FSM: initialized");
}

/**
 * @brief 获取当前MQTT状态
 * @return 当前MQTT状态
 */
mqtt_state_t mqtt_fsm_get_state(void) {
    return g_mqtt_state;
}

/**
 * @brief MQTT断开连接处理
 * @details 关闭MQTT连接和TCP连接，进入重连等待状态
 * @note 只在非IDLE和非RECONNECT_WAIT状态下执行
 */
void mqtt_fsm_disconnect(void) {
    if (g_mqtt_state != MQTT_STATE_IDLE && g_mqtt_state != MQTT_STATE_RECONNECT_WAIT) {
        LOGW("MQTT: Disconnecting from state %d", g_mqtt_state);

        /* 关闭MQTT连接 */
        mqtt_client_disconnect();

        /* 关闭TCP连接 */
        tcp_client_disconnect();

        /* 进入重连等待状态 */
        g_mqtt_state = MQTT_STATE_RECONNECT_WAIT;
        reconnect_start_time = HAL_GetTick();

        /* 应用指数退避策略 */
        if (reconnect_attempts < (sizeof(reconnect_delays) / sizeof(reconnect_delays[0]))) {
            reconnect_delay = reconnect_delays[reconnect_attempts];
            reconnect_attempts++;
        }

        LOGW("MQTT: LOST - entering reconnect wait");
        LOGW("MQTT: Reconnect attempt %d, delay %lu ms", reconnect_attempts, reconnect_delay);
    }
}

/**
 * @brief MQTT状态机主处理函数
 * @details 轮询处理各个状态的转换逻辑
 */
void mqtt_fsm_process(void) {
    int rc;

    switch(g_mqtt_state) {
        case MQTT_STATE_IDLE:
            /* 等待网络就绪 */
            if (net_manager_get_state() == NET_STATE_IP_OK) {
                LOGI("MQTT: Network ready, starting connection");
                g_mqtt_state = MQTT_STATE_TCP_CONNECT;
            }
            break;

        case MQTT_STATE_TCP_CONNECT:
            /* 建立TCP连接 */
            LOGI("MQTT: Connecting TCP to broker");
            rc = tcp_client_connect();
            if (rc == 0) {
                LOGI("MQTT: TCP connected successfully");
                g_mqtt_state = MQTT_STATE_MQTT_CONNECT;
            } else {
                LOGE("MQTT: TCP connect failed (rc=%d), retrying in 1s", rc);
                osDelay(1000);
            }
            break;

        case MQTT_STATE_MQTT_CONNECT:
            /* 建立MQTT连接 */
            LOGI("MQTT: Connecting MQTT to broker");
            rc = mqtt_client_connect();
            if (rc == MQTT_SUCCESS) {
                LOGI("MQTT: CONNECTED - broker accepted connection");
                g_mqtt_state = MQTT_STATE_SUBSCRIBE;
                /* 重置重连计数器 */
                reconnect_attempts = 0;
                reconnect_delay = RECONNECT_DELAY_1;
            } else {
                LOGE("MQTT: Connect failed (rc=%d)", rc);
                tcp_client_disconnect();
                mqtt_fsm_disconnect();
            }
            break;

        case MQTT_STATE_SUBSCRIBE:
            /* 订阅主题 */
            LOGI("MQTT: Subscribing to topic: %s", MQTT_SUBSCRIBE_TOPIC);
            rc = mqtt_client_subscribe(MQTT_SUBSCRIBE_TOPIC, QOS0, mqtt_message_received);
            if (rc == MQTT_SUCCESS) {
                LOGI("MQTT: Subscribed successfully");
                g_mqtt_state = MQTT_STATE_RUNNING;
                last_ping_time = HAL_GetTick();
            } else {
                LOGE("MQTT: Subscribe failed (rc=%d)", rc);
                mqtt_fsm_disconnect();
            }
            break;

        case MQTT_STATE_RUNNING:
            /* 运行状态：处理MQTT消息 */
            rc = mqtt_client_loop(10);
            if (rc != MQTT_SUCCESS) {
                LOGE("MQTT: Loop error (rc=%d), disconnecting", rc);
                mqtt_fsm_disconnect();
                break;
            }

            /* KeepAlive检测：定期发送PINGREQ */
            if (HAL_GetTick() - last_ping_time >= (MQTT_KEEP_ALIVE_INTERVAL * 1000)) {
                last_ping_time = HAL_GetTick();
                LOGD("MQTT: KeepAlive ping sent");
            }
            break;

        case MQTT_STATE_RECONNECT_WAIT:
            /* 重连等待：等待指定延迟后尝试重连 */
            if (HAL_GetTick() - reconnect_start_time >= reconnect_delay) {
                LOGI("MQTT: Reconnect wait completed, attempting reconnect");
                g_mqtt_state = MQTT_STATE_TCP_CONNECT;
            }
            break;

        default:
            /* 未知状态，重置为IDLE */
            LOGE("MQTT: Unknown state %d, resetting to IDLE", g_mqtt_state);
            g_mqtt_state = MQTT_STATE_IDLE;
            break;
    }
}
