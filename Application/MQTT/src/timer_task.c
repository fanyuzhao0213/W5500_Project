/**
 * @file timer_task.c
 * @brief 定时器任务管理实现
 */

#include "timer_task.h"
#include "mqtt_task.h"
#include "mqtt_queue.h"
#include "mqtt_config.h"
#include "LOG.h"
#include "main.h"

/* 任务句柄 */
osThreadId g_timer_task_handle;

/* ============================================
 * 定时器任务配置 (使用宏定义)
 * ============================================ */
#define TIMER_TASK_COUNT    3

/* 任务0: 空闲任务 (1ms间隔) */
#define TASK0_ENABLE        0
#define TASK0_INTERVAL_MS   1
#define TASK0_CALLBACK      NULL

/* 任务1: LED闪烁 (500ms间隔) */
#define TASK1_ENABLE        1
#define TASK1_INTERVAL_MS   200
#define TASK1_CALLBACK      user_led_blink

/* 任务2: MQTT定时发送 (5000ms间隔) */
#define TASK2_ENABLE        1
#define TASK2_INTERVAL_MS   5000
#define TASK2_CALLBACK      user_mqtt_publish

/* 定时器任务结构体 */
typedef struct {
    uint8_t enable;           /* 使能标志 */
    uint32_t interval_ms;     /* 执行间隔(ms) */
    uint32_t last_tick;       /* 上次执行时间 */
    void (*callback)(void);    /* 回调函数 */
} timer_item_t;

/* 定时器任务列表 (使用宏初始化) */
static timer_item_t g_timer_list[TIMER_TASK_COUNT] = {
    {TASK0_ENABLE, TASK0_INTERVAL_MS, 0, TASK0_CALLBACK},
    {TASK1_ENABLE, TASK1_INTERVAL_MS, 0, TASK1_CALLBACK},
    {TASK2_ENABLE, TASK2_INTERVAL_MS, 0, TASK2_CALLBACK},
};

/* 上次空闲任务执行时间 */
static uint32_t g_last_idle_tick = 0;

/**
 * @brief 创建定时器任务
 */
void timer_task_create(void)
{
    osThreadDef(timerTask, StartTimerTask, TIMER_TASK_PRIORITY, 0, TIMER_TASK_STACK);
    g_timer_task_handle = osThreadCreate(osThread(timerTask), NULL);
    if (g_timer_task_handle == NULL) {
        LOGE("TimerTask: create failed");
    } else {
        LOGI("TimerTask: created");
    }
}

/**
 * @brief 获取当前时间戳 (ms)
 * @note 使用 HAL_GetTick()
 */
uint32_t timer_task_get_tick(void)
{
    return HAL_GetTick();
}

/**
 * @brief 用户回调：LED闪烁 (500ms)
 */
void user_led_blink(void)
{
    static uint8_t led_state = 0;
    led_state = !led_state;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, led_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief 用户回调：MQTT定时发送 (5s)
 */
void user_mqtt_publish(void)
{
    mqtt_msg_t pub_msg;
    uint32_t current_tick = timer_task_get_tick();

    if (!mqtt_is_running()) {
        return;
    }

    /* 准备发送数据 */
    memset(&pub_msg, 0, sizeof(pub_msg));
    snprintf(pub_msg.topic, sizeof(pub_msg.topic), "%s", MQTT_PUBLISH_TOPIC);
    snprintf(pub_msg.payload, sizeof(pub_msg.payload),
             "{\"uptime_ms\":%lu}", (unsigned long)current_tick);
    pub_msg.len = strlen(pub_msg.payload);
    pub_msg.qos = QOS1;

    /* 放入发送队列 */
    if (mqtt_tx_queue_put(&pub_msg, 0) == 0) {
        LOGI("TimerTask: publish queued success!");
    }
}

/**
 * @brief 空闲任务回调 (1ms)
 * @note 这里是空闲任务，可用于系统监测
 */
static void user_idle_task(void)
{
    /* 可以添加其他空闲时执行的监测代码 */
}

/**
 * @brief 定时器任务主体
 * @param argument 参数
 */
void StartTimerTask(void const *argument)
{
    uint32_t current_tick;
    uint8_t i;

    (void)argument;

    LOGI("TimerTask: started");

    /* 初始化各任务的last_tick */
    for (i = 0; i < TIMER_TASK_COUNT; i++) {
        g_timer_list[i].last_tick = timer_task_get_tick();
    }
    g_last_idle_tick = g_timer_list[0].last_tick;

    for (;;) {
        current_tick = timer_task_get_tick();

        /* 遍历所有定时器任务 */
        for (i = 0; i < TIMER_TASK_COUNT; i++) {
            /* 检查是否使能 */
            if (!g_timer_list[i].enable) {
                continue;
            }

            /* 检查是否到达执行时间 */
            if ((current_tick - g_timer_list[i].last_tick) >= g_timer_list[i].interval_ms) {
                /* 更新上次执行时间 */
                g_timer_list[i].last_tick = current_tick;

                /* 执行回调 */
                if (g_timer_list[i].callback != NULL) {
                    g_timer_list[i].callback();
                }
            }
        }

        /* 空闲任务处理 */
        if ((current_tick - g_last_idle_tick) >= 1) {  /* 1ms间隔 */
            g_last_idle_tick = current_tick;
            user_idle_task();
        }

        /* 让出CPU，1ms轮询一次 */
        osDelay(1);
    }
}

