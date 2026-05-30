/**
 * @file cli_commands.c
 * @brief FreeRTOS CLI 命令实现
 */

#include "cli_commands.h"
#include "mqtt_task.h"
#include "netconf.h"
#include "wizchip_conf.h"
#include "LOG.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* ============================
 * MQTT 状态命令: mqtt
 * ============================ */
static BaseType_t prvMqttCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString)
{
    (void)pcCommandString;

    net_state_t state = mqtt_task_get_state();
    int running = mqtt_is_running();

    const char *state_str;
    switch (state) {
        case NET_STATE_INIT:          state_str = "INIT";          break;
        case NET_STATE_W5500_CHECK:   state_str = "W5500_CHECK";   break;
        case NET_STATE_NET_CONFIG:    state_str = "NET_CONFIG";    break;
        case NET_STATE_MQTT_INIT:     state_str = "MQTT_INIT";     break;
        case NET_STATE_MQTT_CONNECT:  state_str = "MQTT_CONNECT";   break;
        case NET_STATE_MQTT_SUBSCRIBE:state_str = "MQTT_SUBSCRIBE"; break;
        case NET_STATE_RUNNING:       state_str = "RUNNING";       break;
        case NET_STATE_ERROR:         state_str = "ERROR";          break;
        default:                      state_str = "UNKNOWN";      break;
    }

    snprintf(pcWriteBuffer, xWriteBufferLen,
             "MQTT Status:\r\n"
             "  State: %s\r\n"
             "  Running: %s\r\n",
             state_str, running ? "YES" : "NO");

    return pdFALSE;
}

/* ============================
 * 网络状态命令: net
 * ============================ */
static BaseType_t prvNetCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString)
{
    (void)pcCommandString;

    wiz_NetInfo_t net_info;
    ctlnetwork(CN_GET_NETINFO, (void*)&net_info);

    snprintf(pcWriteBuffer, xWriteBufferLen,
             "Network Config:\r\n"
             "  MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n"
             "  IP:  %d.%d.%d.%d\r\n"
             "  SN:  %d.%d.%d.%d\r\n"
             "  GW:  %d.%d.%d.%d\r\n",
             net_info.mac[0], net_info.mac[1], net_info.mac[2],
             net_info.mac[3], net_info.mac[4], net_info.mac[5],
             net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3],
             net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3],
             net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);

    return pdFALSE;
}

/* ============================
 * 任务列表命令: task
 * ============================ */
static BaseType_t prvTaskCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString)
{
    (void)pcCommandString;

    TaskStatus_t *pxTaskStatusArray;
    UBaseType_t uxArraySize;
    uint32_t ulTotalRunTime;
    uint8_t i;

    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

    if (pxTaskStatusArray == NULL) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Failed to allocate memory\r\n");
        return pdFALSE;
    }

    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

    snprintf(pcWriteBuffer, xWriteBufferLen, "Task List:\r\n");
    char *pBuf = pcWriteBuffer + strlen(pcWriteBuffer);

    for (i = 0; i < uxArraySize; i++) {
        snprintf(pBuf, xWriteBufferLen - strlen(pcWriteBuffer),
                 "  %-16s P=%d S=%d\r\n",
                 pxTaskStatusArray[i].pcTaskName,
                 pxTaskStatusArray[i].uxCurrentPriority,
                 pxTaskStatusArray[i].eCurrentState);
        pBuf = pcWriteBuffer + strlen(pcWriteBuffer);
        if (strlen(pcWriteBuffer) >= xWriteBufferLen - 100) {
            break;
        }
    }

    vPortFree(pxTaskStatusArray);

    return pdFALSE;
}

/* ============================
 * 堆内存命令: heap
 * ============================ */
static BaseType_t prvHeapCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString)
{
    (void)pcCommandString;

    size_t free_heap = xPortGetFreeHeapSize();
    size_t min_free = xPortGetMinimumEverFreeHeapSize();

    snprintf(pcWriteBuffer, xWriteBufferLen,
             "Heap Info:\r\n"
             "  Free Heap:  %lu bytes\r\n"
             "  Min Ever:   %lu bytes\r\n"
             "  Total:      %lu bytes\r\n",
             (unsigned long)free_heap,
             (unsigned long)min_free,
             (unsigned long)configTOTAL_HEAP_SIZE);

    return pdFALSE;
}

/* ============================
 * 重启命令: reboot
 * ============================ */
static BaseType_t prvRebootCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString)
{
    (void)pcCommandString;

    snprintf(pcWriteBuffer, xWriteBufferLen, "Rebooting...\r\n");

    /* 延迟重启，确保输出发送完成 */
    HAL_Delay(100);
    NVIC_SystemReset();

    return pdFALSE;
}

/* ============================
 * 命令定义
 * ============================ */

/* mqtt 命令 */
static const CLI_Command_Definition_t xMqttCommand = {
    "mqtt",
    "\r\nmqtt:\r\n show MQTT status\r\n",
    prvMqttCommand,
    0
};

/* net 命令 */
static const CLI_Command_Definition_t xNetCommand = {
    "net",
    "\r\nnet:\r\n show network status\r\n",
    prvNetCommand,
    0
};

/* task 命令 */
static const CLI_Command_Definition_t xTaskCommand = {
    "task",
    "\r\ntask:\r\n show FreeRTOS task list\r\n",
    prvTaskCommand,
    0
};

/* heap 命令 */
static const CLI_Command_Definition_t xHeapCommand = {
    "heap",
    "\r\nheap:\r\n show heap memory info\r\n",
    prvHeapCommand,
    0
};

/* reboot 命令 */
static const CLI_Command_Definition_t xRebootCommand = {
    "reboot",
    "\r\nreboot:\r\n reboot system\r\n",
    prvRebootCommand,
    0
};

/* ============================
 * 注册所有命令
 * ============================ */
void CLI_RegisterCommands(void)
{
    FreeRTOS_CLIRegisterCommand(&xMqttCommand);
    FreeRTOS_CLIRegisterCommand(&xNetCommand);
    FreeRTOS_CLIRegisterCommand(&xTaskCommand);
    FreeRTOS_CLIRegisterCommand(&xHeapCommand);
    FreeRTOS_CLIRegisterCommand(&xRebootCommand);
}

