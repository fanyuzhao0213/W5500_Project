/**
 * @file tcp_client.h
 * @brief TCP Client 头文件
 */

#ifndef _TCP_CLIENT_H_
#define _TCP_CLIENT_H_

#include <stdint.h>

/* 选择连接模式: 1 = 使用域名, 0 = 使用IP */
#define TCP_USE_DOMAIN     1

/* MQTT Broker 配置 - 域名模式 */
#define MQTT_BROKER_DOMAIN "broker.emqx.io"
#define MQTT_BROKER_PORT   1883

/* MQTT Broker 配置 - IP模式 (备用) */
#define MQTT_BROKER_IP     "47.74.187.120"

/* DNS 服务器 */
#define DNS_SERVER_IP      {8, 8, 8, 8}

/* Socket 缓冲区大小 */
#define TCP_BUFFER_SIZE    1024

/**
 * @brief TCP Client 初始化并连接服务器
 * @return 0 成功, -1 失败
 */
int tcp_client_connect(void);

/**
 * @brief TCP Client 发送数据
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return 发送的字节数, -1 失败
 */
int tcp_client_send(uint8_t* buf, uint16_t len);

/**
 * @brief TCP Client 接收数据
 * @param buf 数据缓冲区
 * @param len 最大接收长度
 * @return 接收的字节数, -1 失败
 */
int tcp_client_recv(uint8_t* buf, uint16_t len);

/**
 * @brief TCP Client 断开连接
 */
void tcp_client_disconnect(void);

/**
 * @brief TCP Client 是否已连接
 */
int tcp_client_is_connected(void);

#endif /* _TCP_CLIENT_H_ */

