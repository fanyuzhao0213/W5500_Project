#ifndef __OTA_H
#define __OTA_H

// OTA模块统一头文件
// 包含所有OTA相关的头文件

#include "ota_config.h"
#include "flash_driver.h"
#include "ota_params.h"

#ifdef __cplusplus
extern "C" {
#endif

// OTA模块初始化
void ota_init(void);

#ifdef __cplusplus
}
#endif

#endif
