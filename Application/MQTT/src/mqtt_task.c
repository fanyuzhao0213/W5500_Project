/**
 * @file mqtt_task.c
 * @brief MQTT 任务实现 - 包含状态机和三个任务
 */

#include "mqtt_task.h"
#include "mqtt_client.h"
#include "mqtt_queue.h"
#include "mqtt_config.h"
#include "mqtt_exception.h"
#include "wizchip_conf.h"
#include "netconf.h"
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
#include "dhcp.h"
#endif
#include "LOG.h"
#include "main.h"
#include "w5500_conf.h"


/* 外部变量声明 */
extern IWDG_HandleTypeDef hiwdg;
extern SPI_HandleTypeDef hspi1;

/* 任务句柄 */
osThreadId g_network_task_handle;
osThreadId g_mqtt_tx_task_handle;
osThreadId g_mqtt_rx_task_handle;

/* 当前网络状态 */
static net_state_t g_net_state = NET_STATE_INIT;

/* MQTT 运行标志 - MQTT连接成功后置1 */
static volatile uint8_t g_mqtt_running = 0;

#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
/* DHCP 缓冲区 (DHCP报文最大576字节，留余量使用1024) */
static uint8_t g_dhcp_buf[1024];

/* DHCP 定时器计数器 (1秒tick) */
static volatile uint32_t g_dhcp_tick_counter = 0;
#endif

/**
 * @brief 获取当前网络状态
 */
net_state_t mqtt_task_get_state(void)
{
    return g_net_state;
}

/**
 * @brief MQTT是否已连接并运行
 */
int mqtt_is_running(void)
{
    return g_mqtt_running;
}

/**
 * @brief 设置网络状态
 * @note 由异常处理模块调用,用于切换到指定状态
 * @param state 要设置的状态 (传入 NET_STATE_INIT 表示完全重置)
 */
void mqtt_task_set_state(net_state_t state)
{
    LOGW("mqtt_task: setting state to %d", state);

    if (state == NET_STATE_INIT) {
        /* 完全重置 */
        g_net_state = NET_STATE_INIT;
        g_mqtt_running = 0;
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
        DHCP_stop();
#endif
        mqtt_exception_reset();
        LOGI("mqtt_task: full reset complete");
    } else {
        /* 只切换状态 */
        g_net_state = state;
        LOGI("mqtt_task: state set complete");
    }
}

/**
 * @brief 设置 MQTT 运行标志
 * @note 由异常处理模块调用
 * @param running 1 运行中, 0 未连接
 */
void mqtt_task_set_running(uint8_t running)
{
    LOGW("mqtt_task: setting MQTT running to %d", running);
    g_mqtt_running = running;
}

#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
/**
 * @brief DHCP IP分配回调函数
 */
static void dhcp_ip_assign_callback(void)
{
    wiz_NetInfo_t net_info;

    getIPfromDHCP(net_info.ip);
    getGWfromDHCP(net_info.gw);
    getSNfromDHCP(net_info.sn);
    getDNSfromDHCP(net_info.dns);

    net_info.mac[0] = 0x00; net_info.mac[1] = 0x08; net_info.mac[2] = 0xDC;
    net_info.mac[3] = 0x12; net_info.mac[4] = 0x34; net_info.mac[5] = 0x56;

    ctlnetwork(CN_SET_NETINFO, (void*)&net_info);

    LOGI("DHCP: IP assigned successfully!");
    LOGI("  IP:  %d.%d.%d.%d", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    LOGI("  GW:  %d.%d.%d.%d", net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);
    LOGI("  SN:  %d.%d.%d.%d", net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3]);
    LOGI("  DNS: %d.%d.%d.%d", net_info.dns[0], net_info.dns[1], net_info.dns[2], net_info.dns[3]);
}

/**
 * @brief DHCP IP更新回调函数
 */
static void dhcp_ip_update_callback(void)
{
    wiz_NetInfo_t net_info;

    getIPfromDHCP(net_info.ip);
    getGWfromDHCP(net_info.gw);
    getSNfromDHCP(net_info.sn);
    getDNSfromDHCP(net_info.dns);

    net_info.mac[0] = 0x00; net_info.mac[1] = 0x08; net_info.mac[2] = 0xDC;
    net_info.mac[3] = 0x12; net_info.mac[4] = 0x34; net_info.mac[5] = 0x56;

    ctlnetwork(CN_SET_NETINFO, (void*)&net_info);

    LOGI("DHCP: IP updated!");
    LOGI("  New IP: %d.%d.%d.%d", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
}

/**
 * @brief DHCP IP冲突回调函数
 */
static void dhcp_ip_conflict_callback(void)
{
    LOGE("DHCP: IP conflict detected!");
}

/**
 * @brief DHCP 1秒定时器处理
 * @note 在状态机中每秒调用一次
 */
static void dhcp_timer_handler(void)
{
    g_dhcp_tick_counter++;
    DHCP_time_handler();
}
#endif

/**
 * @brief 创建所有 MQTT 相关任务
 */
void mqtt_task_create(void)
{
    LOGI("mqtt_task_create: creating NetworkTask...");
    osThreadDef(networkTask, StartNetworkTask, NETWORK_TASK_PRIORITY, 0, NETWORK_TASK_STACK);
    g_network_task_handle = osThreadCreate(osThread(networkTask), NULL);
    if (g_network_task_handle == NULL) {
        LOGE("NetworkTask create failed");
    }

    LOGI("mqtt_task_create: creating MqttTxTask...");
    osThreadDef(mqttTxTask, StartMQTTTxTask, MQTT_TX_TASK_PRIORITY, 0, MQTT_TX_TASK_STACK);
    g_mqtt_tx_task_handle = osThreadCreate(osThread(mqttTxTask), NULL);
    if (g_mqtt_tx_task_handle == NULL) {
        LOGE("MqttTxTask create failed");
    }

    LOGI("mqtt_task_create: creating MqttRxTask...");
    osThreadDef(mqttRxTask, StartMQTTRxTask, MQTT_RX_TASK_PRIORITY, 0, MQTT_RX_TASK_STACK);
    g_mqtt_rx_task_handle = osThreadCreate(osThread(mqttRxTask), NULL);
    if (g_mqtt_rx_task_handle == NULL) {
        LOGE("MqttRxTask create failed");
    }

    LOGI("mqtt_task_create: creating ExceptionTask...");
    mqtt_exception_task_create();
}

/**
 * @brief MQTT 消息接收回调
 * @note 将消息放入接收队列, 由 StartMQTTRxTask 处理
 */
void mqtt_message_callback(MessageData *md)
{
    mqtt_msg_t msg;
    int topic_len, payload_len;

    if (!g_mqtt_running) {
        return;
    }

    /* 获取主题长度 */
    topic_len = md->topicName->lenstring.len;
    if (topic_len >= 64) {
        topic_len = 63;
    }

    /* 获取载荷长度 */
    payload_len = (int)md->message->payloadlen;
    if (payload_len >= 256) {
        payload_len = 255;
    }

    /* 复制主题 */
    memcpy(msg.topic, md->topicName->lenstring.data, topic_len);
    msg.topic[topic_len] = '\0';

    /* 复制载荷 */
    memcpy(msg.payload, md->message->payload, payload_len);
    msg.payload[payload_len] = '\0';
    msg.len = payload_len;

    /* 获取 QoS */
    msg.qos = md->message->qos;

    /* 放入接收队列 (非阻塞) */
    if (mqtt_rx_queue_put(&msg, 0) != 0) {
        LOGW("MQTT RX: queue full, message dropped");
    }
}

/**
 * @brief 网络任务 - W5500初始化和MQTT协议循环
 * @note 使用状态机, 每个状态执行一次后立即 break, 由 FreeRTOS 调度
 */
void StartNetworkTask(void const * argument)
{
    uint8_t version;
    uint8_t snum[8];
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
    uint8_t dhcp_result;
    static uint32_t last_dhcp_tick = 0;
    static uint32_t dhcp_retry_count = 0;
#endif

    (void)argument;

    LOGI("NetworkTask: started");
    LOGI("NetworkTask: entering state machine...");

    /* 状态机主循环 */
    for (;;) {
        switch (g_net_state) {
            case NET_STATE_INIT: {
                LOGI("NET_STATE_INIT: Registering W5500 SPI callbacks...");

                /* 注册 W5500 SPI 回调函数 */
                wizchip_spi_cbfunc();

                LOGI("NET_STATE_INIT: Initializing W5500...");

                /* 初始化 W5500 */
                memset(snum, 16, sizeof(snum));
                wizchip_init(snum, snum);

                HAL_Delay(100);

                LOGI("NET_STATE_INIT: W5500 initialization complete");
                g_net_state = NET_STATE_W5500_CHECK;
                break;
            }

            case NET_STATE_W5500_CHECK: {
                LOGI("NET_STATE_W5500_CHECK: Reading VERSIONR...");

                /* 读取版本寄存器验证 W5500 */
                version = getVERSIONR();

                if (version == 0x04) {
                    LOGI("NET_STATE_W5500_CHECK: W5500 OK! VERSIONR=0x%02X", version);
                    g_net_state = NET_STATE_PHY_LINK_CHECK;
                } else {
                    LOGE("NET_STATE_W5500_CHECK: W5500 ERROR! VERSIONR=0x%02X (expected 0x04)", version);
                    mqtt_exception_report(EXCEPTION_W5500_ERROR);
                    g_net_state = NET_STATE_ERROR;
                }
                break;
            }

            case NET_STATE_PHY_LINK_CHECK: {
                uint8_t phy_link;

                LOGI("NET_STATE_PHY_LINK_CHECK: Checking PHY link status...");

                /* 检查 PHY 链路状态 */
                phy_link = wizphy_getphylink();

                if (phy_link == PHY_LINK_ON) {
                    LOGI("NET_STATE_PHY_LINK_CHECK: Ethernet cable connected (PHY_LINK_ON)");

                    /* 根据配置选择 IP 获取方式 */
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
                    LOGI("NET_STATE_PHY_LINK_CHECK: Using DHCP mode");
                    g_net_state = NET_STATE_DHCP_START;
#else
                    LOGI("NET_STATE_PHY_LINK_CHECK: Using Static IP mode");
                    g_net_state = NET_STATE_NET_CONFIG;
#endif
                } else {
                    LOGW("NET_STATE_PHY_LINK_CHECK: Ethernet cable disconnected, waiting...");
                    osDelay(1000);
                }
                break;
            }

#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
            case NET_STATE_DHCP_START: {
                LOGI("NET_STATE_DHCP_START: Starting DHCP client...");

                /* 初始化 DHCP (使用 Socket 3) */
                DHCP_init(3, g_dhcp_buf);

                /* 注册 DHCP 回调函数 */
                reg_dhcp_cbfunc(dhcp_ip_assign_callback,
                                dhcp_ip_update_callback,
                                dhcp_ip_conflict_callback);

                dhcp_retry_count = 0;
                last_dhcp_tick = HAL_GetTick();

                LOGI("NET_STATE_DHCP_START: DHCP client initialized on Socket 3");
                g_net_state = NET_STATE_DHCP_RUN;
                break;
            }

            case NET_STATE_DHCP_RUN: {
                /* 每1秒调用一次 DHCP 定时器 */
                if (HAL_GetTick() - last_dhcp_tick >= 1000) {
                    last_dhcp_tick = HAL_GetTick();
                    dhcp_timer_handler();
                }

                /* 运行 DHCP 协议 */
                dhcp_result = DHCP_run();

                switch (dhcp_result) {
                    case DHCP_IP_ASSIGN:
                        LOGI("NET_STATE_DHCP_RUN: DHCP_IP_ASSIGN - IP assigned from DHCP server");
                        g_net_state = NET_STATE_MQTT_INIT;
                        break;

                    case DHCP_IP_CHANGED:
                        LOGI("NET_STATE_DHCP_RUN: DHCP_IP_CHANGED - IP address updated");
                        g_net_state = NET_STATE_MQTT_INIT;
                        break;

                    case DHCP_IP_LEASED:
                        LOGI("NET_STATE_DHCP_RUN: DHCP_IP_LEASED - IP lease renewed");
                        g_net_state = NET_STATE_MQTT_INIT;
                        break;

                    case DHCP_RUNNING:
                        /* DHCP 还在运行中，继续等待 */
                        if (dhcp_retry_count % 100 == 0) {
                            LOGI("NET_STATE_DHCP_RUN: DHCP running... (retry=%d)", dhcp_retry_count / 100);
                        }
                        dhcp_retry_count++;

                        /* 超时检查 (60秒) */
                        if (dhcp_retry_count > 1000) {
                            LOGE("NET_STATE_DHCP_RUN: DHCP timeout after 10 seconds");
                            mqtt_exception_report(EXCEPTION_DHCP_TIMEOUT);
                            g_net_state = NET_STATE_WAITTING;
                        }
                        break;

                    case DHCP_FAILED:
                        LOGE("NET_STATE_DHCP_RUN: DHCP failed!");
                        DHCP_stop();
                        mqtt_exception_report(EXCEPTION_DHCP_FAILED);
                        g_net_state = NET_STATE_WAITTING;
                        break;

                    case DHCP_STOPPED:
                        LOGW("NET_STATE_DHCP_RUN: DHCP stopped");
                        g_net_state = NET_STATE_ERROR;
                        break;

                    default:
                        LOGW("NET_STATE_DHCP_RUN: Unknown DHCP result: %d", dhcp_result);
                        break;
                }
                break;
            }
#endif

            case NET_STATE_NET_CONFIG: {
                wiz_NetInfo_t net_info;
                int temp[4];

                LOGI("NET_STATE_NET_CONFIG: Configuring static IP...");

                /* 设置 MAC 地址 */
                net_info.mac[0] = 0x00;
                net_info.mac[1] = 0x08;
                net_info.mac[2] = 0xDC;
                net_info.mac[3] = 0x12;
                net_info.mac[4] = 0x34;
                net_info.mac[5] = 0x56;

                /* 解析静态 IP 配置 */
                sscanf(STATIC_IP_ADDR, "%d.%d.%d.%d",
                       &temp[0], &temp[1], &temp[2], &temp[3]);
                net_info.ip[0] = (uint8_t)temp[0];
                net_info.ip[1] = (uint8_t)temp[1];
                net_info.ip[2] = (uint8_t)temp[2];
                net_info.ip[3] = (uint8_t)temp[3];

                sscanf(STATIC_SUBNET_MASK, "%d.%d.%d.%d",
                       &temp[0], &temp[1], &temp[2], &temp[3]);
                net_info.sn[0] = (uint8_t)temp[0];
                net_info.sn[1] = (uint8_t)temp[1];
                net_info.sn[2] = (uint8_t)temp[2];
                net_info.sn[3] = (uint8_t)temp[3];

                sscanf(STATIC_GATEWAY, "%d.%d.%d.%d",
                       &temp[0], &temp[1], &temp[2], &temp[3]);
                net_info.gw[0] = (uint8_t)temp[0];
                net_info.gw[1] = (uint8_t)temp[1];
                net_info.gw[2] = (uint8_t)temp[2];
                net_info.gw[3] = (uint8_t)temp[3];

                sscanf(STATIC_DNS, "%d.%d.%d.%d",
                       &temp[0], &temp[1], &temp[2], &temp[3]);
                net_info.dns[0] = (uint8_t)temp[0];
                net_info.dns[1] = (uint8_t)temp[1];
                net_info.dns[2] = (uint8_t)temp[2];
                net_info.dns[3] = (uint8_t)temp[3];

                /* 设置网络参数 */
                ctlnetwork(CN_SET_NETINFO, (void*)&net_info);

                LOGI("NET_STATE_NET_CONFIG: Static IP configured successfully!");
                LOGI("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     net_info.mac[0], net_info.mac[1], net_info.mac[2],
                     net_info.mac[3], net_info.mac[4], net_info.mac[5]);
                LOGI("  IP:  %d.%d.%d.%d",
                     net_info.ip[0], net_info.ip[1],
                     net_info.ip[2], net_info.ip[3]);
                LOGI("  SN:  %d.%d.%d.%d",
                     net_info.sn[0], net_info.sn[1],
                     net_info.sn[2], net_info.sn[3]);
                LOGI("  GW:  %d.%d.%d.%d",
                     net_info.gw[0], net_info.gw[1],
                     net_info.gw[2], net_info.gw[3]);
                LOGI("  DNS: %d.%d.%d.%d",
                     net_info.dns[0], net_info.dns[1],
                     net_info.dns[2], net_info.dns[3]);

                g_net_state = NET_STATE_MQTT_INIT;
                break;
            }

            case NET_STATE_MQTT_INIT: {
                LOGI("NET_STATE_MQTT_INIT: Initializing MQTT client...");

                /* 初始化 MQTT 客户端 */
                mqtt_client_init();

                LOGI("NET_STATE_MQTT_INIT: MQTT client initialized");
                g_net_state = NET_STATE_MQTT_CONNECT;
                break;
            }

            case NET_STATE_MQTT_CONNECT: {
                LOGI("NET_STATE_MQTT_CONNECT: Connecting to MQTT Broker %s:%d...",
                     MQTT_BROKER_IP, MQTT_BROKER_PORT);

                /* 连接 MQTT Broker */
                if (mqtt_client_connect() != MQTT_SUCCESS) {
                    LOGE("NET_STATE_MQTT_CONNECT: MQTT connection failed!");
                    mqtt_exception_report(EXCEPTION_MQTT_DISCONNECTED);
                    g_net_state = NET_STATE_ERROR;
                } else {
                    LOGI("NET_STATE_MQTT_CONNECT: MQTT TCP connected successfully!");
                    g_net_state = NET_STATE_MQTT_SUBSCRIBE;
                }
                break;
            }

            case NET_STATE_MQTT_SUBSCRIBE: {
                LOGI("NET_STATE_MQTT_SUBSCRIBE: Subscribing to topic '%s'...", MQTT_SUBSCRIBE_TOPIC);

                /* 订阅主题 */
                if (mqtt_client_subscribe(MQTT_SUBSCRIBE_TOPIC, QOS1, mqtt_message_callback) != MQTT_SUCCESS) {
                    LOGE("NET_STATE_MQTT_SUBSCRIBE: Subscribe failed!");
                    mqtt_exception_report(EXCEPTION_MQTT_SUBSCRIBE_FAILED);
                    g_net_state = NET_STATE_ERROR;
                } else {
                    LOGI("NET_STATE_MQTT_SUBSCRIBE: Subscribed to '%s' successfully!", MQTT_SUBSCRIBE_TOPIC);
                    LOGI("========================================");
                    LOGI("MQTT: Successfully connected to broker!");
                    LOGI("  Broker: %s:%d", MQTT_BROKER_IP, MQTT_BROKER_PORT);
                    LOGI("  Client ID: %s", MQTT_CLIENT_ID);
                    LOGI("  Subscribed: %s", MQTT_SUBSCRIBE_TOPIC);
                    LOGI("========================================");
                    g_mqtt_running = 1;
                    g_net_state = NET_STATE_RUNNING;
                }
                break;
            }

            case NET_STATE_MQTT_RECONNECT: {
                LOGW("NET_STATE_MQTT_RECONNECT: Reconnecting to MQTT Broker %s:%d...",
                     MQTT_BROKER_IP, MQTT_BROKER_PORT);

                /* 先断开已有连接 */
                mqtt_client_disconnect();

                /* 重新连接 MQTT Broker */
                if (mqtt_client_connect() != MQTT_SUCCESS) {
                    LOGE("NET_STATE_MQTT_RECONNECT: MQTT reconnection failed!");
                    mqtt_exception_report(EXCEPTION_MQTT_DISCONNECTED);
                    g_net_state = NET_STATE_ERROR;
                } else {
                    LOGI("NET_STATE_MQTT_RECONNECT: MQTT TCP reconnected successfully!");

                    /* 重新订阅主题 */
                    if (mqtt_client_subscribe(MQTT_SUBSCRIBE_TOPIC, QOS1, mqtt_message_callback) != MQTT_SUCCESS) {
                        LOGE("NET_STATE_MQTT_RECONNECT: Re-subscribe failed!");
                        mqtt_exception_report(EXCEPTION_MQTT_SUBSCRIBE_FAILED);
                        g_net_state = NET_STATE_ERROR;
                    } else {
                        LOGI("NET_STATE_MQTT_RECONNECT: Re-subscribed to '%s' successfully!", MQTT_SUBSCRIBE_TOPIC);
                        LOGI("========================================");
                        LOGI("MQTT: Successfully re-connected to broker!");
                        LOGI("  Broker: %s:%d", MQTT_BROKER_IP, MQTT_BROKER_PORT);
                        LOGI("========================================");
                        g_mqtt_running = 1;
                        g_net_state = NET_STATE_RUNNING;
                    }
                }
                break;
            }

            case NET_STATE_RUNNING: {
#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
				/*
				0:00   - DHCP 获得 IP，租期 24小时
				12:00  - DHCP_run() 自动发送续约请求
				18:00  - 如果第一次续约失败，再次发送
				24:00  - 租期到期，重新开始 DHCP 流程
				*/
                /* DHCP 模式：每1秒调用 DHCP 定时器 (保持 DHCP 租约) */
                if (HAL_GetTick() - last_dhcp_tick >= 1000) {
                    last_dhcp_tick = HAL_GetTick();
                    dhcp_timer_handler();

                    /* 在 RUNNING 状态下继续运行 DHCP 保持租约 */
                    DHCP_run();
                }
#endif

                /* 非阻塞 MQTT 循环处理 */
                mqtt_client_loop(0);

                /* 短延时让出 CPU */
                osDelay(10);
                break;
            }
			case NET_STATE_WAITTING:
				LOGI("NET_STATE_WAITTING: wait systerm dealwith error!");
				osDelay(100);
				break;
            case NET_STATE_ERROR:
            default: {
                LOGE("NET_STATE_ERROR: Network error, retrying in 5 seconds...");
                g_mqtt_running = 0;
                mqtt_exception_report(EXCEPTION_NETWORK_ERROR);

#if (NET_CONFIG_MODE == NET_CONFIG_DHCP)
                DHCP_stop();
#endif

                osDelay(5000);

                /* 重试：回到 PHY 链路检查 */
                LOGI("NET_STATE_ERROR: Retrying from PHY link check...");
                g_net_state = NET_STATE_PHY_LINK_CHECK;
                break;
            }
        }

        osDelay(10);
    }
}

/**
 * @brief MQTT 发送任务 - 从发送队列取数据并发布
 */
void StartMQTTTxTask(void const * argument)
{
    mqtt_msg_t msg;
    (void)argument;

    for (;;) {
        /* 等待 MQTT 连接成功 */
        while (!g_mqtt_running) {
            osDelay(100);
        }

        /* 从发送队列获取消息 (超时500ms) */
        if (mqtt_tx_queue_get(&msg, 500) == osOK) {
            if (mqtt_client_publish(msg.topic, msg.payload, msg.len, msg.qos) != MQTT_SUCCESS) {
                LOGE("MQTT TxTask: publish failed");
            }
        }
    }
}

/**
 * @brief MQTT 接收任务 - 从接收队列取数据并处理
 */
void StartMQTTRxTask(void const * argument)
{
    mqtt_msg_t msg;
    (void)argument;

    for (;;) {
        /* 等待 MQTT 连接成功 */
        while (!g_mqtt_running) {
            osDelay(100);
        }

        /* 从接收队列获取消息 (超时500ms) */
        if (mqtt_rx_queue_get(&msg, 500) == osOK) {
            LOGI("MQTT RxTask: topic=%s, payload=%.*s", msg.topic, msg.len, msg.payload);
        }
    }
}

