/**
 * @file w5500_conf.h
 * @brief W5500 SPI 接口配置头文件
 */

#ifndef _W5500_CONF_H_
#define _W5500_CONF_H_

#include <stdint.h>

/* W5500 CS GPIO Port and Pin */
#define W5500_CS_PORT  GPIOA
#define W5500_CS_PIN   GPIO_PIN_4

/* W5500 RST GPIO Port and Pin */
#define W5500_RST_PORT GPIOB
#define W5500_RST_PIN  GPIO_PIN_0

/**
 * @brief 硬件复位 W5500
 */
void w5500_hard_reset(void);

/**
 * @brief 注册 W5500 SPI 回调函数
 */
void wizchip_spi_cbfunc(void);

#endif /* _W5500_CONF_H_ */

