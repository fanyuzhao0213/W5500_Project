#include "bootloader_utils.h"
#include "main.h"
#include "usart.h"

// Flash解锁/加锁
static void flash_unlock(void)
{
    HAL_FLASH_Unlock();
}

static void flash_lock(void)
{
    HAL_FLASH_Lock();
}

// 写入OTA参数到Flash
int bootloader_write_params(ota_params_t *params)
{
    if (params == NULL) {
        return -1;
    }

    // 确保魔数正确
    params->magic_number = OTA_PARAMS_MAGIC;

    // 解锁Flash
    flash_unlock();

    // 注意：0x080F8000 和 0x080FC000 都在扇区 11 (0x080E0000-0x080FFFFF)
    // 同一扇区不能重复擦除，所以一次性写入两个区域

    // 擦除扇区 11
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = FLASH_SECTOR_11;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASHEx_Erase(&erase_init, &sector_error);

    // 写入备份区
    uint32_t *src = (uint32_t *)params;
    uint32_t dst = OTA_PARAMS_BACKUP_ADDR;
    for (uint32_t i = 0; i < sizeof(ota_params_t) / 4; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst, src[i]);
        dst += 4;
    }

    // 写入主区（同一扇区，无需再擦除）
    src = (uint32_t *)params;
    dst = OTA_PARAMS_ADDR;
    for (uint32_t i = 0; i < sizeof(ota_params_t) / 4; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst, src[i]);
        dst += 4;
    }

    // 加锁Flash
    flash_lock();

    return 0;
}

// CRC32表
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xC8D75010, 0xBFD06086,
    0x5768B525, 0x206F85B3, 0xC1611409, 0xB666249F,
    0x5EDEF90E, 0x29D9C998, 0xC0BA9822, 0xB7BDA8B4,
    0x59B33D17, 0x2EB40D81, 0xC7D95C3B, 0xB0DE6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

// 计算CRC32
uint32_t bootloader_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;

    for (i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// 计算Flash区域CRC32
uint32_t bootloader_crc32_flash(uint32_t addr, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;
    uint8_t byte;

    for (i = 0; i < size; i++) {
        byte = *(volatile uint8_t *)(addr + i);
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// 读取OTA参数
int bootloader_read_params(ota_params_t *params)
{
    if (params == NULL) {
        return -1;
    }

    // 先读取主参数区
    memcpy(params, (void *)OTA_PARAMS_ADDR, sizeof(ota_params_t));

    // 验证魔数
    if (params->magic_number == OTA_PARAMS_MAGIC) {
        return 0;
    }

    // 主参数区无效，尝试备份区
    memcpy(params, (void *)OTA_PARAMS_BACKUP_ADDR, sizeof(ota_params_t));

    if (params->magic_number == OTA_PARAMS_MAGIC) {
        return 0;
    }

    return -1;
}

// 检查是否为有效的ARM应用（检查栈指针和复位向量）
int bootloader_validate_firmware(uint32_t app_addr, firmware_header_t *header)
{
    uint32_t stack_addr = *(volatile uint32_t *)app_addr;
    uint32_t reset_addr = *(volatile uint32_t *)(app_addr + 4);

    // 检查栈指针是否在RAM范围内
    if (stack_addr < RAM_BASE_ADDR || stack_addr >= RAM_END_ADDR) {
        PRINTF("[BOOT] Invalid stack pointer: 0x%08lX\r\n", stack_addr);
        return -1;
    }

    // 检查复位向量是否在Flash范围内（App-A或App-B区域）
    if ((reset_addr < APP_A_ADDR || reset_addr >= APP_A_ADDR + APP_A_SIZE) &&
        (reset_addr < APP_B_ADDR || reset_addr >= APP_B_ADDR + APP_B_SIZE)) {
        PRINTF("[BOOT] Invalid reset vector: 0x%08lX\r\n", reset_addr);
        return -2;
    }

    // 填充header信息（用于后续打印）
    if (header != NULL) {
        header->magic = 0;
        header->version = 0;
        header->size = 0;
        header->crc32 = 0;
    }

    PRINTF("[BOOT] Valid ARM application found\r\n");
    return 0;
}

// 跳转到应用
void bootloader_jump_to_app(uint32_t app_addr)
{
    typedef void (*app_entry_t)(void);

    uint32_t stack_addr = *(volatile uint32_t *)app_addr;
    app_entry_t reset_handler = (app_entry_t)(*(volatile uint32_t *)(app_addr + 4));

    PRINTF("[BOOT] Stack Pointer: 0x%08lX\r\n", stack_addr);
    PRINTF("[BOOT] Reset Handler: 0x%08lX\r\n", (uint32_t)reset_handler);
    PRINTF("[BOOT] Preparing to jump...\r\n");

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    HAL_RCC_DeInit();
    HAL_DeInit();

    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
    }
    for (int i = 0; i < 8; i++) {
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    __set_MSP(stack_addr);
    __set_CONTROL(0);
    __ISB();
    __DSB();

    reset_handler();
}

// 延时函数
void bootloader_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
