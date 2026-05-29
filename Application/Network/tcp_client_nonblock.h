/**
 * @file tcp_client_nonblock.h
 * @brief TCP非阻塞客户端接口
 */

#ifndef TCP_CLIENT_NONBLOCK_H_
#define TCP_CLIENT_NONBLOCK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================================================================
 * 常量定义
 *============================================================================*/
/** TCP客户端使用的Socket号 */
#define TCP_NB_SOCKET         0

/** 连接超时时间 (毫秒) */
#define TCP_CONNECT_TIMEOUT_MS  10000

/** DNS缓冲区大小 */
#define DNS_BUF_SIZE          512

/** 连接结果 */
typedef enum {
    TCP_NB_OK = 0,           /**< 连接成功 */
    TCP_NB_CONNECTING,       /**< 连接中 */
    TCP_NB_TIMEOUT,          /**< 连接超时 */
    TCP_NB_DNS_FAILED,       /**< DNS解析失败 */
    TCP_NB_SOCKET_FAILED,    /**< Socket创建失败 */
    TCP_NB_CONNECT_FAILED,   /**< 连接失败 */
    TCP_NB_DISCONNECTED      /**< 已断开 */
} tcp_nb_status_t;

/*============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 初始化TCP非阻塞客户端
 */
void tcp_nb_init(void);

/**
 * @brief 开始TCP连接 (非阻塞)
 * @return 连接状态
 * @note 首次调用返回CONNECTING,后续调用继续检查连接状态
 */
tcp_nb_status_t tcp_nb_connect_start(void);

/**
 * @brief 检查TCP连接是否已建立
 * @return 1已连接, 0未连接
 */
int tcp_nb_is_connected(void);

/**
 * @brief 断开TCP连接
 */
void tcp_nb_disconnect(void);

/**
 * @brief 获取Socket状态
 * @return Socket状态寄存器值
 */
uint8_t tcp_nb_get_socket_status(void);

/**
 * @brief 获取连接过程的状态描述
 * @param status 连接状态码
 * @return 状态描述字符串
 */
const char* tcp_nb_status_str(tcp_nb_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* TCP_CLIENT_NONBLOCK_H_ */

