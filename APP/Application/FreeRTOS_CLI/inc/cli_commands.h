/**
 * @file cli_commands.h
 * @brief FreeRTOS CLI 命令定义
 */

#ifndef _CLI_COMMANDS_H_
#define _CLI_COMMANDS_H_

#include "FreeRTOS_CLI.h"

/* 注册所有CLI命令 */
void CLI_RegisterCommands(void);

/* 命令列表 */
BaseType_t prvMqttCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString);
BaseType_t prvNetCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString);
BaseType_t prvTaskCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString);
BaseType_t prvHeapCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString);
BaseType_t prvRebootCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString);

#endif /* _CLI_COMMANDS_H_ */


