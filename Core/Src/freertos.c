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
#include "main.h"
#include "mqtt_client.h"
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
void mqtt_message_callback(MessageData* md);
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
}

/**
 * @brief 空闲任务钩子 - 在空闲任务中喂狗
 * @note 空闲任务是系统最底层保障,如果连空闲任务都卡住,说明真的有问题
 *       在这里喂狗可以确保系统不会因为业务逻辑卡死而不复位
 */
void vApplicationIdleHook(void)
{
    extern IWDG_HandleTypeDef hiwdg;
    HAL_IWDG_Refresh(&hiwdg);
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
  osThreadDef(NetworkTask, StartNetworkTask, osPriorityNormal, 0, 512);
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

void mqtt_message_callback(MessageData* md)
{
    char* topic = (char*)md->topicName->lenstring.data;
    char* payload = (char*)md->message->payload;

    LOGI("TOPIC: %.*s", md->topicName->lenstring.len, topic);
    LOGI("PAYLOAD: %.*s", (int)md->message->payloadlen, payload);
}

void StartNetworkTask(void const * argument)
{
  extern IWDG_HandleTypeDef hiwdg;
  (void)argument;
  uint8_t version;
  uint32_t last_publish_time = 0;

  LOGI("NetworkTask: step 1 - task started");
  HAL_IWDG_Refresh(&hiwdg);

  LOGI("NetworkTask: step 2 - registering SPI cbfunc");
  wizchip_spi_cbfunc();

  LOGI("NetworkTask: step 3 - calling wizchip_init");
  uint8_t snum[] = {16, 16, 16, 16, 16, 16, 16, 16};
  wizchip_init(snum, snum);

  LOGI("NetworkTask: step 4 - delay 100ms");
  HAL_Delay(100);

  LOGI("NetworkTask: step 5 - reading VERSIONR");
  version = getVERSIONR();
  LOGI("NetworkTask: step 6 - VERSIONR = 0x%02X", version);

  if (version == 0x04) {
      LOGI("NetworkTask: W5500 OK!");
  } else {
      LOGE("NetworkTask: W5500 ERROR! Expected 0x04, got 0x%02X", version);
  }

  LOGI("NetworkTask: step 7 - calling NetConfig_Init");
  NetConfig_Init();

  LOGI("NetworkTask: step 8 - initializing MQTT client");
  mqtt_client_init();

  LOGI("NetworkTask: step 9 - connecting to MQTT broker");
  if (mqtt_client_connect() != MQTT_SUCCESS) {
      LOGE("NetworkTask: MQTT connect failed!");
      while(1) {
          osDelay(1000);
      }
  }

  LOGI("NetworkTask: step 10 - subscribing to topic: %s", MQTT_SUBSCRIBE_TOPIC);
  mqtt_client_subscribe(MQTT_SUBSCRIBE_TOPIC, QOS0, mqtt_message_callback);

  LOGI("NetworkTask: MQTT setup complete, entering main loop");

  for(;;)
  {
    HAL_IWDG_Refresh(&hiwdg);

    mqtt_client_loop(100);

    if (HAL_GetTick() - last_publish_time >= MQTT_PUBLISH_INTERVAL) {
        last_publish_time = HAL_GetTick();
        mqtt_client_publish(MQTT_PUBLISH_TOPIC, "Hello STM32 MQTT", strlen("Hello STM32 MQTT"), QOS0);
    }

    osDelay(10);
  }
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */

