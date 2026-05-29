#include "mqtt_task.h"
#include "LOG.h"
#include "mqtt_fsm.h"
#include "mqtt_queue.h"
#include "mqtt_client.h"
#include "net_manager.h"

void mqtt_task_init(void) {
    mqtt_fsm_init();
    LOGI("MQTT Task: initialized");
}

void StartMQTTTask(void const *argument) {
    mqtt_msg_t msg;
    uint32_t last_publish_time = 0;
    static mqtt_state_t prev_state = MQTT_STATE_IDLE;

    LOGI("MQTTTask: started");
    LOGI("MQTTTask: waiting for network ready...");

    while(1) {
        /* 处理MQTT状态机 */
        mqtt_fsm_process();

        /* 状态变化日志 */
        if (mqtt_fsm_get_state() != prev_state) {
            prev_state = mqtt_fsm_get_state();
            LOGI("MQTTTask: State changed to %d", prev_state);
        }

        /* 如果MQTT运行中，处理发送队列 */
        if (mqtt_fsm_get_state() == MQTT_STATE_RUNNING) {
            /* 从队列读取消息并发送 */
            while (mqtt_queue_pop(&msg) == 0) {
                LOGI("MQTTTask: Publishing message to %s (len=%d)", msg.topic, msg.len);
                mqtt_client_publish(msg.topic, (char*)msg.payload, msg.len, msg.qos);
            }

            /* 定时发布测试消息 */
            if (HAL_GetTick() - last_publish_time >= 5000) {
                last_publish_time = HAL_GetTick();
                LOGI("MQTTTask: Publishing heartbeat to stm32/uptime");
                mqtt_client_publish("stm32/uptime", "Hello STM32 MQTT", 17, QOS0);
            }
        }

        osDelay(10);
    }
}