/**
 * @file cli_task.h
 * @brief FreeRTOS CLI 任务声明
 */

#ifndef _CLI_TASK_H_
#define _CLI_TASK_H_

#include "cmsis_os.h"

/* 任务句柄外部声明 */
extern osThreadId g_cli_task_handle;

/**
 * @brief 创建CLI任务
 */
void cli_task_create(void);

/**
 * @brief CLI任务入口
 */
void StartCLITask(void const *argument);

#endif /* _CLI_TASK_H_ */


