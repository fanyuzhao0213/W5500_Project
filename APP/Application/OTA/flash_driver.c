#include "flash_driver.h"
#include "mqtt_client.h"
#include <string.h>
#include "LOG.h"
// Flash句柄
static FLASH_EraseInitTypeDef erase_init;
static uint32_t sector_error;

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
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
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
    0xBAD03605, 0xCDD706B3, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

// 解锁Flash
flash_err_t flash_unlock(void)
{
    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (status == HAL_OK) {
        return FLASH_OK;
    } else if (status == HAL_ERROR) {
        return FLASH_ERROR;
    } else if (status == HAL_BUSY) {
        return FLASH_BUSY;
    } else {
        return FLASH_TIMEOUT;
    }
}

// 加锁Flash
flash_err_t flash_lock(void)
{
    HAL_StatusTypeDef status = HAL_FLASH_Lock();
    if (status == HAL_OK) {
        return FLASH_OK;
    }
    return FLASH_ERROR;
}

// 根据地址获取扇区号
static uint32_t get_sector(uint32_t addr)
{
    uint32_t sector;

    if (addr < 0x08010000) {
        sector = FLASH_SECTOR_0 + ((addr - 0x08000000) >> 14);
    } else if (addr < 0x08020000) {
        sector = FLASH_SECTOR_4;
    } else if (addr < 0x08040000) {
        sector = FLASH_SECTOR_5;
    } else if (addr < 0x08060000) {
        sector = FLASH_SECTOR_6;
    } else if (addr < 0x08080000) {
        sector = FLASH_SECTOR_7;
    } else if (addr < 0x080A0000) {
        sector = FLASH_SECTOR_8;
    } else if (addr < 0x080C0000) {
        sector = FLASH_SECTOR_9;
    } else if (addr < 0x080E0000) {
        sector = FLASH_SECTOR_10;
    } else {
        sector = FLASH_SECTOR_11;
    }

    return sector;
}

// 擦除单个扇区
flash_err_t flash_erase_sector(uint32_t sector)
{
    flash_err_t err = FLASH_OK;
    HAL_StatusTypeDef status;

    if (sector > FLASH_SECTOR_11) {
        return FLASH_INVALID_ADDR;
    }

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = sector;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

    if (status == HAL_OK) {
        err = FLASH_OK;
    } else if (status == HAL_ERROR) {
        err = FLASH_ERROR;
    } else if (status == HAL_BUSY) {
        err = FLASH_BUSY;
    } else if (status == HAL_TIMEOUT) {
        err = FLASH_TIMEOUT;
    }

    return err;
}

// 擦除指定范围 - 分扇区擦除，每擦除一个扇区后维护网络
flash_err_t flash_erase_range(uint32_t start_addr, uint32_t size)
{
    flash_err_t err = FLASH_OK;
    uint32_t start_sector, end_sector;
    uint32_t current_sector;

    if (!flash_is_valid_addr(start_addr) || !flash_is_valid_addr(start_addr + size - 1)) {
        return FLASH_INVALID_ADDR;
    }

    start_sector = get_sector(start_addr);
    end_sector = get_sector(start_addr + size - 1);

    // 逐个扇区擦除，每擦除一个扇区后调用网络维护
    for (current_sector = start_sector; current_sector <= end_sector; current_sector++) {
        erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase_init.Banks = FLASH_BANK_1;
        erase_init.Sector = current_sector;
        erase_init.NbSectors = 1;
        erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

        if (status != HAL_OK) {
            if (status == HAL_ERROR) {
                err = FLASH_ERROR;
            } else if (status == HAL_BUSY) {
                err = FLASH_BUSY;
            } else if (status == HAL_TIMEOUT) {
                err = FLASH_TIMEOUT;
            }
            return err;
        }

        // 每擦除一个扇区后，调用网络维护函数防止 MQTT 超时
        mqtt_client_loop_nonblocking(0);
    }

    return FLASH_OK;
}

// 写入32位字
flash_err_t flash_write_word(uint32_t addr, uint32_t data)
{
    if (!flash_is_valid_addr(addr)) {
        return FLASH_INVALID_ADDR;
    }

    HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);

    if (status == HAL_OK) {
        return FLASH_OK;
    } else if (status == HAL_ERROR) {
        return FLASH_ERROR;
    } else if (status == HAL_BUSY) {
        return FLASH_BUSY;
    } else {
        return FLASH_TIMEOUT;
    }
}

// 写入16位半字
flash_err_t flash_write_halfword(uint32_t addr, uint16_t data)
{
    if (!flash_is_valid_addr(addr)) {
        return FLASH_INVALID_ADDR;
    }

    HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, data);

    if (status == HAL_OK) {
        return FLASH_OK;
    } else if (status == HAL_ERROR) {
        return FLASH_ERROR;
    } else if (status == HAL_BUSY) {
        return FLASH_BUSY;
    } else {
        return FLASH_TIMEOUT;
    }
}

// 写入8位字节
flash_err_t flash_write_byte(uint32_t addr, uint8_t data)
{
    return flash_write_halfword(addr, (uint16_t)data);
}

// 写入任意长度数据
flash_err_t flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    flash_err_t err;
    uint32_t i = 0;
    uint32_t word_data;

    LOGI("flash_write: START - addr=0x%08lX, len=%lu", (unsigned long)addr, (unsigned long)len);

    if (!flash_is_valid_addr(addr) || !flash_is_valid_addr(addr + len - 1)) {
        LOGE("flash_write: Invalid address");
        return FLASH_INVALID_ADDR;
    }

    if (data == NULL || len == 0) {
        LOGE("flash_write: Invalid data or len");
        return FLASH_ERROR;
    }

    while (i < len) {
        if ((len - i) >= 4) {
            word_data = ((uint32_t)data[i + 3] << 24) |
                        ((uint32_t)data[i + 2] << 16) |
                        ((uint32_t)data[i + 1] << 8) |
                        ((uint32_t)data[i]);
            err = flash_write_word(addr + i, word_data);
            if (err != FLASH_OK) {
                LOGE("flash_write: word failed at 0x%08lX, i=%lu", (unsigned long)(addr + i), (unsigned long)i);
                return err;
            }
            i += 4;
        } else if ((len - i) >= 2) {
            word_data = ((uint16_t)data[i + 1] << 8) | ((uint16_t)data[i]);
            err = flash_write_halfword(addr + i, (uint16_t)word_data);
            if (err != FLASH_OK) {
                LOGE("flash_write: halfword failed at 0x%08lX, i=%lu", (unsigned long)(addr + i), (unsigned long)i);
                return err;
            }
            i += 2;
        } else {
            err = flash_write_byte(addr + i, data[i]);
            if (err != FLASH_OK) {
                LOGE("flash_write: byte failed at 0x%08lX, i=%lu", (unsigned long)(addr + i), (unsigned long)i);
                return err;
            }
            i++;
        }
    }

    LOGI("flash_write: END - SUCCESS");
    return FLASH_OK;
}

// 读取Flash数据
void flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    memcpy(data, (const void *)addr, len);
}

// 计算CRC32
uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;

    for (i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// 计算Flash区域的CRC32
uint32_t flash_calc_crc32(uint32_t addr, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;
    uint8_t byte;

    if (!flash_is_valid_addr(addr) || !flash_is_valid_addr(addr + size - 1)) {
        LOGE("flash_calc_crc32: Invalid address range");
        return 0;
    }

    for (i = 0; i < size; i++) {
        byte = *(volatile uint8_t *)(addr + i);
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// 检查Flash是否已擦除（全为0xFF）
int flash_is_erased(uint32_t addr, uint32_t size)
{
    uint32_t i;

    for (i = 0; i < size; i++) {
        if (*(volatile uint8_t *)(addr + i) != 0xFF) {
            return 0;
        }
    }

    return 1;
}

// 检查Flash地址是否有效
int flash_is_valid_addr(uint32_t addr)
{
    return (addr >= FLASH_BASE_ADDR) && (addr < FLASH_BASE_ADDR + FLASH_SIZE);
}

// 打印Flash区域数据（用于调试对比）
void flash_dump(uint32_t addr, uint32_t size)
{
    uint8_t buf[16];
    uint32_t i, j;

    LOGI("========== Flash Dump: 0x%08lX, %lu bytes ==========", (unsigned long)addr, (unsigned long)size);

    for (i = 0; i < size; i += 16) {
        for (j = 0; j < 16; j++) {
            buf[j] = *(volatile uint8_t *)(addr + i + j);
        }
        LOGI("%08lX: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
             (unsigned long)(addr + i),
             buf[0], buf[1], buf[2], buf[3],
             buf[4], buf[5], buf[6], buf[7],
             buf[8], buf[9], buf[10], buf[11],
             buf[12], buf[13], buf[14], buf[15]);
    }
    LOGI("===================================================");
}
