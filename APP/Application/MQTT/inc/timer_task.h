/**
 * @file timer_task.h
 * @brief 定时器任务管理
 */

#ifndef _TIMER_TASK_H_
#define _TIMER_TASK_H_

#include <stdint.h>
#include "cmsis_os.h"

/* 定时器任务句柄 */
extern osThreadId g_timer_task_handle;

/* 定时器任务优先级 */
#define TIMER_TASK_PRIORITY    osPriorityBelowNormal
#define TIMER_TASK_STACK       512

/* 定时器任务创建 */
void timer_task_create(void);

/* 获取当前时间戳 (ms) */
uint32_t timer_task_get_tick(void);

/* 定时器任务主体 */
void StartTimerTask(void const *argument);

/* 用户回调：LED闪烁 */
void user_led_blink(void);

/* 用户回调：MQTT定时发送 */
void user_mqtt_publish(void);

#endif /* _TIMER_TASK_H_ */

