/**
 * @file transport.h
 * @brief MQTT传输层接口 - 适配W5500 Socket
 */

#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <stdint.h>

/**
 * @brief 通过Socket发送数据包
 * @param sock Socket号
 * @param buf 数据缓冲区
 * @param buflen 数据长度
 * @return 发送的字节数
 */
int transport_sendPacketBuffer(int sock, unsigned char* buf, int buflen);

/**
 * @brief 从Socket接收数据 (阻塞)
 * @param buf 数据缓冲区
 * @param count 最大接收长度
 * @return 实际接收的字节数
 */
int transport_getdata(unsigned char* buf, int count);

/**
 * @brief 从Socket接收数据 (非阻塞)
 * @param sck Socket指针
 * @param buf 数据缓冲区
 * @param count 最大接收长度
 * @return 实际接收的字节数
 */
int transport_getdatanb(void *sck, unsigned char* buf, int count);

/**
 * @brief 打开传输连接 (W5500已建立连接，此函数仅保存Socket号)
 * @param addr 目标地址 (不使用)
 * @param port 目标端口 (不使用)
 * @return 0成功, -1失败
 */
int transport_open(char* addr, int port);

/**
 * @brief 关闭传输连接 (W5500由tcp_client_disconnect管理)
 * @param sock Socket号
 * @return 0成功
 */
int transport_close(int sock);

#endif /* TRANSPORT_H_ */

