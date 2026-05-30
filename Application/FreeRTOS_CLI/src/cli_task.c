/**
 * @file cli_task.c
 * @brief FreeRTOS CLI 任务实现
 */

#include "cli_task.h"
#include "cli_commands.h"
#include "FreeRTOS_CLI.h"
#include "cmsis_os.h"
#include "usart.h"
#include "LOG.h"
#include <string.h>

/* CLI 配置 */
#define CLI_INPUT_SIZE     128
#define CLI_OUTPUT_SIZE    512

static char cli_input[CLI_INPUT_SIZE];
static char cli_output[CLI_OUTPUT_SIZE];

/* CLI 任务句柄 */
osThreadId g_cli_task_handle;

/* 任务优先级和栈大小 */
#define CLI_TASK_PRIORITY    osPriorityLow
#define CLI_TASK_STACK       384

/**
 * @brief CLI 任务入口
 */
void StartCLITask(void const *argument)
{
    uint8_t ch;
    uint16_t idx = 0;

    (void)argument;

    /* 注册CLI命令 */
    CLI_RegisterCommands();

    LOGI("CLI: Ready");

    /* 清空输入缓冲区 */
    memset(cli_input, 0, sizeof(cli_input));

    while (1) {
        /* 等待串口接收字符 */
        if (HAL_UART_Receive(&huart4, &ch, 1, 10) == HAL_OK) {
            /* 回显字符 */
            HAL_UART_Transmit(&huart4, &ch, 1, 100);

            /* 检查换行符 */
            if (ch == '\r' || ch == '\n') {
                LOGI("CLI: command received - %s", cli_input);

                /* 处理命令 */
                cli_input[idx] = '\0';
                do {
                    memset(cli_output, 0, CLI_OUTPUT_SIZE);

                    BaseType_t more = FreeRTOS_CLIProcessCommand(
                        cli_input,
                        cli_output,
                        CLI_OUTPUT_SIZE
                    );

                    /* 发送输出到串口 */
                    HAL_UART_Transmit(&huart4, (uint8_t*)cli_output, strlen(cli_output), 100);

                    if (more == pdFALSE) {
                        break;
                    }

                } while (1);

                /* 重置索引 */
                idx = 0;
                memset(cli_input, 0, CLI_INPUT_SIZE);

                /* 打印提示符 */
                LOGI("CLI: > ");
            } else if (ch == '\t') {
                /* Tab键 - 列出所有命令 */
                cli_input[0] = 'h';
                cli_input[1] = 'e';
                cli_input[2] = 'l';
                cli_input[3] = 'p';
                cli_input[4] = '\0';
                idx = 4;

                /* 回显help */
                HAL_UART_Transmit(&huart4, (uint8_t*)"help\r\n", 6, 100);

                do {
                    memset(cli_output, 0, CLI_OUTPUT_SIZE);

                    BaseType_t more = FreeRTOS_CLIProcessCommand(
                        cli_input,
                        cli_output,
                        CLI_OUTPUT_SIZE
                    );

                    /* 发送输出到串口 */
                    HAL_UART_Transmit(&huart4, (uint8_t*)cli_output, strlen(cli_output), 100);

                    if (more == pdFALSE) {
                        break;
                    }

                } while (1);

                /* 重置索引 */
                idx = 0;
                memset(cli_input, 0, CLI_INPUT_SIZE);

                /* 打印提示符 */
                LOGI("CLI: > ");
            } else {
                /* 存储字符 */
                if (idx < CLI_INPUT_SIZE - 1) {
                    cli_input[idx++] = ch;
                }
            }
        }

        /* 让出CPU */
        osDelay(10);
    }
}

/**
 * @brief 创建CLI任务
 */
void cli_task_create(void)
{
    osThreadDef(cliTask, StartCLITask, CLI_TASK_PRIORITY, 0, CLI_TASK_STACK);
    g_cli_task_handle = osThreadCreate(osThread(cliTask), NULL);

    if (g_cli_task_handle == NULL) {
        LOGE("CLI: task create failed");
    } else {
        LOGI("CLI: task created");
    }
}

