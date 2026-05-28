/**
 * @file LOG.h
 * @brief 统一调试日志输出 - 支持RTT和UART4
 *
 * 使用方法:
 * 1. 在项目选项中定义 DEBUG_OUTPUT 选择输出方式
 *    - DEBUG_RTT   使用RTT输出 (默认)
 *    - DEBUG_UART4 使用UART4输出
 *    - 不定义      无输出
 *
 * 2. 使用日志宏:
 *    LOGI("Hello %s", "World");
 *    LOGW("Warning: %d", value);
 *    LOGE("Error: %s", error_str);
 */

#ifndef _LOG_H_
#define _LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

//=============================================================================
// 输出方式选择 (三选一)
//=============================================================================
// #define DEBUG_OUTPUT DEBUG_RTT    // 使用RTT输出
// #define DEBUG_OUTPUT DEBUG_UART4  // 使用UART4输出

#ifndef DEBUG_OUTPUT
#define DEBUG_OUTPUT DEBUG_RTT  // 默认使用RTT
//#define DEBUG_OUTPUT DEBUG_UART4  // 使用UART4输出
#endif

//=============================================================================
// RTT支持
//=============================================================================
#if DEBUG_OUTPUT == DEBUG_RTT
#include "SEGGER_RTT.h"
#endif

//=============================================================================
// UART4支持
//=============================================================================
#if DEBUG_OUTPUT == DEBUG_UART4
#include "usart.h"
extern UART_HandleTypeDef huart4;
#endif

//=============================================================================
// 日志输出函数声明
//=============================================================================
void LOG_Init(void);
void LOG_Write(uint8_t* pBuf, uint16_t len);
void LOG_WriteString(const char* str);

//=============================================================================
// 日志宏定义
//=============================================================================
#if DEBUG_OUTPUT == DEBUG_RTT

#define LOG_CLEAR()         SEGGER_RTT_WriteString(0, RTT_CTRL_CLEAR)
#define LOG_WRITE(p, len)   SEGGER_RTT_Write(0, p, len)
#define LOG_WRITE_STRING(p) SEGGER_RTT_WriteString(0, p)

#elif DEBUG_OUTPUT == DEBUG_UART4

#define LOG_CLEAR()         do{}while(0)
#define LOG_WRITE(p, len)   HAL_UART_Transmit(&huart4, p, len, 100)
#define LOG_WRITE_STRING(p) do { \
    uint16_t _len = strlen(p); \
    if (_len > 0) HAL_UART_Transmit(&huart4, (uint8_t*)p, _len, 100); \
} while(0)

#else

#define LOG_CLEAR()         do{}while(0)
#define LOG_WRITE(p, len)   do{}while(0)
#define LOG_WRITE_STRING(p) do{}while(0)

#endif

//=============================================================================
// 颜色定义
//=============================================================================
#if DEBUG_OUTPUT == DEBUG_RTT
#define _LOG_COLOR_RESET   RTT_CTRL_RESET
#define _LOG_COLOR_RED    RTT_CTRL_TEXT_BRIGHT_RED
#define _LOG_COLOR_GREEN  RTT_CTRL_TEXT_BRIGHT_GREEN
#define _LOG_COLOR_YELLOW RTT_CTRL_TEXT_BRIGHT_YELLOW
#define _LOG_COLOR_CYAN   RTT_CTRL_TEXT_BRIGHT_CYAN
#else
#define _LOG_COLOR_RESET   ""
#define _LOG_COLOR_RED     ""
#define _LOG_COLOR_GREEN   ""
#define _LOG_COLOR_YELLOW  ""
#define _LOG_COLOR_CYAN    ""
#endif

//=============================================================================
// 日志宏 (核心)
//=============================================================================
#if DEBUG_OUTPUT != -1

#define _LOG_PRINT(type, color, format, ...) \
    do { \
        char _buf[256]; \
        int _len = snprintf(_buf, sizeof(_buf), "%s%s " format "\r\n", color, type, ##__VA_ARGS__); \
        if (_len > 0 && _len < (int)sizeof(_buf)) { \
            LOG_WRITE((uint8_t*)_buf, _len); \
        } \
    } while(0)

/*** 带颜色的日志 ***/
#define LOGI(format, ...) _LOG_PRINT("I:", _LOG_COLOR_GREEN, format, ##__VA_ARGS__)
#define LOGW(format, ...) _LOG_PRINT("W:", _LOG_COLOR_YELLOW, format, ##__VA_ARGS__)
#define LOGE(format, ...) _LOG_PRINT("E:", _LOG_COLOR_RED, format, ##__VA_ARGS__)

/*** 无颜色日志 ***/
#define LOG(format, ...) _LOG_PRINT("", "", format, ##__VA_ARGS__)

#else

#define LOG_CLEAR()         do{}while(0)
#define LOG_WRITE(p, len)   do{}while(0)
#define LOG_WRITE_STRING(p) do{}while(0)
#define LOG(format, ...)     do{}while(0)
#define LOGI(format, ...)   do{}while(0)
#define LOGW(format, ...)   do{}while(0)
#define LOGE(format, ...)   do{}while(0)

#endif

#ifdef __cplusplus
}
#endif

#endif /* _LOG_H_ */
