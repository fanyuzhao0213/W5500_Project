#ifndef _MQTT_CONFIG_H_
#define _MQTT_CONFIG_H_

/* MQTT Broker 配置 */
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

/* 平台头文件直接在MQTTClient.h中包含 */

#endif /* _MQTT_CONFIG_H_ */


