/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "LOG.h"
#include "w5500_conf.h"
#include "netconf.h"
#include "wizchip_conf.h"
#include "tcp_client.h"
#include "socket.h"
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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId NetworkTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void StartNetworkTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of NetworkTask */
  osThreadDef(NetworkTask, StartNetworkTask, osPriorityHigh, 0, 256);
  NetworkTaskHandle = osThreadCreate(osThread(NetworkTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  (void)argument;
  for(;;)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    osDelay(500);
  }
}

/* USER CODE BEGIN Header_StartNetworkTask */
/**
  * @brief  Function implementing the NetworkTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartNetworkTask */
void StartNetworkTask(void const * argument)
{
  (void)argument;
  uint8_t version;

  LOGI("NetworkTask: W5500_Init...");

  /* 注册 SPI 回调函数 */
  wizchip_spi_cbfunc();

  /* 初始化 W5500 socket 缓冲区和寄存器 */
  uint8_t snum[] = {16, 16, 16, 16, 16, 16, 16, 16};  /* 8个socket, 每个16KB */
  wizchip_init(snum, snum);

  /* 等待 W5500 就绪 */
  HAL_Delay(100);

  /* 验证 VERSIONR */
  version = getVERSIONR();
  LOGI("NetworkTask: W5500 VERSIONR = 0x%02X", version);

  if (version == 0x04) {
      LOGI("NetworkTask: W5500 OK!");
  } else {
      LOGE("NetworkTask: W5500 ERROR! Expected 0x04, got 0x%02X", version);
  }

  /* 初始化网络配置 (静态IP) */
  NetConfig_Init();

  LOGI("NetworkTask: Connecting to MQTT broker...");

  /* 连接 MQTT Broker */
  if (tcp_client_connect() != 0) {
      LOGE("NetworkTask: TCP connect failed!");
  } else {
      LOGI("NetworkTask: TCP connected!");
  }

  for(;;)
  {
    /* 处理网络事件 */
    if (tcp_client_is_connected()) {
      uint8_t buf[256];
      int ret;

      /* 发送测试数据 */
      ret = tcp_client_send((uint8_t*)"Hello from STM32!", 18);
      if (ret > 0) {
        LOGI("NetworkTask: sent %d bytes", ret);
      }

      /* 接收服务器返回的数据 */
      ret = tcp_client_recv(buf, sizeof(buf));
      if (ret > 0) {
        buf[ret] = '\0';  /* 添加字符串结束符 */
        LOGI("NetworkTask: received %d bytes: %s", ret, buf);
      }
    }

    osDelay(1000);
  }
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
