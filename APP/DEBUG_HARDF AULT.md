# HardFault 调试指南

## 问题现象
APP 启动后直接 HardFault

## 调试步骤

### 1. 使用调试器单步调试

#### 在 Keil MDK 中：
1. 打开 APP 工程
2. 设置调试选项：Debug → Settings → Flash Download
   - 起始地址: 0x0800C000
   - 大小: 0x00078000
3. 启动调试 (Ctrl+F5)
4. 在以下位置设置断点：
   - `Reset_Handler` (startup_stm32f405xx.s)
   - `SystemInit` (system_stm32f4xx.c)
   - `main` (main.c)

### 2. 检查关键寄存器

#### 在调试器中查看：
```
View → Watch Window → Watch 1

添加以下变量：
- SCB->VTOR
- __get_MSP()
- __get_PSP()
- __get_CONTROL()
```

### 3. 验证 VTOR 设置

#### 在 SystemInit 中设置断点：
```c
void SystemInit(void)
{
    // ... 其他代码 ...
    
    #if defined(USER_VECT_TAB_ADDRESS)
    SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET; // ← 在这里设置断点
    #endif
}
```

#### 检查 VTOR 值：
- 应该是: `0x0800C000`
- 如果不是，说明 VTOR 设置有问题

### 4. 检查栈指针

#### 在 Reset_Handler 中设置断点：
```assembly
Reset_Handler PROC
    EXPORT  Reset_Handler   [WEAK]
    IMPORT  SystemInit
    IMPORT  __main
    LDR     R0, =SystemInit
    BLX     R0               ; ← 在这里设置断点
    LDR     R0, =__main
    BX      R0
    ENDP
```

#### 检查栈指针：
- MSP 应该是: `0x2001XXXX` (接近 RAM 顶部)
- 如果不是，说明栈指针有问题

### 5. 检查中断向量表

#### 使用 J-Link Commander：
```
J-Link> mem32 0x0800C000 16
```

#### 预期输出：
```
0x0800C000: 2001XXXX  ; 栈指针
0x0800C004: 0800XXXX  ; Reset_Handler
0x0800C008: 0800XXXX  ; NMI_Handler
0x0800C00C: 0800XXXX  ; HardFault_Handler
...
```

### 6. 检查 HardFault 原因

#### 在调试器中查看：
```
View → Watch Window → Watch 1

添加以下变量：
- SCB->HFSR  (HardFault Status Register)
- SCB->CFSR  (Configurable Fault Status Register)
- SCB->MMFAR (MemManage Fault Address Register)
- SCB->BFAR  (BusFault Address Register)
```

#### HFSR 值分析：
- `0x00000002` - Vector Table HardFault (VTOR 问题)
- `0x40000000` - Forced HardFault (其他错误升级)
- `0x00000000` - 无 HardFault

#### CFSR 值分析：
- `0x00020000` - INVSTATE (无效状态)
- `0x00010000` - INVPC (无效 PC)
- `0x00000200` - DIVBYZERO (除零)
- `0x00000100` - UNALIGNED (未对齐访问)

### 7. 常见问题排查

#### 问题 1: VTOR 设置错误
**症状**: HFSR = 0x00000002
**原因**: VTOR 没有正确设置
**解决**: 检查 `system_stm32f4xx.c` 中的 VTOR 设置

#### 问题 2: 栈溢出
**症状**: CFSR = 0x00000200 或更高
**原因**: 栈太小
**解决**: 增加 `startup_stm32f405xx.s` 中的 `Stack_Size`

#### 问题 3: 中断向量表错误
**症状**: HFSR = 0x00000002
**原因**: 中断向量表没有正确链接
**解决**: 检查 `.sct` 文件中的 `RESET` 段位置

#### 问题 4: PendSV/SysTick 中断
**症状**: 在 FreeRTOS 初始化时 HardFault
**原因**: Bootloader 没有清除中断
**解决**: 在 Bootloader 跳转前清除所有中断

### 8. 最小化测试

#### 创建最小化 main.c：
```c
int main(void)
{
    // 1. 设置 VTOR
    SCB->VTOR = 0x0800C000;
    
    // 2. 简单的 LED 闪烁
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    while (1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);
        for (volatile uint32_t i = 0; i < 1000000; i++);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
        for (volatile uint32_t i = 0; i < 1000000; i++);
    }
}
```

#### 如果最小化代码能运行：
- 问题在 FreeRTOS 初始化
- 检查 `MX_FREERTOS_Init()` 和 `osKernelStart()`

#### 如果最小化代码不能运行：
- 问题在 VTOR 或栈指针
- 检查 Bootloader 跳转代码

### 9. 使用 J-Link RTT

#### 在 Bootloader 和 APP 中都使用 RTT：
```c
#include "SEGGER_RTT.h"

// 在关键位置添加日志
SEGGER_RTT_WriteString(0, "SystemInit start\n");
SEGGER_RTT_WriteString(0, "SystemInit end\n");
SEGGER_RTT_WriteString(0, "main start\n");
```

### 10. 检查链接脚本

#### 验证 .sct 文件：
```
LR_IROM1 0x0800C000 0x00078000  {
  ER_IROM1 0x0800C000 0x00078000  {
   *.o (RESET, +First)    ; ← 确保向量表在最前面
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  RW_IRAM1 0x20000000 0x0001C000  {
   .ANY (+RW +ZI)
  }
}
```

## 预期的调试输出

### 正常启动流程：
```
[BOOT] Stack Pointer: 0x2001XXXX
[BOOT] Reset Handler: 0x0800XXXX
[BOOT] Preparing to jump...
SystemInit start
SystemInit end
main start
LED blinking...
```

### HardFault 发生时：
```
[BOOT] Stack Pointer: 0x2001XXXX
[BOOT] Reset Handler: 0x0800XXXX
[BOOT] Preparing to jump...
SystemInit start
<HardFault>  ← 在这里停止
```

## 下一步

请告诉我：
1. HardFault 发生在哪个阶段？（SystemInit / main / FreeRTOS 初始化）
2. HFSR 和 CFSR 的值是多少？
3. VTOR 的值是多少？
4. 栈指针的值是多少？

这样我可以帮你精确定位问题！
