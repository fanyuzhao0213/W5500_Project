/**
 * @file tcp_client_nonblock.c
 * @brief TCP非阻塞客户端实现
 *
 * 功能:
 * - 非阻塞TCP连接
 * - 支持连接超时
 * - 状态机管理连接过程
 */

#include "tcp_client_nonblock.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "dns.h"
#include "LOG.h"
#include "network_types.h"

/*============================================================================
 * 常量定义
 *============================================================================*/
/** TCP连接超时时间 (毫秒) */
#define TCP_CONNECT_TIMEOUT_MS   10000

/** DNS缓冲区 */
static uint8_t g_dns_buf[DNS_BUF_SIZE];

/*============================================================================
 * 静态变量
 *============================================================================*/
/** 连接状态机 */
typedef enum {
    TCP_STATE_IDLE = 0,
    TCP_STATE_DNS_RESOLVING,
    TCP_STATE_DNS_DONE,
    TCP_STATE_SOCKET_CREATED,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_DISCONNECTED
} tcp_nb_state_t;

static volatile tcp_nb_state_t g_tcp_state = TCP_STATE_IDLE;

/** 目标服务器IP */
static uint8_t g_target_ip[4] = {0};

/** 连接开始时间戳 */
static uint32_t g_connect_start_tick = 0;

/** DNS解析结果标志 */
static int8_t g_dns_result = -1;

/*============================================================================
 * 静态函数声明
 *============================================================================*/
static int tcp_nb_resolve_domain(const uint8_t* domain);
static int tcp_nb_create_socket(void);
static int tcp_nb_start_connect(void);

/*============================================================================
 * DNS解析 (阻塞式,但有超时保护)
 *============================================================================*/
/**
 * @brief 解析域名获取IP
 * @param domain 域名
 * @return 0成功, -1失败
 */
static int tcp_nb_resolve_domain(const uint8_t* domain)
{
    uint8_t dns_server_ip[4] = DNS_SERVER_IP;
    uint8_t resolved_ip[4] = {0};
    uint32_t start_tick;
    int8_t dns_ret;

    LOGI("TCP_NB: Resolving domain: %s", domain);

    /* 初始化DNS */
    DNS_init(TCP_NB_SOCKET, g_dns_buf);

    start_tick = HAL_GetTick();

    /* DNS解析 - 这里会阻塞,实际应用中可用异步DNS */
    dns_ret = DNS_run(dns_server_ip, (uint8_t*)domain, resolved_ip);

    if (dns_ret == 1) {
        g_target_ip[0] = resolved_ip[0];
        g_target_ip[1] = resolved_ip[1];
        g_target_ip[2] = resolved_ip[2];
        g_target_ip[3] = resolved_ip[3];
        LOGI("TCP_NB: DNS resolved -> %d.%d.%d.%d",
             g_target_ip[0], g_target_ip[1], g_target_ip[2], g_target_ip[3]);
        return 0;
    }

    LOGE("TCP_NB: DNS resolution failed");
    return -1;
}

/*============================================================================
 * Socket创建
 *============================================================================*/
/**
 * @brief 创建TCP Socket
 * @return 0成功, -1失败
 */
static int tcp_nb_create_socket(void)
{
    int ret;

    /* 关闭旧socket */
    close(TCP_NB_SOCKET);

    /* 创建TCP Socket */
    ret = socket(TCP_NB_SOCKET, Sn_MR_TCP, 0, 0);
    if (ret != TCP_NB_SOCKET) {
        LOGE("TCP_NB: socket() failed, ret=%d", ret);
        return -1;
    }

    LOGI("TCP_NB: socket() created, sock=%d", TCP_NB_SOCKET);
    return 0;
}

/*============================================================================
 * 开始连接
 *============================================================================*/
/**
 * @brief 开始TCP连接
 * @return 0成功, -1失败
 */
static int tcp_nb_start_connect(void)
{
    int ret;

    ret = connect(TCP_NB_SOCKET, g_target_ip, MQTT_BROKER_PORT);
    if (ret == SOCK_OK) {
        LOGI("TCP_NB: connect() immediate success");
        return 0;
    } else if (ret == SOCK_BUSY) {
        /* 连接中,等待Sn_SR变化 */
        LOGI("TCP_NB: connect() in progress...");
        return 0;
    } else {
        LOGE("TCP_NB: connect() failed, ret=%d", ret);
        return -1;
    }
}

/*============================================================================
 * 公共函数
 *============================================================================*/
/**
 * @brief 初始化TCP非阻塞客户端
 */
void tcp_nb_init(void)
{
    g_tcp_state = TCP_STATE_IDLE;
    g_dns_result = -1;
    g_target_ip[0] = 0;
    g_target_ip[1] = 0;
    g_target_ip[2] = 0;
    g_target_ip[3] = 0;
    LOGI("TCP_NB: initialized");
}

/**
 * @brief 开始TCP连接 (非阻塞状态机)
 * @return 连接状态
 */
tcp_nb_status_t tcp_nb_connect_start(void)
{
    uint8_t sock_sr;
    uint32_t elapsed;

    switch (g_tcp_state) {

    /* 空闲状态: 开始DNS解析 */
    case TCP_STATE_IDLE:
        LOGI("TCP_NB: State -> IDLE, starting DNS...");
        if (tcp_nb_resolve_domain((const uint8_t*)MQTT_BROKER_DOMAIN) != 0) {
            LOGE("TCP_NB: DNS failed");
            g_tcp_state = TCP_STATE_DISCONNECTED;
            return TCP_NB_DNS_FAILED;
        }
        g_tcp_state = TCP_STATE_DNS_DONE;
        /* fall through */

    /* DNS完成: 创建Socket */
    case TCP_STATE_DNS_DONE:
        LOGI("TCP_NB: State -> DNS_DONE, creating socket...");
        if (tcp_nb_create_socket() != 0) {
            g_tcp_state = TCP_STATE_DISCONNECTED;
            return TCP_NB_SOCKET_FAILED;
        }
        g_connect_start_tick = HAL_GetTick();
        g_tcp_state = TCP_STATE_CONNECTING;
        /* fall through */

    /* 连接中: 检查连接状态 */
    case TCP_STATE_CONNECTING:
        sock_sr = getSn_SR(TCP_NB_SOCKET);

        /* 检查超时 */
        elapsed = HAL_GetTick() - g_connect_start_tick;
        if (elapsed >= TCP_CONNECT_TIMEOUT_MS) {
            LOGE("TCP_NB: connect timeout (%ums)", elapsed);
            close(TCP_NB_SOCKET);
            g_tcp_state = TCP_STATE_DISCONNECTED;
            return TCP_NB_TIMEOUT;
        }

        /* 检查Socket状态 */
        if (sock_sr == SOCK_INIT) {
            /* Socket已初始化,调用connect开始连接 */
            if (tcp_nb_start_connect() != 0) {
                close(TCP_NB_SOCKET);
                g_tcp_state = TCP_STATE_DISCONNECTED;
                return TCP_NB_CONNECT_FAILED;
            }
            /* 等待下一状态 */
            return TCP_NB_CONNECTING;
        } else if (sock_sr == SOCK_SYNSENT) {
            /* 正在连接中 (SYN已发送,等待SYN/ACK) */
            return TCP_NB_CONNECTING;
        } else if (sock_sr == SOCK_ESTABLISHED) {
            /* 连接建立 */
            LOGI("TCP_NB: TCP connected! (elapsed=%ums)", elapsed);
            g_tcp_state = TCP_STATE_CONNECTED;
            return TCP_NB_OK;
        } else if (sock_sr == SOCK_CLOSE_WAIT) {
            /* 连接被关闭 */
            LOGW("TCP_NB: socket closed by peer");
            close(TCP_NB_SOCKET);
            g_tcp_state = TCP_STATE_DISCONNECTED;
            return TCP_NB_DISCONNECTED;
        } else if (sock_sr == SOCK_CLOSED) {
            /* Socket已关闭,重新开始 */
            LOGW("TCP_NB: socket closed, retrying...");
            close(TCP_NB_SOCKET);
            g_tcp_state = TCP_STATE_DNS_DONE;
            return TCP_NB_CONNECTING;
        } else {
            /* 其他状态,继续等待 */
            return TCP_NB_CONNECTING;
        }

    /* 已连接 */
    case TCP_STATE_CONNECTED:
        sock_sr = getSn_SR(TCP_NB_SOCKET);
        if (sock_sr != SOCK_ESTABLISHED) {
            LOGW("TCP_NB: connection lost, sr=0x%02X", sock_sr);
            close(TCP_NB_SOCKET);
            g_tcp_state = TCP_STATE_DISCONNECTED;
            return TCP_NB_DISCONNECTED;
        }
        return TCP_NB_OK;

    /* 已断开 */
    case TCP_STATE_DISCONNECTED:
        return TCP_NB_DISCONNECTED;

    default:
        g_tcp_state = TCP_STATE_IDLE;
        return TCP_NB_DISCONNECTED;
    }
}

/**
 * @brief 检查TCP连接是否已建立
 */
int tcp_nb_is_connected(void)
{
    if (g_tcp_state == TCP_STATE_CONNECTED) {
        uint8_t sock_sr = getSn_SR(TCP_NB_SOCKET);
        return (sock_sr == SOCK_ESTABLISHED) ? 1 : 0;
    }
    return 0;
}

/**
 * @brief 断开TCP连接
 */
void tcp_nb_disconnect(void)
{
    if (g_tcp_state != TCP_STATE_DISCONNECTED &&
        g_tcp_state != TCP_STATE_IDLE) {
        close(TCP_NB_SOCKET);
        LOGI("TCP_NB: disconnected");
    }
    g_tcp_state = TCP_STATE_IDLE;
}

/**
 * @brief 获取Socket状态寄存器
 */
uint8_t tcp_nb_get_socket_status(void)
{
    return getSn_SR(TCP_NB_SOCKET);
}

/**
 * @brief 获取状态描述字符串
 */
const char* tcp_nb_status_str(tcp_nb_status_t status)
{
    switch (status) {
        case TCP_NB_OK:            return "OK";
        case TCP_NB_CONNECTING:    return "CONNECTING";
        case TCP_NB_TIMEOUT:       return "TIMEOUT";
        case TCP_NB_DNS_FAILED:   return "DNS_FAILED";
        case TCP_NB_SOCKET_FAILED: return "SOCKET_FAILED";
        case TCP_NB_CONNECT_FAILED: return "CONNECT_FAILED";
        case TCP_NB_DISCONNECTED:  return "DISCONNECTED";
        default:                   return "UNKNOWN";
    }
}