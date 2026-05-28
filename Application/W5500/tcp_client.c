/**
 * @file tcp_client.c
 * @brief TCP Client 实现
 */

#include "tcp_client.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "netconf.h"
#include "dns.h"
#include "LOG.h"

/* TCP Client 使用的 Socket 号 */
#define TCP_CLIENT_SOCKET   0

/* 目标服务器信息 */
static uint8_t  g_target_ip[4] = {0};
static uint8_t   g_connected = 0;

/**
 * @brief 通过 DNS 解析域名获取 IP
 * @param domain 域名
 * @param ip 输出IP地址 (4字节)
 * @return 0 成功, -1 失败
 */
static int tcp_resolve_domain(const uint8_t* domain, uint8_t* ip)
{
    uint8_t dns_ip[4] = DNS_SERVER_IP;
    uint8_t buf[MAX_DNS_BUF_SIZE];

    LOGI("DNS: Resolving domain: %s", domain);

    /* 初始化 DNS */
    DNS_init(TCP_CLIENT_SOCKET, buf);

    /* 执行 DNS 解析 */
    int8_t ret = DNS_run(dns_ip, (uint8_t*)domain, ip);
    if (ret == 1) {
        LOGI("DNS: Resolved %s -> %d.%d.%d.%d",
              domain, ip[0], ip[1], ip[2], ip[3]);
        return 0;
    } else {
        LOGE("DNS: Failed to resolve %s", domain);
        return -1;
    }
}

/**
 * @brief TCP Client 初始化并连接服务器
 */
int tcp_client_connect(void)
{
    int ret;

#if TCP_USE_DOMAIN
    /* 使用域名模式，先解析 DNS */
    LOGI("TCP: Resolving domain: %s", MQTT_BROKER_DOMAIN);
    if (tcp_resolve_domain((const uint8_t*)MQTT_BROKER_DOMAIN, g_target_ip) != 0) {
        LOGE("TCP: DNS resolution failed!");
        return -1;
    }
#else
    /* 使用 IP 模式 */
    uint8_t ip_arr[4];
    sscanf(MQTT_BROKER_IP, "%d.%d.%d.%d",
           &ip_arr[0], &ip_arr[1], &ip_arr[2], &ip_arr[3]);
    g_target_ip[0] = ip_arr[0];
    g_target_ip[1] = ip_arr[1];
    g_target_ip[2] = ip_arr[2];
    g_target_ip[3] = ip_arr[3];
#endif

    LOGI("TCP: Connecting to %d.%d.%d.%d:%d...",
          g_target_ip[0], g_target_ip[1], g_target_ip[2], g_target_ip[3],
          MQTT_BROKER_PORT);

    /* 创建 TCP Socket */
    ret = socket(TCP_CLIENT_SOCKET, Sn_MR_TCP, 0, 0);
    if (ret != TCP_CLIENT_SOCKET) {
        LOGE("TCP: socket() failed, ret=%d", ret);
        return -1;
    }
    LOGI("TCP: socket() created, sock=%d", TCP_CLIENT_SOCKET);

    /* 连接服务器 */
    ret = connect(TCP_CLIENT_SOCKET, g_target_ip, MQTT_BROKER_PORT);
    if (ret != SOCK_OK) {
        LOGE("TCP: connect() failed, ret=%d", ret);
        close(TCP_CLIENT_SOCKET);
        return -1;
    }

    g_connected = 1;
    LOGI("TCP: Connected!");
    return 0;
}

/**
 * @brief TCP Client 发送数据
 */
int tcp_client_send(uint8_t* buf, uint16_t len)
{
    int ret;

    if (!g_connected) {
        return -1;
    }

    ret = send(TCP_CLIENT_SOCKET, buf, len);
    if (ret < 0) {
        LOGE("TCP: send() failed, ret=%d", ret);
        g_connected = 0;
        return -1;
    }

    return ret;
}

/**
 * @brief TCP Client 接收数据
 */
int tcp_client_recv(uint8_t* buf, uint16_t len)
{
    int ret;

    if (!g_connected) {
        return -1;
    }

    ret = recv(TCP_CLIENT_SOCKET, buf, len);
    if (ret < 0) {
        LOGE("TCP: recv() failed, ret=%d", ret);
        g_connected = 0;
        return -1;
    }

    return ret;
}

/**
 * @brief TCP Client 断开连接
 */
void tcp_client_disconnect(void)
{
    if (g_connected) {
        close(TCP_CLIENT_SOCKET);
        g_connected = 0;
        LOGI("TCP: Disconnected");
    }
}

/**
 * @brief TCP Client 是否已连接
 */
int tcp_client_is_connected(void)
{
    return g_connected;
}

