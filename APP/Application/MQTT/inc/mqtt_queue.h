/**
 * @file mqtt_queue.h
 * @brief MQTT 发送/接收队列定义
 */

#ifndef _MQTT_QUEUE_H_
#define _MQTT_QUEUE_H_

#include <stdint.h>
#include <stddef.h>
#include "cmsis_os.h"
#include "MQTTClient.h"

/* 队列消息数据结构
 * 注意：payload 大小需要支持 OTA 数据块传输
 * OTA_DEFAULT_CHUNK_SIZE = 4KB，但实际单包数据一般 <= 2KB
 */
typedef struct {
    char topic[64];         /* 主题 */
    char payload[2048];     /* 载荷 - 支持 2KB OTA 数据块 */
    int len;                /* 载荷长度 */
    enum QoS qos;           /* QoS级别 */
} mqtt_msg_t;

/* 队列容量 */
#define MQTT_TX_QUEUE_SIZE      10
#define MQTT_RX_QUEUE_SIZE      10
#define OTA_CMD_QUEUE_SIZE      10

/* 外部变量声明 */
extern osMessageQId g_mqtt_tx_queue;
extern osMessageQId g_mqtt_rx_queue;
extern osMessageQId g_ota_cmd_queue;

/**
 * @brief 初始化 MQTT 发送/接收队列
 * @retval 0 成功, -1 失败
 */
int mqtt_queue_init(void);

/**
 * @brief 向发送队列发送消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), 0表示不阻塞
 * @retval 0 成功, -1 失败
 */
int mqtt_tx_queue_put(mqtt_msg_t *msg, uint32_t timeout);

/**
 * @brief 从发送队列获取消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), osWaitForever表示永久等待
 * @retval osOK 成功, 其他失败
 */
osStatus mqtt_tx_queue_get(mqtt_msg_t *msg, uint32_t timeout);

/**
 * @brief 向接收队列发送消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), 0表示不阻塞
 * @retval 0 成功, -1 失败
 */
int mqtt_rx_queue_put(mqtt_msg_t *msg, uint32_t timeout);

/**
 * @brief 从接收队列获取消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), osWaitForever表示永久等待
 * @retval osOK 成功, 其他失败
 */
osStatus mqtt_rx_queue_get(mqtt_msg_t *msg, uint32_t timeout);

/**
 * @brief 检查发送队列是否有消息
 * @return 1 有消息, 0 无消息
 */
int mqtt_tx_queue_available(void);

/**
 * @brief 获取可用缓冲区数量
 */
int mqtt_msg_buf_available(void);

/**
 * @brief 向 OTA 命令队列发送消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), 0表示不阻塞
 * @retval 0 成功, -1 失败
 */
int ota_cmd_queue_put(mqtt_msg_t *msg, uint32_t timeout);

/**
 * @brief 从 OTA 命令队列获取消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), osWaitForever表示永久等待
 * @retval osOK 成功, 其他失败
 */
osStatus ota_cmd_queue_get(mqtt_msg_t *msg, uint32_t timeout);

#endif /* _MQTT_QUEUE_H_ */

