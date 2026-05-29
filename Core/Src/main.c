/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "adc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "LOG.h"
#include "stm32f4xx_hal.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
IWDG_HandleTypeDef hiwdg;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 *==============================================================================
 * 看门狗 (IWDG) 配置
 *==============================================================================
 *
 * IWDG 超时计算公式:
 *   Timeout = (Prescaler × Reload) / LSI_Frequency
 *
 * IWDG 预分频设置:
 *   IWDG_PRESCALER_4   = 0x00  -> 分频 4
 *   IWDG_PRESCALER_8   = 0x01  -> 分频 8
 *   IWDG_PRESCALER_16  = 0x02  -> 分频 16
 *   IWDG_PRESCALER_32  = 0x03  -> 分频 32
 *   IWDG_PRESCALER_64  = 0x04  -> 分频 64
 *   IWDG_PRESCALER_128 = 0x05  -> 分频 128
 *   IWDG_PRESCALER_256 = 0x06  -> 分频 256  (常用)
 *
 * LSI 频率: 约 32kHz (实际范围 30kHz ~ 40kHz)
 *
 * 计算示例 - 目标 10秒超时:
 *   Reload = (Timeout × LSI_Freq) / Prescaler
 *         = (10 × 32000) / 256
 *         = 1250
 *
 * 常用超时配置:
 *   1秒:  Reload = (1 × 32000) / 256 = 125
 *   5秒:  Reload = (5 × 32000) / 256 = 625
 *   10秒: Reload = (10 × 32000) / 256 = 1250
 *   20秒: Reload = (20 × 32000) / 256 = 2500 (超过最大值 0xFFF)
 *
 *==============================================================================
 */

/**
 * @brief  初始化看门狗 (10秒超时)
 */
void watchdog_init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;  /* 256分频 */
    hiwdg.Init.Reload = 1250;                   /* 10秒 @32kHz */
    HAL_IWDG_Init(&hiwdg);
    LOGI("Watchdog: initialized (timeout=10s)");
}

/**
 * @brief  喂狗 (刷新计数器)
 */
void watchdog_feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

/**
 * @brief  系统软复位
 */
void system_reset(void)
{
    LOGI("System: software reset");
    HAL_Delay(100);
    NVIC_SystemReset();
}

/**
 * @brief  打印系统重启原因
 * @note   需要在 main 函数开始处调用
 */
void print_reset_cause(void)
{
    uint32_t csr_value = RCC->CSR;

    LOGI("====================");
    LOGI("Last Reset Cause:");

    if (csr_value & RCC_CSR_LPWRRSTF) {
        LOGI("  - Low-power reset (LPWRRSTF)");
    }
    if (csr_value & RCC_CSR_WWDGRSTF) {
        LOGI("  - Window watchdog reset (WWDGRSTF)");
    }
    if (csr_value & RCC_CSR_IWDGRSTF) {
        LOGI("  - Independent watchdog reset (IWDGRSTF)");
    }
    if (csr_value & RCC_CSR_SFTRSTF) {
        LOGI("  - Software reset (SFTRSTF)");
    }
    if (csr_value & RCC_CSR_PORRSTF) {
        LOGI("  - Power-on / POR reset (PORRSTF)");
    }
    if (csr_value & RCC_CSR_PINRSTF) {
        LOGI("  - Pin reset (NRST pin) (PINRSTF)");
    }
    if (csr_value & RCC_CSR_BORRSTF) {
        LOGI("  - Brown-out reset (BORRSTF)");
    }
    if ((csr_value & (RCC_CSR_LPWRRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_IWDGRSTF |
                      RCC_CSR_SFTRSTF | RCC_CSR_PORRSTF | RCC_CSR_PINRSTF |
                      RCC_CSR_BORRSTF)) == 0) {
        LOGI("  - Unknown or other reset cause");
    }

    LOGI("====================");

    /* 清除复位标志，以便下次读取准确 */
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_UART4_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  print_reset_cause();
  LOG_CLEAR();
  LOGI("======================================");
  LOGI("STM32F4 FreeRTOS RTT Project Started");
  LOGI("System Clock: 168 MHz");
  LOGI("RTT Log System Initialized");
  LOGI("======================================");

  /* 初始化看门狗 */
  watchdog_init();

  /* USER CODE END 2 */

  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

