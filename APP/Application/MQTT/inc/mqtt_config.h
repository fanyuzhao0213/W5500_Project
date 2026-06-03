#ifndef _MQTT_CONFIG_H_
#define _MQTT_CONFIG_H_

/* ============================================================
 * 网络配置 - IP 地址获取方式
 * ============================================================
 * 选择以下模式之一：
 *   - NET_CONFIG_DHCP     : 使用 DHCP 自动获取 IP
 *   - NET_CONFIG_STATIC   : 使用静态 IP 配置
 */
/* ============================================================
 * 网络配置模式枚举 (内部使用)
 * ============================================================ */
#define NET_CONFIG_DHCP        0
#define NET_CONFIG_STATIC      1

#define NET_CONFIG_MODE        NET_CONFIG_DHCP

/* 静态 IP 配置参数 (仅在 NET_CONFIG_STATIC 模式下使用) */
#define STATIC_IP_ADDR         "192.168.1.88"
#define STATIC_SUBNET_MASK     "255.255.255.0"
#define STATIC_GATEWAY         "192.168.1.1"
#define STATIC_DNS             "8.8.8.8"

/* MAC 地址 (固定) */
#define MAC_ADDR               "00:08:DC:12:34:56"

/* MQTT Broker 配置
 * 支持 IP 或域名格式：
 *   - IP:     "47.74.187.120"
 *   - 域名:   "broker.emqx.io"
 */
#define MQTT_BROKER_HOSTNAME    "app-management-server.washer-saas.istarix.com"
#define MQTT_BROKER_PORT        20118
#define MQTT_CLIENT_ID          "W5500_DEVICE"
#define MQTT_USERNAME           "washer_saas_mu"
#define MQTT_PASSWORD           "$5ywq8bye5e7ah2hb*"

/* MQTT 配置参数 */
#define MQTT_KEEP_ALIVE        60
#define MQTT_CLEAN_SESSION     1
#define MQTT_COMMAND_TIMEOUT   5000

/* 订阅主题 */
#define MQTT_SUBSCRIBE_TOPIC   "stm32/test"

/* 发布主题 */
#define MQTT_PUBLISH_TOPIC     "stm32/uptime"
#define MQTT_PUBLISH_INTERVAL  5000

/* 缓冲区大小 - 接收缓冲区加大以支持 4KB OTA 数据块 */
#define MQTT_SEND_BUF_SIZE     (4 * 1024)   // 4KB - 支持OTA状态消息和ACK发送
#define MQTT_READ_BUF_SIZE     (8 * 1024)   // 8KB

/* 最大消息处理器数量 */
#define MAX_MESSAGE_HANDLERS   5

/* ============================================================
 * OTA 升级配置
 * ============================================================ */

/* 设备 ID (用于构建 OTA 主题)
 * 格式：w5500_XXX
 * 用途：区分不同设备，构建设备专属主题
 */
#define OTA_DEVICE_ID          "w5500_001"

/* OTA 主题定义
 *
 * 设备订阅主题：
 *   - OTA_TOPIC_CMD: 接收服务器下发的 OTA 命令
 *   - OTA_TOPIC_DATA: 接收固件数据块（分包传输）
 *   - OTA_TOPIC_RESPONSE: 接收服务器的响应
 *
 * 设备发布主题：
 *   - OTA_TOPIC_STATUS: 上报 OTA 状态和进度
 *   - OTA_TOPIC_ACK: 发送数据块接收确认
 */
#define OTA_TOPIC_CMD          "device/" OTA_DEVICE_ID "/ota/cmd"
#define OTA_TOPIC_STATUS       "device/" OTA_DEVICE_ID "/ota/status"
#define OTA_TOPIC_DATA         "device/" OTA_DEVICE_ID "/ota/data"
#define OTA_TOPIC_ACK          "device/" OTA_DEVICE_ID "/ota/ack"
#define OTA_TOPIC_RESPONSE     "device/" OTA_DEVICE_ID "/ota/response"

/* OTA 服务器主题（可选）
 * 用于服务器端订阅和发布
 */
#define OTA_SERVER_CMD         "server/ota/cmd"
#define OTA_SERVER_RESPONSE    "server/ota/response"

/* OTA 配置参数
 *
 * OTA_FIRMWARE_MAX_SIZE: 固件最大大小（字节）
 *   - 受限于 Flash 分区大小（480KB）
 *
 * OTA_DOWNLOAD_TIMEOUT: 下载超时时间（毫秒）
 *   - 网络不稳定时可适当增加
 *
 * OTA_MAX_RETRY_COUNT: 最大重试次数
 *   - 下载失败时的重试次数
 *
 * OTA_PROGRESS_INTERVAL: 进度上报间隔（百分比）
 *   - 每 10% 上报一次进度
 *
 * OTA_DEFAULT_CHUNK_SIZE: 默认数据块大小（字节）
 *   - MQTT 消息负载限制，建议 4KB
 *   - 支持 1K(1024), 2K(2048), 4K(4096)
 *   - 过大会导致传输失败，过小效率低
 *
 * OTA_MAX_CHUNK_SIZE: 最大支持的数据块大小（字节）
 *   - 限制客户端发送的数据块大小
 */
#define OTA_FIRMWARE_MAX_SIZE  (480 * 1024)   // 480KB
#define OTA_DOWNLOAD_TIMEOUT   30000          // 30 秒
#define OTA_MAX_RETRY_COUNT    3              // 最多重试 3 次
#define OTA_PROGRESS_INTERVAL  10             // 每 10% 上报一次
#define OTA_DEFAULT_CHUNK_SIZE (1* 1024)     // 默认 4KB 数据块
#define OTA_MAX_CHUNK_SIZE     (4 * 1024)     // 最大支持 4KB
#define OTA_MIN_CHUNK_SIZE     1024           // 最小支持 1KB

#endif /* _MQTT_CONFIG_H_ */


