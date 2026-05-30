/**
 * @file mqtt_task.c
 * @brief MQTT 任务实现 - 包含状态机和三个任务
 */

#include "mqtt_task.h"
#include "mqtt_client.h"
#include "mqtt_queue.h"
#include "mqtt_config.h"
#include "wizchip_conf.h"
#include "netconf.h"
#include "LOG.h"
#include "main.h"
#include "w5500_conf.h"


/* 外部变量声明 */
extern IWDG_HandleTypeDef hiwdg;
extern SPI_HandleTypeDef hspi1;

/* 任务句柄 */
osThreadId g_network_task_handle;
osThreadId g_mqtt_tx_task_handle;
osThreadId g_mqtt_rx_task_handle;

/* 当前网络状态 */
static net_state_t g_net_state = NET_STATE_INIT;

/* MQTT 运行标志 - MQTT连接成功后置1 */
static volatile uint8_t g_mqtt_running = 0;

/**
 * @brief 获取当前网络状态
 */
net_state_t mqtt_task_get_state(void)
{
    return g_net_state;
}

/**
 * @brief MQTT是否已连接并运行
 */
int mqtt_is_running(void)
{
    return g_mqtt_running;
}

/**
 * @brief 创建所有 MQTT 相关任务
 */
void mqtt_task_create(void)
{
    LOGI("mqtt_task_create: creating NetworkTask...");
    osThreadDef(networkTask, StartNetworkTask, NETWORK_TASK_PRIORITY, 0, NETWORK_TASK_STACK);
    g_network_task_handle = osThreadCreate(osThread(networkTask), NULL);
    if (g_network_task_handle == NULL) {
        LOGE("NetworkTask create failed");
    }

    LOGI("mqtt_task_create: creating MqttTxTask...");
    osThreadDef(mqttTxTask, StartMQTTTxTask, MQTT_TX_TASK_PRIORITY, 0, MQTT_TX_TASK_STACK);
    g_mqtt_tx_task_handle = osThreadCreate(osThread(mqttTxTask), NULL);
    if (g_mqtt_tx_task_handle == NULL) {
        LOGE("MqttTxTask create failed");
    }

    LOGI("mqtt_task_create: creating MqttRxTask...");
    osThreadDef(mqttRxTask, StartMQTTRxTask, MQTT_RX_TASK_PRIORITY, 0, MQTT_RX_TASK_STACK);
    g_mqtt_rx_task_handle = osThreadCreate(osThread(mqttRxTask), NULL);
    if (g_mqtt_rx_task_handle == NULL) {
        LOGE("MqttRxTask create failed");
    }
}

/**
 * @brief MQTT 消息接收回调
 * @note 将消息放入接收队列, 由 StartMQTTRxTask 处理
 */
void mqtt_message_callback(MessageData *md)
{
    mqtt_msg_t msg;
    int topic_len, payload_len;

    if (!g_mqtt_running) {
        return;
    }

    /* 获取主题长度 */
    topic_len = md->topicName->lenstring.len;
    if (topic_len >= 64) {
        topic_len = 63;
    }

    /* 获取载荷长度 */
    payload_len = (int)md->message->payloadlen;
    if (payload_len >= 256) {
        payload_len = 255;
    }

    /* 复制主题 */
    memcpy(msg.topic, md->topicName->lenstring.data, topic_len);
    msg.topic[topic_len] = '\0';

    /* 复制载荷 */
    memcpy(msg.payload, md->message->payload, payload_len);
    msg.payload[payload_len] = '\0';
    msg.len = payload_len;

    /* 获取 QoS */
    msg.qos = md->message->qos;

    /* 放入接收队列 (非阻塞) */
    if (mqtt_rx_queue_put(&msg, 0) != 0) {
        LOGW("MQTT RX: queue full, message dropped");
    }
}

/**
 * @brief 网络任务 - W5500初始化和MQTT协议循环
 * @note 使用状态机, 每个状态执行一次后立即 break, 由 FreeRTOS 调度
 */
void StartNetworkTask(void const * argument)
{
    uint8_t version;
    uint8_t snum[8];

    (void)argument;

    /* 状态机主循环 */
    for (;;) {
        switch (g_net_state) {
            case NET_STATE_INIT: {
                /* 注册 W5500 SPI 回调函数 */
                wizchip_spi_cbfunc();

                /* 初始化 W5500 */
                memset(snum, 16, sizeof(snum));
                wizchip_init(snum, snum);

                HAL_Delay(100);

                g_net_state = NET_STATE_W5500_CHECK;
                break;
            }

            case NET_STATE_W5500_CHECK: {
                /* 读取版本寄存器验证 W5500 */
                version = getVERSIONR();

                if (version == 0x04) {
                    g_net_state = NET_STATE_NET_CONFIG;
                } else {
                    LOGE("NetworkTask: W5500 ERROR! VERSIONR=0x%02X", version);
                    g_net_state = NET_STATE_ERROR;
                }
                break;
            }

            case NET_STATE_NET_CONFIG: {
                /* 配置网络参数 (静态IP) */
                NetConfig_Init();

                g_net_state = NET_STATE_MQTT_INIT;
                break;
            }

            case NET_STATE_MQTT_INIT: {
                /* 初始化 MQTT 客户端 */
                mqtt_client_init();

                g_net_state = NET_STATE_MQTT_CONNECT;
                break;
            }

            case NET_STATE_MQTT_CONNECT: {
                /* 连接 MQTT Broker */
                if (mqtt_client_connect() != MQTT_SUCCESS) {
                    g_net_state = NET_STATE_ERROR;
                } else {
                    g_net_state = NET_STATE_MQTT_SUBSCRIBE;
                }
                break;
            }

            case NET_STATE_MQTT_SUBSCRIBE: {
                /* 订阅主题 */
                if (mqtt_client_subscribe(MQTT_SUBSCRIBE_TOPIC, QOS1, mqtt_message_callback) != MQTT_SUCCESS) {
                    g_net_state = NET_STATE_ERROR;
                } else {
                    g_mqtt_running = 1;
                    g_net_state = NET_STATE_RUNNING;
                }
                break;
            }

            case NET_STATE_RUNNING: {
                /* 非阻塞 MQTT 循环处理 */
                mqtt_client_loop(0);

                /* 喂狗 */
                HAL_IWDG_Refresh(&hiwdg);

                /* 短延时让出 CPU */
                osDelay(10);
                break;
            }

            case NET_STATE_ERROR:
            default: {
                g_mqtt_running = 0;
                osDelay(1000);
                break;
            }
        }

        osDelay(1);
    }
}

/**
 * @brief MQTT 发送任务 - 从发送队列取数据并发布
 */
void StartMQTTTxTask(void const * argument)
{
    mqtt_msg_t msg;
    (void)argument;

    for (;;) {
        /* 等待 MQTT 连接成功 */
        while (!g_mqtt_running) {
            osDelay(100);
        }

        /* 从发送队列获取消息 (超时500ms) */
        if (mqtt_tx_queue_get(&msg, 500) == osOK) {
            if (mqtt_client_publish(msg.topic, msg.payload, msg.len, msg.qos) != MQTT_SUCCESS) {
                LOGE("MQTT TxTask: publish failed");
            }
        }
    }
}

/**
 * @brief MQTT 接收任务 - 从接收队列取数据并处理
 */
void StartMQTTRxTask(void const * argument)
{
    mqtt_msg_t msg;
    (void)argument;

    for (;;) {
        /* 等待 MQTT 连接成功 */
        while (!g_mqtt_running) {
            osDelay(100);
        }

        /* 从接收队列获取消息 (超时500ms) */
        if (mqtt_rx_queue_get(&msg, 500) == osOK) {
            LOGI("MQTT RxTask: topic=%s, payload=%.*s", msg.topic, msg.len, msg.payload);
        }
    }
}

