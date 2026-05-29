/**
 * @file mqtt_manager.c
 * @brief MQTT管理器实现 - MQTT状态机、自动重连、KeepAlive
 *
 * 功能:
 * - MQTT状态机 (OFFLINE/TCP_CONNECTING/CONNECTING/SUBSCRIBING/ONLINE)
 * - 自动重连 (指数退避)
 * - KeepAlive心跳
 * - 订阅恢复
 * - 消息发送队列
 *
 * @note 禁止在MQTT层失败时重置W5500
 */

#include "mqtt_manager.h"
#include "tcp_client_nonblock.h"
#include "net_manager.h"
#include "transport.h"
#include "MQTTPacket.h"
#include "MQTTConnect.h"
#include "MQTTSubscribe.h"
#include "MQTTPublish.h"
#include "network_types.h"
#include "LOG.h"
#include "cmsis_os.h"
#include "main.h"

/*============================================================================
 * 常量定义
 *============================================================================*/
/** MQTT缓冲区大小 */
#define MQTT_BUF_SIZE           512

/** 发送队列深度 */
#define MQTT_QUEUE_SIZE        16

/** KeepAlive间隔 (毫秒) - 建议小于Broker配置的1.5倍 */
#define MQTT_KEEPALIVE_INTERVAL_MS  (MQTT_KEEPALIVE * 1000 / 2)

/** PING超时时间 (毫秒) */
#define MQTT_PING_TIMEOUT_MS    5000

/** 订阅超时时间 (毫秒) */
#define MQTT_SUB_TIMEOUT_MS     5000

/** 重连最大尝试次数 (0=无限) */
#define RECONNECT_MAX_RETRIES   0

/*============================================================================
 * 静态变量
 *============================================================================*/
/** MQTT状态 */
static volatile mqtt_state_t g_mqtt_state = MQTT_OFFLINE;

/** 连接建立后的运行时间 */
static uint32_t g_mqtt_uptime_ms = 0;

/** 连接建立时间戳 */
static uint32_t g_mqtt_connect_tick = 0;

/** 上次KeepAlive发送时间 */
static uint32_t g_last_ping_tick = 0;

/** 重连间隔索引 */
static uint8_t g_reconnect_idx = 0;

/** 重连计数器 */
static uint32_t g_reconnect_count = 0;

/** 订阅状态 */
static uint8_t g_subscribed = 0;

/** Packet ID */
static uint16_t g_packet_id = 1;

/** MQTT发送队列 */
static mqtt_msg_t g_mqtt_queue[MQTT_QUEUE_SIZE];
static uint8_t g_queue_head = 0;
static uint8_t g_queue_tail = 0;

/** MQTT缓冲区 */
static uint8_t g_mqtt_send_buf[MQTT_BUF_SIZE];
static uint8_t g_mqtt_recv_buf[MQTT_BUF_SIZE];

/** 消息回调 */
static void (*g_msg_callback)(const char* topic, uint8_t* payload, uint16_t len) = NULL;

/*============================================================================
 * 静态函数声明
 *============================================================================*/
static void mqtt_reset(void);
static int  mqtt_connect(void);
static int  mqtt_subscribe_all(void);
static int  mqtt_send_pingreq(void);
static int  mqtt_process_recv(void);
static int  mqtt_process_queue(void);
static void mqtt_schedule_reconnect(void);
static uint16_t mqtt_get_packet_id(void);

/*============================================================================
 * 辅助函数
 *============================================================================*/
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
 * @brief 获取重连间隔
 */
static uint32_t mqtt_get_reconnect_interval(void)
{
    if (g_reconnect_idx >= RECONNECT_INTERVALS_NUM) {
        g_reconnect_idx = RECONNECT_INTERVALS_NUM - 1;
    }
    return g_reconnect_intervals[g_reconnect_idx];
}

/**
 * @brief 重置MQTT状态
 */
static void mqtt_reset(void)
{
    g_mqtt_state = MQTT_OFFLINE;
    g_subscribed = 0;
    g_mqtt_uptime_ms = 0;
    g_last_ping_tick = 0;
    g_reconnect_idx = 0;
    g_mqtt_connect_tick = 0;
    g_queue_head = 0;
    g_queue_tail = 0;

    tcp_nb_disconnect();
    transport_close(0);

    LOGI("MQTT: reset");
}

/**
 * @brief 调度重连 (指数退避)
 */
static void mqtt_schedule_reconnect(void)
{
    uint32_t interval;

    /* 增加退避索引 */
    if (g_reconnect_idx < RECONNECT_INTERVALS_NUM - 1) {
        g_reconnect_idx++;
    }

    interval = mqtt_get_reconnect_interval();
    g_reconnect_count++;

    LOGW("MQTT: reconnect scheduled in %ums (idx=%d, count=%lu)",
         interval, g_reconnect_idx, g_reconnect_count);

    /* 延迟执行 - 由调用者处理osDelay */
}

/*============================================================================
 * MQTT连接
 *============================================================================*/
/**
 * @brief 连接MQTT Broker
 * @return 0成功, 负值失败
 */
static int mqtt_connect(void)
{
    int ret;
    MQTTPacket_connectData options;
    unsigned char sessionPresent = 0;
    unsigned char connack_rc = 0;
    int len;

    LOGI("MQTT: connecting...");

    /* 构造CONNECT报文 */
    memset(&options, 0, sizeof(options));
    options.MQTTVersion = 4;  /* MQTT 3.1.1 */
    options.clientID.cstring = MQTT_CLIENT_ID;
    options.keepAliveInterval = MQTT_KEEPALIVE;
    options.cleansession = 1;

    len = MQTTSerialize_connect(g_mqtt_send_buf, MQTT_BUF_SIZE, &options);
    if (len <= 0) {
        LOGE("MQTT: serialize connect failed");
        return -1;
    }

    /* 发送CONNECT报文 */
    ret = transport_sendPacketBuffer(0, g_mqtt_send_buf, len);
    if (ret != len) {
        LOGE("MQTT: send connect failed");
        return -2;
    }
    LOGI("MQTT: CONNECT sent");

    /* 等待CONNACK响应 (带超时) */
    uint32_t start_tick = HAL_GetTick();
    while (HAL_GetTick() - start_tick < MQTT_SUB_TIMEOUT_MS) {
        ret = transport_getdata(g_mqtt_recv_buf, MQTT_BUF_SIZE);
        if (ret > 0) {
            break;
        }
        osDelay(10);
    }

    if (ret <= 0) {
        LOGE("MQTT: no connack received");
        return -3;
    }

    /* 解析CONNACK */
    ret = MQTTDeserialize_connack(&sessionPresent, &connack_rc, g_mqtt_recv_buf, ret);
    if (ret != 1) {
        LOGE("MQTT: deserialize connack failed");
        return -4;
    }

    if (connack_rc != MQTT_CONNECTION_ACCEPTED) {
        LOGE("MQTT: connection refused, rc=%d", connack_rc);
        return -5;
    }

    LOGI("MQTT: CONNECTED! (sessionPresent=%d)", sessionPresent);
    return 0;
}

/**
 * @brief 订阅所有预定义主题
 * @return 0成功, 负值失败
 */
static int mqtt_subscribe_all(void)
{
    int ret;
    int len;
    MQTTString topicFilters[2];
    int requestedQoSs[2];
    unsigned short packet_id;
    int grantedQoS[2];
    int count = 2;
    uint32_t start_tick;

    LOGI("MQTT: subscribing...");

    /* 构造SUBSCRIBE报文 */
    topicFilters[0].cstring = (char*)MQTT_SUB_TOPIC_1;
    topicFilters[1].cstring = (char*)MQTT_SUB_TOPIC_2;
    requestedQoSs[0] = MQTT_SUB_QOS_1;
    requestedQoSs[1] = MQTT_SUB_QOS_2;

    packet_id = mqtt_get_packet_id();
    len = MQTTSerialize_subscribe(g_mqtt_send_buf, MQTT_BUF_SIZE, 0, packet_id,
                                   count, topicFilters, requestedQoSs);
    if (len <= 0) {
        LOGE("MQTT: serialize subscribe failed");
        return -1;
    }

    /* 发送SUBSCRIBE报文 */
    ret = transport_sendPacketBuffer(0, g_mqtt_send_buf, len);
    if (ret != len) {
        LOGE("MQTT: send subscribe failed");
        return -2;
    }
    LOGI("MQTT: SUBSCRIBE sent");

    /* 等待SUBACK */
    start_tick = HAL_GetTick();
    while (HAL_GetTick() - start_tick < MQTT_SUB_TIMEOUT_MS) {
        ret = transport_getdata(g_mqtt_recv_buf, MQTT_BUF_SIZE);
        if (ret > 0) {
            /* 解析SUBACK */
            ret = MQTTDeserialize_suback(&packet_id, count, &count, grantedQoS, g_mqtt_recv_buf, ret);
            if (ret == 1) {
                LOGI("MQTT: SUBSCRIBED! (QoS: %d, %d)", grantedQoS[0], grantedQoS[1]);
                g_subscribed = 1;
                return 0;
            }
        }
        osDelay(10);
    }

    LOGE("MQTT: no suback received");
    return -3;
}

/*============================================================================
 * KeepAlive
 *============================================================================*/
/**
 * @brief 发送PINGREQ
 * @return 0成功, 负值失败
 */
static int mqtt_send_pingreq(void)
{
    int len;

    len = MQTTSerialize_pingreq(g_mqtt_send_buf, MQTT_BUF_SIZE);
    if (len <= 0) {
        LOGE("MQTT: serialize pingreq failed");
        return -1;
    }

    if (transport_sendPacketBuffer(0, g_mqtt_send_buf, len) != len) {
        LOGE("MQTT: send pingreq failed");
        return -2;
    }

    LOGI("MQTT: PINGREQ sent");
    return 0;
}

/*============================================================================
 * 数据处理
 *============================================================================*/
/**
 * @brief 处理接收到的MQTT消息
 */
static int mqtt_process_recv(void)
{
    int ret;
    unsigned char dup;
    int qos;
    unsigned char retained;
    unsigned short packet_id;
    MQTTString topicName;
    unsigned char* payload = NULL;
    int payloadlen = 0;

    /* 非阻塞读取 */
    ret = transport_getdatanb(NULL, g_mqtt_recv_buf, MQTT_BUF_SIZE);
    if (ret <= 0) {
        return 0;
    }

    /* 解析PUBLISH */
    if (MQTTDeserialize_publish(&dup, &qos, &retained, &packet_id,
                                 &topicName, &payload, &payloadlen,
                                 g_mqtt_recv_buf, ret) != 1) {
        /* 非PUBLISH报文,忽略 */
        return 0;
    }

    /* 调用消息回调 */
    if (g_msg_callback != NULL && payload != NULL && payloadlen > 0) {
        char* topic_str = topicName.cstring ? topicName.cstring : "";
        g_msg_callback(topic_str, payload, (uint16_t)payloadlen);
    }

    /* QoS 1 回复PUBACK */
    if (qos == 1) {
        int ack_len = MQTTSerialize_puback(g_mqtt_send_buf, MQTT_BUF_SIZE, packet_id);
        if (ack_len > 0) {
            transport_sendPacketBuffer(0, g_mqtt_send_buf, ack_len);
        }
    }

    return 0;
}

/**
 * @brief 处理发送队列
 */
static int mqtt_process_queue(void)
{
    int ret;
    mqtt_msg_t* msg;
    MQTTString topicName;
    unsigned short packet_id = 0;

    /* 队列为空 */
    if (g_queue_head == g_queue_tail) {
        return 0;
    }

    /* 获取消息 */
    msg = &g_mqtt_queue[g_queue_tail];
    g_queue_tail = (g_queue_tail + 1) % MQTT_QUEUE_SIZE;

    /* 构造PUBLISH报文 */
    topicName.cstring = msg->topic;

    if (msg->qos > 0) {
        packet_id = mqtt_get_packet_id();
    }

    ret = MQTTSerialize_publish(g_mqtt_send_buf, MQTT_BUF_SIZE, 0, msg->qos, 0,
                                  packet_id, topicName, msg->payload, msg->len);
    if (ret <= 0) {
        LOGE("MQTT: serialize publish failed");
        return -1;
    }

    ret = transport_sendPacketBuffer(0, g_mqtt_send_buf, ret);
    if (ret < 0) {
        LOGE("MQTT: send publish failed");
        return -2;
    }

    LOGI("MQTT: published to %s (len=%d)", msg->topic, msg->len);
    return 0;
}

/*============================================================================
 * 公共函数
 *============================================================================*/
/**
 * @brief 初始化MQTT管理器
 */
void mqtt_manager_init(void)
{
    mqtt_reset();
    g_msg_callback = NULL;
    g_reconnect_count = 0;

    LOGI("MQTT Manager: initialized");
}

/**
 * @brief 设置消息回调
 */
void mqtt_manager_set_callback(void (*callback)(const char* topic, uint8_t* payload, uint16_t len))
{
    g_msg_callback = callback;
}

/**
 * @brief 获取MQTT状态字符串
 */
const char* mqtt_manager_get_state_str(void)
{
    switch (g_mqtt_state) {
        case MQTT_OFFLINE:         return "MQTT_OFFLINE";
        case MQTT_TCP_CONNECTING:  return "TCP_CONNECTING";
        case MQTT_CONNECTING:      return "MQTT_CONNECTING";
        case MQTT_SUBSCRIBING:     return "MQTT_SUBSCRIBING";
        case MQTT_ONLINE:          return "MQTT_ONLINE";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief 获取当前MQTT状态
 */
mqtt_state_t mqtt_manager_get_state(void)
{
    return g_mqtt_state;
}

/**
 * @brief 检查MQTT是否在线
 */
int mqtt_manager_is_online(void)
{
    return (g_mqtt_state == MQTT_ONLINE) ? 1 : 0;
}

/**
 * @brief 获取运行时间
 */
uint32_t mqtt_manager_get_uptime_ms(void)
{
    if (g_mqtt_connect_tick == 0) {
        return 0;
    }
    return HAL_GetTick() - g_mqtt_connect_tick;
}

/**
 * @brief 发送MQTT消息 (放入队列)
 */
int mqtt_manager_send(mqtt_msg_t* msg)
{
    uint8_t next_head;

    if (g_mqtt_state != MQTT_ONLINE) {
        return -2;
    }

    next_head = (g_queue_head + 1) % MQTT_QUEUE_SIZE;
    if (next_head == g_queue_tail) {
        LOGW("MQTT: queue full, message dropped");
        return -1;
    }

    /* 复制消息到队列 */
    memcpy(&g_mqtt_queue[g_queue_head], msg, sizeof(mqtt_msg_t));
    g_queue_head = next_head;

    return 0;
}

/**
 * @brief 发布消息简化接口
 */
int mqtt_manager_publish(const char* topic, uint8_t* payload, uint16_t len, uint8_t qos)
{
    mqtt_msg_t msg;

    if (strlen(topic) >= sizeof(msg.topic)) {
        LOGE("MQTT: topic too long");
        return -1;
    }

    if (len >= sizeof(msg.payload)) {
        LOGE("MQTT: payload too large");
        return -1;
    }

    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    memcpy(msg.payload, payload, len);
    msg.len = len;
    msg.qos = qos;

    return mqtt_manager_send(&msg);
}

/*============================================================================
 * MQTT任务主函数
 *============================================================================*/
/**
 * @brief MQTT管理器任务主函数
 */
void mqtt_manager_task(void const * argument)
{
    (void)argument;
    tcp_nb_status_t tcp_ret;
    int mqtt_ret;
    uint32_t reconnect_delay;
    uint8_t reconnect_triggered = 0;

    LOGI("MQTT Manager: task started");

    /* 初始化 */
    mqtt_manager_init();
    tcp_nb_init();

    while (1) {
        switch (g_mqtt_state) {

        /*======================================================================
         * MQTT_OFFLINE: 离线状态,等待网络就绪
         *======================================================================*/
        case MQTT_OFFLINE:
            /* 检查网络是否就绪 (NetManager状态) */
            if (net_manager_get_state() != NET_IP_OK) {
                osDelay(100);
                continue;
            }

            /* 网络就绪,开始TCP连接 */
            LOGI("MQTT: network ready, starting TCP...");
            tcp_nb_init();
            g_mqtt_state = MQTT_TCP_CONNECTING;
            reconnect_triggered = 0;
            break;

        /*======================================================================
         * MQTT_TCP_CONNECTING: TCP连接中
         *======================================================================*/
        case MQTT_TCP_CONNECTING:
            tcp_ret = tcp_nb_connect_start();

            if (tcp_ret == TCP_NB_OK) {
                /* TCP连接成功,打开transport */
                transport_open(NULL, 0);
                g_mqtt_state = MQTT_CONNECTING;
                LOGI("MQTT: TCP connected, now MQTT...");
            } else if (tcp_ret == TCP_NB_CONNECTING) {
                /* 连接中,继续轮询 */
                osDelay(10);
            } else {
                /* 连接失败,调度重连 */
                LOGE("MQTT: TCP connect failed: %s", tcp_nb_status_str(tcp_ret));
                if (!reconnect_triggered) {
                    reconnect_triggered = 1;
                    mqtt_schedule_reconnect();
                }
                reconnect_delay = mqtt_get_reconnect_interval();
                LOGI("MQTT: reconnecting in %ums...", reconnect_delay);
                osDelay(reconnect_delay);

                /* 重置并重试 */
                tcp_nb_init();
                reconnect_triggered = 0;
            }
            break;

        /*======================================================================
         * MQTT_CONNECTING: MQTT连接中
         *======================================================================*/
        case MQTT_CONNECTING:
            mqtt_ret = mqtt_connect();
            if (mqtt_ret == 0) {
                g_mqtt_state = MQTT_SUBSCRIBING;
            } else {
                LOGE("MQTT: connect failed, ret=%d", mqtt_ret);
                mqtt_reset();
                g_mqtt_state = MQTT_OFFLINE;
            }
            break;

        /*======================================================================
         * MQTT_SUBSCRIBING: 订阅中
         *======================================================================*/
        case MQTT_SUBSCRIBING:
            mqtt_ret = mqtt_subscribe_all();
            if (mqtt_ret == 0) {
                g_mqtt_state = MQTT_ONLINE;
                g_mqtt_connect_tick = HAL_GetTick();
                g_last_ping_tick = HAL_GetTick();
                g_reconnect_idx = 0;  /* 重置退避索引 */
                LOGI("MQTT: ONLINE! All subscribed.");
            } else {
                LOGE("MQTT: subscribe failed, ret=%d", mqtt_ret);
                mqtt_reset();
                g_mqtt_state = MQTT_OFFLINE;
            }
            break;

        /*======================================================================
         * MQTT_ONLINE: 在线状态
         *======================================================================*/
        case MQTT_ONLINE:
            /* 处理接收 */
            mqtt_process_recv();

            /* 处理发送队列 */
            mqtt_process_queue();

            /* 检查TCP连接状态 */
            if (!tcp_nb_is_connected()) {
                LOGW("MQTT: TCP connection lost!");
                mqtt_reset();
                g_mqtt_state = MQTT_OFFLINE;
                break;
            }

            /* KeepAlive检查 */
            if (HAL_GetTick() - g_last_ping_tick >= MQTT_KEEPALIVE_INTERVAL_MS) {
                if (mqtt_send_pingreq() == 0) {
                    g_last_ping_tick = HAL_GetTick();
                } else {
                    LOGW("MQTT: ping failed, will retry...");
                }
            }

            /* 短暂的周期延迟 */
            osDelay(10);
            break;

        /*======================================================================
         * 默认: 未知状态,重置
         *======================================================================*/
        default:
            LOGW("MQTT: unknown state %d, resetting...", g_mqtt_state);
            mqtt_reset();
            g_mqtt_state = MQTT_OFFLINE;
            break;
        }
    }
}