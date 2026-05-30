#ifndef _MQTT_CONFIG_H_
#define _MQTT_CONFIG_H_

/* ============================================================
 * 网络配置 - IP 地址获取方式
 * ============================================================
 * 选择以下模式之一：
 *   - NET_CONFIG_DHCP     : 使用 DHCP 自动获取 IP
 *   - NET_CONFIG_STATIC   : 使用静态 IP 配置
 */
/* ============================================================
 * 网络配置模式枚举 (内部使用)
 * ============================================================ */
#define NET_CONFIG_DHCP        0
#define NET_CONFIG_STATIC      1

#define NET_CONFIG_MODE        NET_CONFIG_DHCP

/* 静态 IP 配置参数 (仅在 NET_CONFIG_STATIC 模式下使用) */
#define STATIC_IP_ADDR         "192.168.1.88"
#define STATIC_SUBNET_MASK     "255.255.255.0"
#define STATIC_GATEWAY         "192.168.1.1"
#define STATIC_DNS             "8.8.8.8"

/* MAC 地址 (固定) */
#define MAC_ADDR               "00:08:DC:12:34:56"

/* ============================================================
 * MQTT Broker 配置
 * ============================================================ */
#define MQTT_BROKER_IP         "47.74.187.120"
#define MQTT_BROKER_PORT       1883
#define MQTT_CLIENT_ID         "stm32_w5500_client"
#define MQTT_USERNAME          ""
#define MQTT_PASSWORD          ""

/* MQTT 配置参数 */
#define MQTT_KEEP_ALIVE        60
#define MQTT_CLEAN_SESSION     1
#define MQTT_COMMAND_TIMEOUT   5000

/* 订阅主题 */
#define MQTT_SUBSCRIBE_TOPIC   "stm32/test"

/* 发布主题 */
#define MQTT_PUBLISH_TOPIC     "stm32/uptime"
#define MQTT_PUBLISH_INTERVAL  5000

/* 缓冲区大小 */
#define MQTT_SEND_BUF_SIZE     256
#define MQTT_READ_BUF_SIZE     512

/* 最大消息处理器数量 */
#define MAX_MESSAGE_HANDLERS   5



#endif /* _MQTT_CONFIG_H_ */


