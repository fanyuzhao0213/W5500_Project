/**
 * @file mqtt_manager.h
 * @brief MQTT管理器接口 - 负责MQTT状态机、自动重连、KeepAlive
 */

#ifndef MQTT_MANAGER_H_
#define MQTT_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "network_types.h"
#include <stdint.h>

/*============================================================================
 * 函数声明
 *============================================================================*/
/**
 * @brief 初始化MQTT管理器
 * @note 必须在其他网络任务之后调用
 */
void mqtt_manager_init(void);

/**
 * @brief MQTT管理器任务主函数
 * @param argument 任务参数
 *
 * @note 实现以下状态机:
 * - MQTT_OFFLINE -> TCP_CONNECTING -> MQTT_CONNECTING
 * - -> MQTT_SUBSCRIBING -> MQTT_ONLINE
 * - 断开后进入自动重连流程
 */
void mqtt_manager_task(void const * argument);

/**
 * @brief 获取当前MQTT状态
 * @return MQTT状态
 */
mqtt_state_t mqtt_manager_get_state(void);

/**
 * @brief 获取当前MQTT状态字符串
 * @return 状态字符串
 */
const char* mqtt_manager_get_state_str(void);

/**
 * @brief 检查MQTT是否在线
 * @return 1在线, 0离线
 */
int mqtt_manager_is_online(void);

/**
 * @brief 发送MQTT消息 (非阻塞)
 * @param msg 消息结构体
 * @return 0成功, -1队列满, -2离线
 *
 * @note 消息会放入发送队列,由MQTT任务执行发送
 */
int mqtt_manager_send(mqtt_msg_t* msg);

/**
 * @brief 发布消息简化接口
 * @param topic 主题
 * @param payload 数据
 * @param len 数据长度
 * @param qos QoS级别
 * @return 0成功, -1失败
 */
int mqtt_manager_publish(const char* topic, uint8_t* payload, uint16_t len, uint8_t qos);

/**
 * @brief 获取自连接建立以来的运行时间
 * @return 毫秒数
 */
uint32_t mqtt_manager_get_uptime_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MANAGER_H_ */

