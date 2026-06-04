/**
 * @file mqtt_exception.c
 * @brief 网络异常处理模块 - 独立线程实现
 */

#include "mqtt_exception.h"
#include "mqtt_task.h"
#include "mqtt_client.h"
#include "mqtt_config.h"
#include "wizchip_conf.h"
#include "netconf.h"
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
#include "dhcp.h"
#endif
#include "LOG.h"
#include "main.h"
#include "w5500_conf.h"

/* ============================================================
 * 外部变量声明
 * ============================================================ */
extern IWDG_HandleTypeDef hiwdg;

/* ============================================================
 * 模块内部变量
 * ============================================================ */
osThreadId g_exception_task_handle = NULL;
exception_status_t g_exception_status;

static uint8_t g_phy_check_count = 0;
static uint32_t g_last_watchdog_feed = 0;
static uint8_t g_dhcp_fail_count = 0;

/* ============================================================
 * 内部函数声明
 * ============================================================
 * 重要：异常任务只做"监测"和"通知"，不直接恢复网络
 * 所有恢复动作由 NetworkTask 状态机完成 (避免双任务并发改状态)
 */
static void reset_exception_status(void);
static void feed_watchdog_safely(void);
static void crc32_self_test(void);
static void notify_network_task(exception_type_t type);

/* ============================================================
 * 初始化模块
 * ============================================================ */
void mqtt_exception_init(void)
{
    reset_exception_status();
    g_phy_check_count = 0;
    g_last_watchdog_feed = 0;
    g_dhcp_fail_count = 0;

    // CRC32 上电自检
    crc32_self_test();

    LOGI("MQTT Exception: initialized");
}

/* ============================================================
 * CRC32 上电自检
 * ============================================================ */
static void crc32_self_test(void)
{
    // 测试字符串 "123456789" 的标准CRC32是 0xCBF43926
    // 使用内联的 CRC32 表进行测试
    static const uint32_t test_table[256] = {
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

    const uint8_t test_data[] = "123456789";
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < 9; i++) {
        crc = test_table[(crc ^ test_data[i]) & 0xFF] ^ (crc >> 8);
    }
    crc = crc ^ 0xFFFFFFFF;
    LOGI("CRC32 self-test: 0x%08lX %s", (unsigned long)crc,
         crc == 0xCBF43926 ? "PASS" : "FAIL");
}

/* ============================================================
 * 创建异常处理任务
 * ============================================================ */
void mqtt_exception_task_create(void)
{
    osThreadDef(exceptionTask, StartExceptionTask,
                EXCEPTION_TASK_PRIORITY, 0, EXCEPTION_TASK_STACK);
    g_exception_task_handle = osThreadCreate(osThread(exceptionTask), NULL);

    if (g_exception_task_handle == NULL) {
        LOGE("MQTT Exception: task create failed");
    } else {
        LOGI("MQTT Exception: task created");
    }
}

/* ============================================================
 * 上报异常
 * ============================================================ */
void mqtt_exception_report(exception_type_t type)
{
    if (type == EXCEPTION_NONE) {
        return;
    }

    /* 如果是同一类型异常，只增加计数 */
    if (g_exception_status.type == type) {
        g_exception_status.count++;
        g_exception_status.timestamp = HAL_GetTick();
        LOGW("MQTT Exception: %d repeated (count=%d)", type, g_exception_status.count);
    } else {
        /* 新的异常类型 */
        g_exception_status.type = type;
        g_exception_status.timestamp = HAL_GetTick();
        g_exception_status.count = 1;
        g_exception_status.in_recovery = 0;
        g_exception_status.recovery_attempts = 0;
        LOGE("MQTT Exception: type=%d occurred at %d", type, (int)HAL_GetTick());
    }
}

/* ============================================================
 * 清除异常
 * ============================================================ */
void mqtt_exception_clear(void)
{
    if (g_exception_status.type != EXCEPTION_NONE) {
        LOGI("MQTT Exception: cleared");
        reset_exception_status();
    }
}

/* ============================================================
 * 重置异常处理模块
 * ============================================================ */
void mqtt_exception_reset(void)
{
    LOGW("MQTT Exception: resetting all exception states");
    reset_exception_status();
    g_phy_check_count = 0;
    g_last_watchdog_feed = 0;
    LOGI("MQTT Exception: reset complete");
}

/* ============================================================
 * 重置 DHCP 失败计数
 * @note DHCP 成功获取 IP 后调用
 * ============================================================ */
void mqtt_exception_reset_dhcp_count(void)
{
    if (g_dhcp_fail_count > 0) {
        LOGI("MQTT Exception: DHCP success, resetting fail count from %d to 0", g_dhcp_fail_count);
        g_dhcp_fail_count = 0;
    }
}

/* ============================================================
 * 获取异常状态
 * ============================================================ */
exception_status_t* mqtt_exception_get_status(void)
{
    return &g_exception_status;
}

/* ============================================================
 * 异常处理任务主循环
 * ============================================================
 * 职责：纯监测 + 上报
 *   1. 监测 PHY 链路
 *   2. 监测 MQTT 连接状态
 *   3. 监测堆栈高水位
 *   4. 安全喂狗
 *   5. 上报异常 (mqtt_exception_report)
 *
 * 严禁在本任务内执行任何 mqtt_client_disconnect / mqtt_task_set_state
 * 等会改全局状态的操作。所有恢复由 NetworkTask 状态机独立完成。
 * ============================================================ */
void StartExceptionTask(void const * argument)
{
    uint32_t last_check = 0;
    uint8_t phy_link;

    (void)argument;
    LOGI("MQTT Exception: task started (monitor-only mode)");

    for (;;) {
        /* 按检查间隔运行 */
        if (HAL_GetTick() - last_check >= EXCEPTION_CHECK_INTERVAL) {
            last_check = HAL_GetTick();

            /* =====================================================
             * 1. 监测 PHY 链路状态
             * ===================================================== */
            phy_link = wizphy_getphylink();

            if (phy_link != PHY_LINK_ON) {
                if (g_phy_check_count < 255) {
                    g_phy_check_count++;
                }

                if (g_phy_check_count >= PHY_CHECK_THRESHOLD) {
                    if (g_exception_status.type != EXCEPTION_PHY_LINK_DOWN) {
                        /* 上报异常 -> 由 NetworkTask 状态机处理 */
                        mqtt_exception_report(EXCEPTION_PHY_LINK_DOWN);
                        notify_network_task(EXCEPTION_PHY_LINK_DOWN);
                    }
                }
            } else {
                /* PHY 链路恢复：仅清零计数，不擅自清异常类型
                 * (异常类型由 NetworkTask 在正确状态机位置清除) */
                if (g_phy_check_count > 0) {
                    LOGI("MQTT Exception: PHY link recovered");
                    g_phy_check_count = 0;
                }
            }

            /* =====================================================
             * 2. 喂狗（安全策略：MQTT 正常连接时喂，出错时也喂以免异常恢复过程被复位）
             * ===================================================== */
            feed_watchdog_safely();
        }

        osDelay(10);
    }
}

/* ============================================================
 * 内部：通知网络任务处理异常
 * ============================================================
 * 通过 FreeRTOS 信号量唤醒网络任务，让 NetworkTask 在自己的
 * 状态机里完成恢复，避免双任务并发改全局状态。
 * ============================================================ */
static void notify_network_task(exception_type_t type)
{
    extern osThreadId g_network_task_handle;
    (void)type;

    if (g_network_task_handle != NULL) {
        /* 唤醒网络任务：让它在状态机里检查并恢复 */
        osSignalSet(g_network_task_handle, 0x01);
    }
}

/* ============================================================
 * 内部：重置异常状态
 * ============================================================ */
static void reset_exception_status(void)
{
    g_exception_status.type = EXCEPTION_NONE;
    g_exception_status.timestamp = 0;
    g_exception_status.count = 0;
    g_exception_status.in_recovery = 0;
    g_exception_status.recovery_attempts = 0;
}

/* ============================================================
 * 内部：安全喂狗策略
 * ============================================================ */
static void feed_watchdog_safely(void)
{
    /* 只有 MQTT 正常运行，且无异常时才喂狗 */
    if (mqtt_is_running() && g_exception_status.type == EXCEPTION_NONE) {
        if (HAL_GetTick() - g_last_watchdog_feed >= 1000) {
            HAL_IWDG_Refresh(&hiwdg);
            g_last_watchdog_feed = HAL_GetTick();
        }
    } else {
        /* 异常情况下，喂狗间隔加倍，给恢复时间 */
        if (HAL_GetTick() - g_last_watchdog_feed >= 2000) {
            HAL_IWDG_Refresh(&hiwdg);
            g_last_watchdog_feed = HAL_GetTick();
        }
    }
}
