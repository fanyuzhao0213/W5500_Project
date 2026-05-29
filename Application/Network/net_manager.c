/**
 * @file net_manager.c
 * @brief NetManagerTask实现 - 负责PHY检测、DHCP维护、IP状态
 *
 * 功能:
 * - W5500初始化
 * - PHY Link检测
 * - DHCP维护 (DHCP模式)
 * - IP状态维护
 */

#include "net_manager.h"
#include "wizchip_conf.h"
#include "w5500_conf.h"
#include "socket.h"
#include "dhcp.h"
#include "network_types.h"
#include "LOG.h"
#include "cmsis_os.h"
#include "main.h"
#include <string.h>

/*============================================================================
 * 常量定义
 *============================================================================*/
/** W5500缓冲区配置 */
static uint8_t g_snum[] = {16, 16, 16, 16, 16, 16, 16, 16};

/** DHCP请求间隔 (毫秒) */
#define DHCP_RUN_INTERVAL_MS    100

/* DHCP缓冲区 */
#define DHCP_BUF_SIZE          512
static uint8_t g_dhcp_buf[DHCP_BUF_SIZE];

/*============================================================================
 * 静态变量
 *============================================================================*/
/** 当前网络状态 */
static net_state_t g_net_state = NET_DOWN;

/** W5500初始化标志 */
static uint8_t g_w5500_initialized = 0;

/** 上次Link检测状态 */
static uint8_t g_last_phy_link = 0;

/** 需要重新获取IP标志 */
static uint8_t g_request_ip_renew = 0;

/** DHCP运行时间 (毫秒) */
static uint32_t g_net_uptime_ms = 0;

/** 网络恢复时间戳 */
static uint32_t g_net_recovery_tick = 0;

/** IP配置信息 */
static wiz_NetInfo g_net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
    .ip  = {192, 168, 1, 88},
    .sn  = {255, 255, 255, 0},
    .gw  = {192, 168, 1, 1},
    .dns = {8, 8, 8, 8}
};

/*============================================================================
 * 静态函数声明
 *============================================================================*/
static int  w5500_hw_init(void);
static void dhcp_callback_ip_assign(void);
static void dhcp_callback_ip_update(void);
static void dhcp_callback_ip_conflict(void);
static void set_static_ip(void);

/*============================================================================
 * DHCP回调函数
 *============================================================================*/
/**
 * @brief DHCP成功获取IP回调
 */
static void dhcp_callback_ip_assign(void)
{
    LOGI("DHCP: IP assigned");
    g_net_state = NET_IP_OK;
    g_net_uptime_ms = 0;
    g_net_recovery_tick = HAL_GetTick();
}

/**
 * @brief DHCP IP更新回调
 */
static void dhcp_callback_ip_update(void)
{
    LOGI("DHCP: IP updated");
    g_net_state = NET_IP_OK;
    g_net_uptime_ms = 0;
    g_net_recovery_tick = HAL_GetTick();
}

/**
 * @brief DHCP IP冲突回调
 */
static void dhcp_callback_ip_conflict(void)
{
    LOGE("DHCP: IP conflict!");
    g_net_state = NET_DOWN;
}

/*============================================================================
 * W5500硬件初始化
 *============================================================================*/
/**
 * @brief W5500硬件初始化
 * @return 0成功, -1失败
 */
static int w5500_hw_init(void)
{
    uint8_t version;

    /* 注册 SPI 回调函数 */
    wizchip_spi_cbfunc();

    /* 初始化 W5500 socket 缓冲区和寄存器 */
    if (wizchip_init(g_snum, g_snum) < 0) {
        LOGE("W5500: init failed");
        return -1;
    }

    /* 等待 W5500 就绪 */
    osDelay(100);

    /* 验证 VERSIONR */
    version = getVERSIONR();
    LOGI("W5500: VERSIONR = 0x%02X", version);

    if (version != 0x04) {
        LOGE("W5500: ERROR! Expected 0x04, got 0x%02X", version);
        return -1;
    }

    LOGI("W5500: OK!");
    return 0;
}

/*============================================================================
 * 设置静态IP
 *============================================================================*/
/**
 * @brief 设置静态IP配置
 * @note 静态IP模式时调用,DHCP模式不使用
 */
static void set_static_ip(void)
{
    wiz_NetInfo netinfo;

    /* 使用memcpy复制数组 */
    memcpy(netinfo.mac, g_net_info.mac, 6);
    memcpy(netinfo.ip, g_net_info.ip, 4);
    memcpy(netinfo.sn, g_net_info.sn, 4);
    memcpy(netinfo.gw, g_net_info.gw, 4);
    memcpy(netinfo.dns, g_net_info.dns, 4);
    netinfo.dhcp = NETINFO_STATIC;

    ctlnetwork(CN_SET_NETINFO, (void*)&netinfo);

    /* 验证配置 */
    ctlnetwork(CN_GET_NETINFO, (void*)&netinfo);

    LOGI("Static IP Config:");
    LOGI("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
          netinfo.mac[0], netinfo.mac[1], netinfo.mac[2],
          netinfo.mac[3], netinfo.mac[4], netinfo.mac[5]);
    LOGI("  IP:  %d.%d.%d.%d", netinfo.ip[0], netinfo.ip[1], netinfo.ip[2], netinfo.ip[3]);
    LOGI("  SN:  %d.%d.%d.%d", netinfo.sn[0], netinfo.sn[1], netinfo.sn[2], netinfo.sn[3]);
    LOGI("  GW:  %d.%d.%d.%d", netinfo.gw[0], netinfo.gw[1], netinfo.gw[2], netinfo.gw[3]);
}

/*============================================================================
 * 公共函数
 *============================================================================*/
/**
 * @brief 初始化网络管理器
 */
void net_manager_init(void)
{
    g_net_state = NET_DOWN;
    g_w5500_initialized = 0;
    g_last_phy_link = 0;
    g_request_ip_renew = 0;
    g_net_uptime_ms = 0;
    g_net_recovery_tick = 0;

    LOGI("NetManager: initialized");
}

/**
 * @brief 获取当前网络状态字符串描述
 */
const char* net_manager_get_state_str(void)
{
    switch (g_net_state) {
        case NET_DOWN:     return "NET_DOWN";
        case NET_LINK_UP:  return "NET_LINK_UP";
        case NET_IP_OK:    return "NET_IP_OK";
        default:           return "UNKNOWN";
    }
}

/**
 * @brief 检查PHY Link状态
 * @return 1 Link UP, 0 Link DOWN
 */
int net_manager_check_phy_link(void)
{
    int8_t link_status;

    link_status = wizphy_getphylink();
    return (link_status == PHY_LINK_ON) ? 1 : 0;
}

/**
 * @brief 获取当前IP地址
 */
int net_manager_get_ip(uint8_t ip_out[4])
{
    wiz_NetInfo netinfo;

    if (g_net_state != NET_IP_OK) {
        return -1;
    }

    ctlnetwork(CN_GET_NETINFO, (void*)&netinfo);
    ip_out[0] = netinfo.ip[0];
    ip_out[1] = netinfo.ip[1];
    ip_out[2] = netinfo.ip[2];
    ip_out[3] = netinfo.ip[3];

    return 0;
}

/**
 * @brief 标记需要重新获取IP
 */
void net_manager_request_ip_renew(void)
{
    g_request_ip_renew = 1;
    LOGI("NetManager: IP renew requested");
}

/**
 * @brief 获取网络恢复后的运行时间
 */
uint32_t net_manager_get_uptime_ms(void)
{
    if (g_net_recovery_tick == 0) {
        return 0;
    }
    return HAL_GetTick() - g_net_recovery_tick;
}

/**
 * @brief 获取当前网络状态
 */
net_state_t net_manager_get_state(void)
{
    return g_net_state;
}

/*============================================================================
 * 网络管理器任务主函数
 *============================================================================*/
/**
 * @brief 网络管理器任务主函数
 *
 * 运行逻辑:
 * 1. 初始化W5500 (只执行一次)
 * 2. 检查PHY Link状态
 * 3. Link断开 -> 标记NET_DOWN
 * 4. Link恢复 -> DHCP获取IP
 * 5. IP成功 -> 通知MQTT层
 */
void net_manager_task(void const * argument)
{
    (void)argument;
    uint8_t current_phy_link;
    uint8_t dhcp_run_result;

    LOGI("NetManager: task started");

    /* 初始化网络管理器状态 */
    net_manager_init();

    /* 主循环 */
    while (1) {

        /*----------------------------------------------------------------------
         * 步骤1: W5500初始化 (仅执行一次)
         *--------------------------------------------------------------------*/
        if (!g_w5500_initialized) {
            LOGI("NetManager: initializing W5500...");

            if (w5500_hw_init() != 0) {
                LOGE("NetManager: W5500 init failed, retry in 1s");
                osDelay(1000);
                continue;
            }

            g_w5500_initialized = 1;
            LOGI("NetManager: W5500 initialized successfully");
        }

        /*----------------------------------------------------------------------
         * 步骤2: 检查PHY Link状态
         *--------------------------------------------------------------------*/
        current_phy_link = net_manager_check_phy_link();

        /* Link状态变化检测 */
        if (current_phy_link != g_last_phy_link) {
            g_last_phy_link = current_phy_link;

            if (current_phy_link) {
                LOGI("LINK: UP");
                g_net_state = NET_LINK_UP;
            } else {
                LOGI("LINK: DOWN");
                g_net_state = NET_DOWN;
                /* 网线断开,清除恢复时间 */
                g_net_recovery_tick = 0;
            }
        }

        /*----------------------------------------------------------------------
         * 步骤3: Link断开处理
         *--------------------------------------------------------------------*/
        if (g_net_state == NET_DOWN) {
            osDelay(100);
            continue;
        }

        /*----------------------------------------------------------------------
         * 步骤4: Link恢复,获取IP
         *--------------------------------------------------------------------*/
        if (g_net_state == NET_LINK_UP) {
            LOGI("NetManager: Link established, acquiring IP...");

#if NET_DHCP_MODE
            /* DHCP模式初始化 */
            DHCP_init(0, g_dhcp_buf);
            reg_dhcp_cbfunc(
                dhcp_callback_ip_assign,
                dhcp_callback_ip_update,
                dhcp_callback_ip_conflict
            );

            /* DHCP主循环 - 非阻塞 */
            dhcp_run_result = DHCP_run();

            if (dhcp_run_result == DHCP_IP_ASSIGN) {
                LOGI("DHCP: First IP assigned");
                g_net_state = NET_IP_OK;
                g_net_uptime_ms = 0;
                g_net_recovery_tick = HAL_GetTick();
            } else if (dhcp_run_result == DHCP_IP_CHANGED) {
                LOGI("DHCP: IP changed");
                g_net_state = NET_IP_OK;
                g_net_uptime_ms = 0;
                g_net_recovery_tick = HAL_GetTick();
            } else if (dhcp_run_result == DHCP_RUNNING) {
                /* DHCP还在运行中,继续等待 */
                /* 状态保持在NET_LINK_UP */
            } else if (dhcp_run_result == DHCP_FAILED) {
                LOGW("DHCP: failed, retry in 1s...");
                osDelay(1000);
                /* 重新初始化DHCP */
                DHCP_init(0, g_dhcp_buf);
            }
            /* DHCP_STOPPED表示暂时无DHCP响应,继续循环 */

#else
            /* 静态IP模式 */
            set_static_ip();
            g_net_state = NET_IP_OK;
            g_net_uptime_ms = 0;
            g_net_recovery_tick = HAL_GetTick();
            LOGI("Static IP configured");
#endif
        }

        /*----------------------------------------------------------------------
         * 步骤5: IP OK状态处理
         *--------------------------------------------------------------------*/
        if (g_net_state == NET_IP_OK) {
            /* 更新运行时间 */
            g_net_uptime_ms += 100;

            /* 检查是否需要重新获取IP */
            if (g_request_ip_renew) {
                g_request_ip_renew = 0;
#if NET_DHCP_MODE
                LOGI("DHCP: renewing IP...");
                DHCP_stop();
                DHCP_init(0, g_dhcp_buf);
                g_net_state = NET_LINK_UP;
#endif
            }
        }

        osDelay(100);
    }
}