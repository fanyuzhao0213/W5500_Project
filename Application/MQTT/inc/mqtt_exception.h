/**
 * @file mqtt_exception.h
 * @brief 网络异常处理模块 - 独立线程
 */

#ifndef _MQTT_EXCEPTION_H_
#define _MQTT_EXCEPTION_H_

#include "cmsis_os.h"
#include <stdint.h>

/* ============================================================
 * 异常类型定义
 * ============================================================ */
typedef enum {
    EXCEPTION_NONE = 0,                  /* 无异常 */
    EXCEPTION_PHY_LINK_DOWN,             /* PHY 链路断开 */
    EXCEPTION_W5500_ERROR,               /* W5500 芯片异常 */
    EXCEPTION_DHCP_FAILED,               /* DHCP 获取失败 */
    EXCEPTION_DHCP_TIMEOUT,              /* DHCP 超时 */
    EXCEPTION_MQTT_DISCONNECTED,         /* MQTT 连接断开 */
    EXCEPTION_MQTT_PUBLISH_FAILED,       /* MQTT 发布失败 */
    EXCEPTION_MQTT_SUBSCRIBE_FAILED,     /* MQTT 订阅失败 */
    EXCEPTION_NETWORK_ERROR,             /* 网络通用错误 */
    EXCEPTION_MAX
} exception_type_t;

/* ============================================================
 * 异常状态结构体
 * ============================================================ */
typedef struct {
    exception_type_t type;               /* 异常类型 */
    uint32_t timestamp;                  /* 发生时间（tick）*/
    uint32_t count;                      /* 累计次数 */
    uint8_t in_recovery;                 /* 是否在恢复中 */
    uint8_t recovery_attempts;           /* 恢复尝试次数 */
} exception_status_t;

/* ============================================================
 * 恢复策略枚举
 * ============================================================ */
typedef enum {
    RECOVERY_NONE = 0,
    RECOVERY_RETRY_MQTT,                 /* 重新连接 MQTT */
    RECOVERY_RESTART_DHCP,               /* 重启 DHCP */
    RECOVERY_RESET_W5500,                /* 复位 W5500 */
    RECOVERY_FULL_RESET,                 /* 完全复位（回到初始状态）*/
    RECOVERY_WATCHDOG_RESET              /* 看门狗复位（硬件复位）*/
} recovery_strategy_t;

/* ============================================================
 * 配置参数
 * ============================================================ */
#define EXCEPTION_TASK_STACK        512
#define EXCEPTION_TASK_PRIORITY     osPriorityAboveNormal
#define EXCEPTION_CHECK_INTERVAL    500     /* 检查间隔（ms）*/

#define MAX_RECOVERY_ATTEMPTS       3       /* 最大恢复尝试次数 */
#define RECOVERY_DELAY_MS           1000    /* 恢复前等待时间 */
#define PHY_CHECK_THRESHOLD         10      /* PHY 连续检测阈值 */

/* ============================================================
 * 全局变量
 * ============================================================ */
extern osThreadId g_exception_task_handle;
extern exception_status_t g_exception_status;

/* ============================================================
 * 函数接口
 * ============================================================ */

/**
 * @brief 初始化异常处理模块
 * @note 在 main 函数中，任务创建前调用
 */
void mqtt_exception_init(void);

/**
 * @brief 创建异常处理任务
 */
void mqtt_exception_task_create(void);

/**
 * @brief 上报异常
 * @param type 异常类型
 */
void mqtt_exception_report(exception_type_t type);

/**
 * @brief 清除异常
 */
void mqtt_exception_clear(void);

/**
 * @brief 重置异常处理模块
 * @note 由网络任务调用,用于在复位时重置所有异常相关状态
 */
void mqtt_exception_reset(void);

/**
 * @brief 重置 DHCP 失败计数
 * @note DHCP 成功获取 IP 后调用
 */
void mqtt_exception_reset_dhcp_count(void);

/**
 * @brief 获取当前异常状态
 * @return 异常状态指针
 */
exception_status_t* mqtt_exception_get_status(void);

/**
 * @brief 异常处理任务 - 主循环
 */
void StartExceptionTask(void const * argument);

#endif /* _MQTT_EXCEPTION_H_ */
