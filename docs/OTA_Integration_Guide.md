# OTA模块 - 集成说明

## 模块文件列表

在 `Application/OTA/` 目录下已创建以下文件：

| 文件名 | 描述 |
|--------|------|
| `ota_config.h` | OTA配置文件，Flash分区地址定义 |
| `flash_driver.h` | Flash驱动头文件，API定义 |
| `flash_driver.c` | Flash驱动实现，擦除/读写/CRC32 |
| `ota_params.h` | OTA参数区头文件，数据结构定义 |
| `ota_params.c` | OTA参数区实现，读写/备份机制 |
| `ota.h` | OTA模块统一头文件 |
| `ota_example.c` | OTA模块使用示例（可选，测试用） |

## 集成到Keil项目步骤

### 1. 添加文件到Keil工程

1. 打开Keil工程
2. 在工程管理窗口中，找到 `Application` 分组
3. 右键点击，选择 "Add Group"，新建一个名为 `OTA` 的分组
4. 右键点击 `OTA` 分组，选择 "Add Existing Files to Group 'OTA'"
5. 添加以下源文件：
   - `Application/OTA/flash_driver.c`
   - `Application/OTA/ota_params.c`
   - `Application/OTA/ota_example.c` (可选，仅用于测试)

### 2. 添加头文件路径

1. 点击菜单 `Project -> Options for Target '...'` (或按 Alt+F7)
2. 切换到 `C/C++` 标签页
3. 在 `Include Paths` 中添加路径：
   ```
   ..\Application\OTA
   ```
4. 点击 "OK" 保存

### 3. 修改链接脚本（重要！）

**警告：这一步非常重要，必须修改链接脚本，否则会覆盖Bootloader或参数区！**

当前项目使用的Flash起始地址是 `0x08000000`，需要修改为 `0x0800C000` (App-A起始地址)。

#### 修改方法1 - 直接编辑sct文件

1. 打开 `MDK-ARM/w5500_project/w5500_project.sct`
2. 修改如下：

**原内容：**
```
LR_IROM1 0x08000000 0x00100000  {    ; load region size_region
  ER_IROM1 0x08000000 0x00100000  {  ; load address = execution address
```

**修改为：**
```
LR_IROM1 0x0800C000 0x00078000  {    ; load region size_region (App-A: 0x0800C000 ~ 0x08083FFF, 480KB)
  ER_IROM1 0x0800C000 0x00078000  {  ; load address = execution address
```

**解释：**
- 起始地址：`0x0800C000` (跳过前48KB的Bootloader区域)
- 大小：`0x00078000` (480KB，App-A的大小)

#### 修改方法2 - 通过Keil配置

1. 打开 `Project -> Options for Target '...'`
2. 切换到 `Target` 标签页
3. 在 `IROM1` 区域修改：
   - `Start`: `0x0800C000`
   - `Size`: `0x00078000`
4. 点击 "OK" 保存

### 4. 在main.c中集成OTA模块

在 `main.c` 中添加OTA初始化：

```c
/* USER CODE BEGIN Includes */
#include "ota.h"  // 包含OTA模块头文件
/* USER CODE END Includes */

/* USER CODE BEGIN 2 */
// 在main函数开始处，初始化OTA模块
ota_params_init();
/* USER CODE END 2 */
```

### 5. 编译测试

1. 点击 `Project -> Build Target` (或按 F7)
2. 检查是否有编译错误
3. 如果有错误，检查头文件路径是否正确，sct文件是否正确修改

## Flash分区总览

```
0x08000000 ┌─────────────────────────┐
           │    Bootloader (48KB)   │ 预留，后续实现
0x0800C000 ├─────────────────────────┤
           │     App-A (480KB)      │ 当前运行的固件
0x08084000 ├─────────────────────────┤
           │     App-B (480KB)      │ 备份/升级固件
0x080FC000 ├─────────────────────────┤
           │   OTA Params (16KB)    │ 主参数区
0x08100000 └─────────────────────────┘ (Flash结束)
```

## API使用示例

### 1. Flash驱动基本操作

```c
#include "flash_driver.h"

void flash_test(void)
{
    flash_err_t err;
    uint8_t data[] = "Test data";

    // 解锁Flash
    err = flash_unlock();

    // 擦除扇区
    err = flash_erase_range(APP_B_ADDR, 4096);

    // 写入数据
    err = flash_write(APP_B_ADDR, data, sizeof(data));

    // 读取数据
    uint8_t read_buf[32];
    flash_read(APP_B_ADDR, read_buf, sizeof(read_buf));

    // 计算CRC
    uint32_t crc = flash_calc_crc32(APP_B_ADDR, sizeof(data));

    // 加锁Flash
    flash_lock();
}
```

### 2. OTA参数操作

```c
#include "ota_params.h"

void params_test(void)
{
    ota_params_err_t err;

    // 初始化
    err = ota_params_init();

    // 设置OTA标志
    err = ota_params_set_flag(OTA_FLAG_PENDING);

    // 设置激活App
    err = ota_params_set_active_app(APP_B);

    // 递增启动计数
    err = ota_params_inc_boot_count();

    // 更新App-B信息
    err = ota_params_update_app_b(
        20240530,      // 版本
        245760,        // 大小
        0x12345678,    // CRC32
        1              // 有效
    );

    // 读取参数
    uint32_t flag = ota_params_get_flag();
    uint32_t app = ota_params_get_active_app();
}
```

### 3. 完整升级准备流程

```c
void prepare_upgrade(void)
{
    // 1. 下载固件到App-B
    // download_firmware_to_app_b();

    // 2. 校验固件CRC
    uint32_t crc = flash_calc_crc32(APP_B_ADDR, firmware_size);
    if (crc != expected_crc) {
        return;  // CRC错误，中止
    }

    // 3. 更新App-B信息
    ota_params_update_app_b(
        new_version,
        firmware_size,
        crc,
        1
    );

    // 4. 设置OTA标志
    ota_params_set_flag(OTA_FLAG_PENDING);

    // 5. 设置激活App
    ota_params_set_active_app(APP_B);

    // 6. 重置启动计数
    ota_params_reset_boot_count();

    // 7. 重启
    NVIC_SystemReset();
}
```

## 注意事项

1. **Flash擦除操作**：Flash擦除是按扇区操作的，且擦除后数据会变为0xFF
2. **Flash写入操作**：只能将1变为0，不能将0变为1，必须先擦除才能再次写入
3. **参数区备份机制**：写入参数时先写备份区，再写主区，避免掉电损坏
4. **链接脚本必须修改**：如果不改，固件会覆盖Bootloader和参数区
5. **中断保护**：Flash写入/擦除时需要禁用中断吗？目前代码没禁用，如果需要可以添加

## 下一步

现在第1阶段完成了！接下来是：

**第2阶段：Bootloader实现**

需要创建独立的Bootloader工程，包含：
- 启动时检查OTA标志
- 验证固件（CRC/魔数）
- 跳转逻辑
- 回滚机制

需要我继续实现Bootloader吗？
