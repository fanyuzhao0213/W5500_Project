/**
 * @file network_types.h
 * @brief 网络层统一类型定义
 */

#ifndef NETWORK_TYPES_H_
#define NETWORK_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================================================================
 * 网络状态定义
 *============================================================================*/
/**
 * @brief 网络状态枚举
 */
typedef enum {
    NET_DOWN = 0,       /**< 网络断开 */
    NET_LINK_UP,        /**< 网线Link恢复 */
    NET_IP_OK,          /**< IP获取成功 */
} net_state_t;

/**
 * @brief MQTT状态枚举
 */
typedef enum {
    MQTT_OFFLINE = 0,
    MQTT_TCP_CONNECTING,
    MQTT_CONNECTING,
    MQTT_SUBSCRIBING,
    MQTT_ONLINE,
} mqtt_state_t;

/*============================================================================
 * MQTT消息队列
 *============================================================================*/
/**
 * @brief MQTT消息结构体
 */
typedef struct {
    char     topic[64];      /**< 主题 */
    uint8_t  payload[256];   /**< 数据 */
    uint16_t len;            /**< 数据长度 */
    uint8_t  qos;            /**< QoS级别 */
} mqtt_msg_t;

/*============================================================================
 * 全局配置变量 (使用宏控制DHCP/静态IP)
 *============================================================================*/
/**
 * @brief 网络配置模式选择
 * @note 1 = DHCP模式, 0 = 静态IP模式
 */
#define NET_DHCP_MODE          1

/**
 * @brief MQTT Broker配置
 */
#define MQTT_BROKER_DOMAIN     "broker.emqx.io"
#define MQTT_BROKER_PORT       1883
#define MQTT_BROKER_IP         "47.74.187.120"
#define MQTT_CLIENT_ID         "STM32_W5500"
#define MQTT_KEEPALIVE         60
#define TCP_USE_DOMAIN         1

/**
 * @brief 订阅主题配置
 */
#define MQTT_SUB_TOPIC_1       "stm32/test"
#define MQTT_SUB_TOPIC_2       "stm32/cmd"
#define MQTT_SUB_QOS_1         0
#define MQTT_SUB_QOS_2         1

/**
 * @brief 默认静态IP配置
 */
#define DEFAULT_IP             {192, 168, 1, 88}
#define DEFAULT_SN             {255, 255, 255, 0}
#define DEFAULT_GW             {192, 168, 1, 1}
#define DEFAULT_DNS            {8, 8, 8, 8}
#define DEFAULT_MAC            {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56}

/**
 * @brief DNS服务器
 */
#define DNS_SERVER_IP          {8, 8, 8, 8}

/*============================================================================
 * 重连退避时间定义 (毫秒)
 *============================================================================*/
/**
 * @brief 重连间隔时间表
 */
#define RECONNECT_INTERVALS_NUM    6
static const uint32_t g_reconnect_intervals[RECONNECT_INTERVALS_NUM] = {
    1000,   /* 1s */
    2000,   /* 2s */
    5000,   /* 5s */
    10000,  /* 10s */
    30000,  /* 30s */
    60000   /* 60s */
};

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_TYPES_H_ */

