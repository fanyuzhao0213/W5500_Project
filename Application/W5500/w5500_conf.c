/**
 * @file w5500_conf.c
 * @brief W5500 SPI 接口配置
 */

#include "w5500_conf.h"
#include "wizchip_conf.h"
#include "spi.h"
#include "main.h"
#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi1;

/* W5500 CS GPIO Port and Pin */
#define W5500_CS_PORT  GPIOA
#define W5500_CS_PIN   GPIO_PIN_4

/* W5500 RST GPIO Port and Pin */
#define W5500_RST_PORT GPIOB
#define W5500_RST_PIN  GPIO_PIN_0

/**
 * @brief  进入临界区 (禁用中断)
 */
static void w5500_cris_enter(void)
{
    __set_PRIMASK(1);
}

/**
 * @brief  退出临界区 (使能中断)
 */
static void w5500_cris_exit(void)
{
    __set_PRIMASK(0);
}

/**
 * @brief  选中 W5500 (CS 低电平)
 */
static void w5500_cs_select(void)
{
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  取消选中 W5500 (CS 高电平)
 */
static void w5500_cs_deselect(void)
{
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief  通过 SPI 读取一个字节 (发送0xFF dummy)
 * @return 接收的数据
 */
static uint8_t w5500_spi_readbyte(void)
{
    uint8_t tx_byte = 0xFF;
    uint8_t rx_byte;
    HAL_SPI_TransmitReceive(&hspi1, &tx_byte, &rx_byte, 1, HAL_MAX_DELAY);
    return rx_byte;
}

/**
 * @brief  通过 SPI 发送一个字节
 * @param  wb 发送的数据
 */
static void w5500_spi_writebyte(uint8_t wb)
{
    HAL_SPI_Transmit(&hspi1, &wb, 1, HAL_MAX_DELAY);
}

/**
 * @brief  批量 SPI 读取
 * @param  buf 接收数据缓冲区
 * @param  len 数据长度
 */
static void w5500_spi_readburst(uint8_t* buf, uint16_t len)
{
    uint8_t tx_byte = 0xFF;
    uint8_t rx_byte;
    uint16_t i;

    for (i = 0; i < len; i++) {
        HAL_SPI_TransmitReceive(&hspi1, &tx_byte, &rx_byte, 1, HAL_MAX_DELAY);
        buf[i] = rx_byte;
    }
}

/**
 * @brief  批量 SPI 发送
 * @param  buf 发送数据缓冲区
 * @param  len 数据长度
 */
static void w5500_spi_writeburst(uint8_t* buf, uint16_t len)
{
    HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY);
}

/**
 * @brief  硬件复位 W5500
 */
void w5500_hard_reset(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIOB 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 配置 RST 引脚为输出 */
    GPIO_InitStruct.Pin = W5500_RST_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(W5500_RST_PORT, &GPIO_InitStruct);

    /* 拉低复位 */
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(50);

    /* 释放复位 */
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
}

/**
 * @brief  注册 W5500 SPI 回调函数
 */
void wizchip_spi_cbfunc(void)
{
    /* 注册临界区回调 (禁用/使能中断) */
    reg_wizchip_cris_cbfunc(w5500_cris_enter, w5500_cris_exit);

    /* 注册片选回调 */
    reg_wizchip_cs_cbfunc(w5500_cs_select, w5500_cs_deselect);

    /* 注册单字节 SPI 回调 */
    reg_wizchip_spi_cbfunc(w5500_spi_readbyte, w5500_spi_writebyte);

    /* 注册批量 SPI 回调 */
    reg_wizchip_spiburst_cbfunc(w5500_spi_readburst, w5500_spi_writeburst);
}

