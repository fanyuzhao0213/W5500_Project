#include "app_task.h"
#include "cmsis_os.h"
#include "LOG.h"
#include "mqtt_queue.h"
#include "mqtt_fsm.h"
#include "net_manager.h"
#include "MQTTClient.h"

void app_task_init(void) {
    LOGI("App Task: initialized");
}

void StartAppTask(void const *argument) {
    mqtt_msg_t msg;
    uint32_t last_data_send = 0;

    LOGI("AppTask: started");

    while(1) {
        /* 处理接收队列中的消息 */
        while (mqtt_queue_pop_rx(&msg) == 0) {
            LOGI("APP: Received message");
            LOGI("APP: TOPIC: %s", msg.topic);
            LOGI("APP: PAYLOAD: %.*s", msg.len, msg.payload);

            /* 在这里添加业务逻辑处理 */
        }

        /* 模拟业务数据发送 */
        if (HAL_GetTick() - last_data_send >= 10000) {
            last_data_send = HAL_GetTick();

            /* 通过队列发送消息，而不是直接调用publish */
            mqtt_queue_push("stm32/data", (uint8_t*)"Sensor data", 12, QOS0);
            LOGI("APP: Sent data to queue");
        }

        osDelay(50);
    }
}
