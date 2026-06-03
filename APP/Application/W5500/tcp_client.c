/**
 * @file tcp_client.c
 * @brief TCP Client 实现
 */

#include "tcp_client.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "netconf.h"
#include "dns.h"
#include "dhcp.h"
#include "cmsis_os.h"
#include "LOG.h"

/* TCP Client 使用的 Socket 号 */
#define TCP_CLIENT_SOCKET   0

/* DNS 使用的 Socket 号 (与 DHCP 共用 Socket 3) */
#define DNS_SOCKET          3

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
    uint8_t dns_ip[4];
    uint8_t buf[MAX_DNS_BUF_SIZE];
    int8_t ret;
    int retry;

    /* 优先使用 DHCP 获取的 DNS 服务器 */
    getDNSfromDHCP(dns_ip);

    /* 如果 DHCP 获取的 DNS 无效，使用备用 DNS */
    if (dns_ip[0] == 0 && dns_ip[1] == 0 && dns_ip[2] == 0 && dns_ip[3] == 0) {
        uint8_t backup_dns[4] = DNS_SERVER_IP;
        dns_ip[0] = backup_dns[0];
        dns_ip[1] = backup_dns[1];
        dns_ip[2] = backup_dns[2];
        dns_ip[3] = backup_dns[3];
        LOGW("DNS: DHCP DNS not available, using backup: %d.%d.%d.%d",
              dns_ip[0], dns_ip[1], dns_ip[2], dns_ip[3]);
    } else {
        LOGI("DNS: Using DHCP DNS server: %d.%d.%d.%d",
              dns_ip[0], dns_ip[1], dns_ip[2], dns_ip[3]);
    }

    LOGI("DNS: Resolving domain: %s", domain);

    /* 初始化 DNS */
    DNS_init(DNS_SOCKET, buf);

    /* 执行 DNS 解析，最多重试 3 次 */
    for (retry = 0; retry < 3; retry++) {
        LOGI("DNS: Attempt %d/%d...", retry + 1, 3);

        ret = DNS_run(dns_ip, (uint8_t*)domain, ip);
        if (ret == 1) {
            LOGI("DNS: Resolved %s -> %d.%d.%d.%d",
                  domain, ip[0], ip[1], ip[2], ip[3]);
            return 0;
        }

        LOGW("DNS: Attempt %d failed, retrying...", retry + 1);
        osDelay(500);  // 等待 500ms 再重试
    }

    LOGE("DNS: Failed to resolve %s after 3 attempts", domain);
    return -1;
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

    LOGI("TCP: DNS resolution complete, IP=%d.%d.%d.%d",
          g_target_ip[0], g_target_ip[1], g_target_ip[2], g_target_ip[3]);

    LOGI("TCP: Creating socket...");
    ret = socket(TCP_CLIENT_SOCKET, Sn_MR_TCP, 0, 0);
    LOGI("TCP: socket() returned %d", ret);
    if (ret != TCP_CLIENT_SOCKET) {
        LOGE("TCP: socket() failed, ret=%d", ret);
        return -1;
    }
    LOGI("TCP: socket() created, sock=%d", TCP_CLIENT_SOCKET);

    LOGI("TCP: Connecting to server...");
    ret = connect(TCP_CLIENT_SOCKET, g_target_ip, MQTT_BROKER_PORT);
    LOGI("TCP: connect() returned %d", ret);

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
 * @brief TCP Client 发送数据 (非阻塞)
 * @note 先检查发送缓冲区是否有足够空间，避免阻塞等待
 */
int tcp_client_send(uint8_t* buf, uint16_t len)
{
    int ret;
    uint16_t free_size;

    if (!g_connected) {
        return -1;
    }

    free_size = getSn_TX_FSR(TCP_CLIENT_SOCKET);

    if (len > free_size) {
        return 0;
    }

    ret = send(TCP_CLIENT_SOCKET, buf, len);
    if (ret < 0) {
        g_connected = 0;
        return -1;
    }

    return ret;
}

/**
 * @brief TCP Client 接收数据 (非阻塞)
 * @note 先检查接收缓冲区是否有数据，避免阻塞等待
 */
int tcp_client_recv(uint8_t* buf, uint16_t len)
{
    int ret;
    uint16_t rx_len;

    if (!g_connected) {
        return -1;
    }

    /* 非阻塞模式：先检查接收缓冲区中有多少数据 */
    rx_len = getSn_RX_RSR(TCP_CLIENT_SOCKET);
    if (rx_len == 0) {
        /* 没有数据，立即返回，不阻塞 */
        return 0;
    }

    /* 只读取可用的数据量 */
    if (rx_len > len) {
        rx_len = len;
    }

    ret = recv(TCP_CLIENT_SOCKET, buf, rx_len);
    if (ret < 0) {
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

