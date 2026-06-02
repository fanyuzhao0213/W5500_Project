/**
 * @file mqtt_queue.c
 * @brief MQTT 发送/接收队列实现
 */

#include "mqtt_queue.h"
#include "LOG.h"

/* CMSIS-RTOS v1 队列定义 (必须全局/静态,osMessageCreate内部可能引用) */
static osMessageQDef_t g_tx_queue_def;
static osMessageQDef_t g_rx_queue_def;
static osMessageQDef_t g_ota_cmd_queue_def;

/* CMSIS-RTOS v1 队列对象 */
osMessageQId g_mqtt_tx_queue;
osMessageQId g_mqtt_rx_queue;
osMessageQId g_ota_cmd_queue;

/* 消息缓冲区池 (静态分配) */
#define MSG_BUF_POOL_SIZE      16
static mqtt_msg_t g_msg_buf_pool[MSG_BUF_POOL_SIZE];
static uint8_t g_msg_buf_used[MSG_BUF_POOL_SIZE];

/**
 * @brief 从池中分配一个消息缓冲区
 * @return 缓冲区索引, -1 表示无可用缓冲区
 */
static int msg_buf_alloc(void)
{
    int i;
    for (i = 0; i < MSG_BUF_POOL_SIZE; i++) {
        if (g_msg_buf_used[i] == 0) {
            g_msg_buf_used[i] = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief 释放池中消息缓冲区
 */
static void msg_buf_free(int idx)
{
    if (idx >= 0 && idx < MSG_BUF_POOL_SIZE) {
        g_msg_buf_used[idx] = 0;
    }
}

/**
 * @brief 获取可用缓冲区数量
 */
int mqtt_tx_queue_free_count(void)
{
    int count = 0;
    int i;
    for (i = 0; i < MSG_BUF_POOL_SIZE; i++) {
        if (g_msg_buf_used[i] == 0) {
            count++;
        }
    }
    return count;
}

/**
 * @brief 获取消息缓冲池状态
 */
int mqtt_msg_buf_available(void)
{
    int i;
    int count = 0;
    for (i = 0; i < MSG_BUF_POOL_SIZE; i++) {
        if (g_msg_buf_used[i] == 0) {
            count++;
        }
    }
    return count;
}

/**
 * @brief 初始化 MQTT 发送/接收队列
 */
int mqtt_queue_init(void)
{
    int i;

    /* 初始化消息缓冲区池 */
    for (i = 0; i < MSG_BUF_POOL_SIZE; i++) {
        g_msg_buf_used[i] = 0;
    }

    /* 创建发送队列 (CMSIS-RTOS v1) */
    g_tx_queue_def.queue_sz = MQTT_TX_QUEUE_SIZE;
    g_tx_queue_def.item_sz = sizeof(mqtt_msg_t *);  /* 队列存放指针 */

    g_mqtt_tx_queue = osMessageCreate(&g_tx_queue_def, NULL);
    if (g_mqtt_tx_queue == NULL) {
        LOGE("MQTT TX Queue: create failed");
        return -1;
    }
    LOGI("MQTT TX Queue: created (size=%d)", MQTT_TX_QUEUE_SIZE);

    /* 创建接收队列 (CMSIS-RTOS v1) */
    g_rx_queue_def.queue_sz = MQTT_RX_QUEUE_SIZE;
    g_rx_queue_def.item_sz = sizeof(mqtt_msg_t *);  /* 队列存放指针 */

    g_mqtt_rx_queue = osMessageCreate(&g_rx_queue_def, NULL);
    if (g_mqtt_rx_queue == NULL) {
        LOGE("MQTT RX Queue: create failed");
        return -1;
    }
    LOGI("MQTT RX Queue: created (size=%d)", MQTT_RX_QUEUE_SIZE);

    /* 创建 OTA 命令队列 (CMSIS-RTOS v1) */
    g_ota_cmd_queue_def.queue_sz = OTA_CMD_QUEUE_SIZE;
    g_ota_cmd_queue_def.item_sz = sizeof(mqtt_msg_t *);  /* 队列存放指针 */

    g_ota_cmd_queue = osMessageCreate(&g_ota_cmd_queue_def, NULL);
    if (g_ota_cmd_queue == NULL) {
        LOGE("OTA CMD Queue: create failed");
        return -1;
    }
    LOGI("OTA CMD Queue: created (size=%d)", OTA_CMD_QUEUE_SIZE);

    return 0;
}

/**
 * @brief 向 OTA 命令队列发送消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), 0表示不阻塞
 * @retval 0 成功, -1 失败
 */
int ota_cmd_queue_put(mqtt_msg_t *msg, uint32_t timeout)
{
    int buf_idx;
    mqtt_msg_t *p_buf;
    osStatus status;

    /* 检查队列是否有效 */
    if (g_ota_cmd_queue == NULL) {
        LOGE("OTA CMD Queue: queue is NULL!");
        return -1;
    }

    /* 从池中分配缓冲区 */
    buf_idx = msg_buf_alloc();
    if (buf_idx < 0) {
        LOGE("OTA CMD Queue: no buffer available");
        return -1;
    }

    p_buf = &g_msg_buf_pool[buf_idx];

    /* 复制消息数据 */
    memcpy(p_buf, msg, sizeof(mqtt_msg_t));

    /* 将指针放入队列 */
    status = osMessagePut(g_ota_cmd_queue, (uint32_t)p_buf, timeout);
    if (status != osOK) {
        LOGE("OTA CMD Queue: put failed, status=%d", status);
        msg_buf_free(buf_idx);
        return -1;
    }

    return 0;
}

/**
 * @brief 从 OTA 命令队列获取消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), osWaitForever表示永久等待
 * @retval osOK 成功, 其他失败
 */
osStatus ota_cmd_queue_get(mqtt_msg_t *msg, uint32_t timeout)
{
    osEvent evt;
    int buf_idx;

    evt = osMessageGet(g_ota_cmd_queue, timeout);
    if (evt.status == osEventMessage) {
        /* 复制消息数据到输出缓冲区 */
        *msg = *(mqtt_msg_t *)evt.value.p;
        /* 计算缓冲区索引并释放 */
        buf_idx = (mqtt_msg_t *)evt.value.p - g_msg_buf_pool;
        msg_buf_free(buf_idx);
        return osOK;
    }

    return evt.status;
}

/**
 * @brief 向发送队列发送消息
 * @note 消息会被复制到池中的缓冲区
 */
int mqtt_tx_queue_put(mqtt_msg_t *msg, uint32_t timeout)
{
    int buf_idx;
    mqtt_msg_t *p_buf;
    osStatus status;

    /* 从池中分配缓冲区 */
    buf_idx = msg_buf_alloc();
    if (buf_idx < 0) {
        LOGE("MQTT TX Queue: no buffer available");
        return -1;
    }

    p_buf = &g_msg_buf_pool[buf_idx];

    /* 复制消息数据 */
    memcpy(p_buf, msg, sizeof(mqtt_msg_t));

    /* 将指针放入队列 */
    status = osMessagePut(g_mqtt_tx_queue, (uint32_t)p_buf, timeout);
    if (status != osOK) {
        LOGE("MQTT TX Queue: put failed, status=%d", status);
        msg_buf_free(buf_idx);
        return -1;
    }

    return 0;
}

/**
 * @brief 从发送队列获取消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), osWaitForever表示永久等待
 * @retval osOK 成功, 其他失败
 * @note 调用者需要负责释放缓冲区
 */
osStatus mqtt_tx_queue_get(mqtt_msg_t *msg, uint32_t timeout)
{
    osEvent evt;
    int buf_idx;

    evt = osMessageGet(g_mqtt_tx_queue, timeout);
    if (evt.status == osEventMessage) {
        *msg = *(mqtt_msg_t *)evt.value.p;
        buf_idx = (mqtt_msg_t *)evt.value.p - g_msg_buf_pool;
        msg_buf_free(buf_idx);
        return osOK;
    }

    return evt.status;
}

/**
 * @brief 向接收队列发送消息 (内部使用, 由回调调用)
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), 0表示不阻塞
 * @retval 0 成功, -1 失败
 */
int mqtt_rx_queue_put(mqtt_msg_t *msg, uint32_t timeout)
{
    int buf_idx;
    mqtt_msg_t *p_buf;
    osStatus status;

    /* 从池中分配缓冲区 */
    buf_idx = msg_buf_alloc();
    if (buf_idx < 0) {
        LOGW("MQTT RX Queue: no buffer available");
        return -1;
    }

    p_buf = &g_msg_buf_pool[buf_idx];

    /* 复制消息数据 */
    memcpy(p_buf, msg, sizeof(mqtt_msg_t));

    /* 将指针放入队列 */
    status = osMessagePut(g_mqtt_rx_queue, (uint32_t)p_buf, timeout);
    if (status != osOK) {
        LOGE("MQTT RX Queue: put failed, status=%d", status);
        msg_buf_free(buf_idx);
        return -1;
    }

    return 0;
}

/**
 * @brief 从接收队列获取消息
 * @param msg 消息指针
 * @param timeout 超时时间 (ms), osWaitForever表示永久等待
 * @retval osOK 成功, 其他失败
 * @note 调用者需要负责释放缓冲区
 */
osStatus mqtt_rx_queue_get(mqtt_msg_t *msg, uint32_t timeout)
{
    osEvent evt;
    int buf_idx;

    evt = osMessageGet(g_mqtt_rx_queue, timeout);
    if (evt.status == osEventMessage) {
        /* 复制消息数据到输出缓冲区 */
        *msg = *(mqtt_msg_t *)evt.value.p;
        /* 计算缓冲区索引并释放 */
        buf_idx = (mqtt_msg_t *)evt.value.p - g_msg_buf_pool;
        msg_buf_free(buf_idx);
        return osOK;
    }

    return evt.status;
}

/**
 * @brief 检查发送队列是否有消息 (非阻塞)
 * @return 1 有消息, 0 无消息
 */
int mqtt_tx_queue_available(void)
{
    osEvent evt;

    /* 使用 osMessageGet(timeout=0) 非阻塞检查 */
    evt = osMessageGet(g_mqtt_tx_queue, 0);
    if (evt.status == osEventMessage) {
        /* 有消息, 需要放回去并返回1 */
        osMessagePut(g_mqtt_tx_queue, (uint32_t)evt.value.p, 0);
        return 1;
    }

    return 0;
}

