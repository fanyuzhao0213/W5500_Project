/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Bootloader main program for OTA upgrade
  * @version        : v1.0.0
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "bootloader_config.h"
#include "bootloader_utils.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
#define BOOT_TIMEOUT_MS     3000    // 启动超时时间
#define LED_BLINK_FAST      100     // 快速闪烁间隔
#define LED_BLINK_SLOW      500     // 慢速闪烁间隔

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
uint32_t boot_start_time = 0;
uint8_t led_state = 0;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void bootloader_main(void);
static void bootloader_boot_app(uint32_t app_addr);
static void bootloader_handle_ota(ota_params_t *params);
static void led_set_state(uint8_t state);
static void led_blink(uint32_t interval_ms);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_UART4_Init();

  /* USER CODE BEGIN 2 */
  PRINTF("\r\n====================================\r\n");
  PRINTF("Bootloader :%s\r\n",BOOTLOADER_VERSION);
  PRINTF("STM32F405RG - OTA Bootloader\r\n");
  PRINTF("====================================\r\n");

  boot_start_time = HAL_GetTick();
  led_set_state(LED_BOOTING);

  /* USER CODE END 2 */

  /* Bootloader main logic */
  bootloader_main();

  /* Should never reach here */
  while (1)
  {
    led_set_state(LED_ERROR);
  }
}

/**
  * @brief  Bootloader 主逻辑
  *
  * 流程：
  * 1. 读取 OTA 参数
  * 2. 处理 OTA 升级/回滚逻辑
  * 3. 启动应用程序
  */
static void bootloader_main(void)
{
    ota_params_t params;
    int ret;

    PRINTF("\r\n[BOOT] Reading OTA parameters...\r\n");

    // 从 Flash 读取 OTA 参数
    ret = bootloader_read_params(&params);
    if (ret != 0) {
        PRINTF("[WARN] OTA params not found, using defaults\r\n");

        // 首次启动，使用默认参数
        params.ota_flag = OTA_FLAG_NORMAL;      // 正常启动模式
        params.active_app = APP_A;              // 默认启动 App-A
        params.boot_count = 0;                  // 启动计数清零
        params.app_a_valid = 1;                 // App-A 有效
        params.app_b_valid = 0;                 // App-B 无效
    } else {
        PRINTF("[INFO] OTA params loaded:\r\n");
        PRINTF("       OTA Flag: %d\r\n", params.ota_flag);
        PRINTF("       Active App: %d\r\n", params.active_app);
        PRINTF("       Boot Count: %d/%d\r\n", params.boot_count, params.max_boot_count);
        PRINTF("       App-A Valid: %d\r\n", params.app_a_valid);
        PRINTF("       App-B Valid: %d\r\n", params.app_b_valid);
    }

    // 处理 OTA 逻辑（升级/回滚）
    bootloader_handle_ota(&params);
}

/**
  * @brief  处理 OTA 升级和回滚逻辑
  *
  * 主要流程：
  * 1. 确定要启动的应用分区
  * 2. 验证固件有效性
  * 3. 处理 OTA 升级验证
  * 4. 处理 OTA 回滚
  * 5. 跳转到应用程序
  */
static void bootloader_handle_ota(ota_params_t *params)
{
    uint32_t app_addr;
    firmware_header_t header;
    int ret;

    // 根据激活标志和有效性确定要启动的应用地址
    if (params->active_app == APP_B && params->app_b_valid) {
        app_addr = APP_B_ADDR;
        PRINTF("[INFO] Selected App-B (0x%08lX)\r\n", app_addr);
    } else {
        app_addr = APP_A_ADDR;
        PRINTF("[INFO] Selected App-A (0x%08lX)\r\n", app_addr);
    }

    // 验证固件有效性（栈指针、复位向量）
    PRINTF("[BOOT] Validating firmware...\r\n");
    ret = bootloader_validate_firmware(app_addr, &header);

    if (ret != 0) {
        PRINTF("[ERROR] Firmware validation failed (code: %d)\r\n", ret);
        PRINTF("[ERROR] -1: Invalid stack pointer\r\n");
        PRINTF("[ERROR] -2: Invalid reset vector\r\n");

        // 如果 App-A 无效，尝试 App-B 作为备份
        if (app_addr == APP_A_ADDR && params->app_b_valid) {
            PRINTF("[INFO] Trying App-B as fallback...\r\n");
            app_addr = APP_B_ADDR;
            ret = bootloader_validate_firmware(app_addr, &header);
        }

        // 如果两个分区都无效，进入错误状态
        if (ret != 0) {
            PRINTF("[ERROR] No valid firmware found!\r\n");
            led_set_state(LED_ERROR);
            while (1);
        }
    }

    // 打印固件信息
    if (header.size > 0) {
        PRINTF("[INFO] Firmware validated:\r\n");
        PRINTF("       Version: %lu\r\n", header.version);
        PRINTF("       Size: %lu bytes\r\n", header.size);
        PRINTF("       CRC32: 0x%08lX\r\n", header.crc32);
    } else {
        PRINTF("[INFO] ARM application validated (no header)\r\n");
    }

    // OTA 升级处理逻辑
    // 当 ota_flag = PENDING 时，表示新固件首次启动，需要验证稳定性
    if (params->ota_flag == OTA_FLAG_PENDING) {
        PRINTF("[OTA] Upgrade pending, verifying new firmware...\r\n");
        led_set_state(LED_UPGRADE);

        // 启动计数增加
        params->boot_count++;
        PRINTF("[OTA] Boot count: %d/%d\r\n", params->boot_count, params->max_boot_count);

        // 达到最大验证次数，确认升级成功
        if (params->boot_count >= params->max_boot_count) {
            PRINTF("[OTA] New firmware verified successfully!\r\n");
            params->ota_flag = OTA_FLAG_SUCCESS;   // 设置为升级成功
            params->boot_count = 0;                 // 重置启动计数

            // 更新分区有效性标志
            if (params->active_app == APP_A) {
                params->app_a_valid = 1;  // App-A 有效
                params->app_b_valid = 0;  // App-B 无效
            } else {
                params->app_b_valid = 1;  // App-B 有效
                params->app_a_valid = 0;  // App-A 无效
            }

            // 写入 OTA 参数到 Flash
            bootloader_write_params(params);
            PRINTF("[OTA] Upgrade completed!\r\n");
        } else {
            // 继续验证，保存当前启动计数
            bootloader_write_params(params);
            PRINTF("[OTA] Continuing verification...\r\n");
        }
    }

    // OTA 回滚处理逻辑
    // 当 ota_flag = ROLLBACK 时，表示新固件有问题，需要回滚到旧版本
    if (params->ota_flag == OTA_FLAG_ROLLBACK) {
        PRINTF("[OTA] Rollback triggered!\r\n");
        led_set_state(LED_ERROR);

        // 如果当前在 App-B，回滚到 App-A
        if (params->active_app == APP_B && params->app_a_valid) {
            PRINTF("[OTA] Rolling back to App-A...\r\n");
            params->active_app = APP_A;            // 切换到 App-A
            params->ota_flag = OTA_FLAG_NORMAL;    // 恢复正常模式
            params->boot_count = 0;                // 重置启动计数
            bootloader_write_params(params);

            app_addr = APP_A_ADDR;
        }
        // 如果当前在 App-A，回滚到 App-B
        else if (params->active_app == APP_A && params->app_b_valid) {
            PRINTF("[OTA] Rolling back to App-B...\r\n");
            params->active_app = APP_B;            // 切换到 App-B
            params->ota_flag = OTA_FLAG_NORMAL;    // 恢复正常模式
            params->boot_count = 0;                // 重置启动计数
            bootloader_write_params(params);

            app_addr = APP_B_ADDR;
        } else {
            // 没有有效的固件可以回滚，进入错误状态
            PRINTF("[ERROR] No valid firmware to rollback!\r\n");
            while (1) {
                led_set_state(LED_ERROR);
            }
        }
    }

    // 跳转到应用程序
    bootloader_boot_app(app_addr);
}

/**
  * @brief  启动应用程序
  * @param  app_addr 应用程序起始地址
  *
  * 流程：
  * 1. 打印启动信息
  * 2. 关闭 LED
  * 3. 跳转到应用程序
  * 4. 如果跳转失败，进入错误状态
  */
static void bootloader_boot_app(uint32_t app_addr)
{
    PRINTF("[BOOT] Jumping to application at 0x%08lX...\r\n", app_addr);
    PRINTF("====================================\r\n\r\n");

    // 关闭 LED
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

    // 跳转到应用程序（设置栈指针、VTOR、跳转到 Reset Handler）
    bootloader_jump_to_app(app_addr);

    // 如果跳转失败，进入死循环
    PRINTF("[ERROR] Failed to jump to application!\r\n");
    while (1) {
        led_set_state(LED_ERROR);
    }
}

/**
  * @brief  Set LED state
  */
static void led_set_state(uint8_t state)
{
    switch (state) {
        case LED_BOOTING:
            // 快速闪烁
            led_blink(LED_BLINK_FAST);
            break;
        case LED_OK:
            // 常亮
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            break;
        case LED_UPGRADE:
            // 慢速闪烁
            led_blink(LED_BLINK_SLOW);
            break;
        case LED_ERROR:
            // 熄灭
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            break;
        default:
            break;
    }
}

/**
  * @brief  Blink LED with specified interval
  */
static void led_blink(uint32_t interval_ms)
{
    if ((HAL_GetTick() - boot_start_time) % interval_ms < interval_ms / 2) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }
}

/**
  * @brief System Clock Configuration
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
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  PRINTF("[ERROR] Fatal error, halting!\r\n");
  __disable_irq();
  while (1)
  {
    led_set_state(LED_ERROR);
    HAL_Delay(500);
    led_set_state(LED_BOOTING);
    HAL_Delay(500);
  }
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
  PRINTF("[ASSERT] File: %s, Line: %d\r\n", file, line);
}
#endif /* USE_FULL_ASSERT */
