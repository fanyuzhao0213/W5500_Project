/**
 * @file transport.c
 * @brief MQTT传输层实现 - 适配W5500 Socket
 */

#include "transport.h"
#include "tcp_client.h"
#include "socket.h"
#include "LOG.h"

/* 当前连接的Socket号 */
static int g_mqtt_sock = -1;

int transport_sendPacketBuffer(int sock, unsigned char* buf, int buflen)
{
    int ret = tcp_client_send(buf, (uint16_t)buflen);
    if (ret < 0) {
        LOGE("MQTT transport: send failed, ret=%d", ret);
        return -1;
    }
    return ret;
}

int transport_getdata(unsigned char* buf, int count)
{
    int ret = tcp_client_recv(buf, (uint16_t)count);
    return ret;
}

int transport_getdatanb(void *sck, unsigned char* buf, int count)
{
    int sock = *((int *)sck);
    (void)sock;  /* 未使用，W5500同步读取 */

    int ret = tcp_client_recv(buf, (uint16_t)count);
    if (ret < 0) {
        return 0;  /* 非阻塞模式返回0表示无数据 */
    }
    return ret;
}

int transport_open(char* addr, int port)
{
    (void)addr;
    (void)port;

    /* W5500连接已在tcp_client_connect()中建立
     * 此函数仅保存Socket号供后续使用
     */
    g_mqtt_sock = 0;  /* TCP_CLIENT_SOCKET = 0 */
    LOGI("MQTT transport: opened (sock=%d)", g_mqtt_sock);
    return g_mqtt_sock;
}

int transport_close(int sock)
{
    (void)sock;
    /* W5500连接由tcp_client_disconnect()管理 */
    g_mqtt_sock = -1;
    LOGI("MQTT transport: closed");
    return 0;
}


