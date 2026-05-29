#ifndef _WATCHDOG_H_
#define _WATCHDOG_H_

#include "stm32f4xx_hal.h"

/* IWDG句柄 */
extern IWDG_HandleTypeDef hiwdg;

/* Watchdog初始化 */
void watchdog_init(void);

/* Watchdog喂狗 */
void watchdog_feed(void);

/* 检查是否应该喂狗（MQTT正常运行时才喂狗）*/
void watchdog_check_and_feed(void);

#endif /* _WATCHDOG_H_ */
