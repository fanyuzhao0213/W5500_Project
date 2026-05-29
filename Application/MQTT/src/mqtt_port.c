#include "mqtt_port.h"
#include "tcp_client.h"
#include "stm32f4xx_hal.h"

/* Timer 实现 */
void TimerInit(Timer* timer) {
    timer->start_ms = HAL_GetTick();
    timer->timeout_ms = 0;
}

char TimerIsExpired(Timer* timer) {
    return (HAL_GetTick() - timer->start_ms) >= timer->timeout_ms;
}

void TimerCountdownMS(Timer* timer, unsigned int timeout_ms) {
    timer->start_ms = HAL_GetTick();
    timer->timeout_ms = timeout_ms;
}

void TimerCountdown(Timer* timer, unsigned int timeout) {
    TimerCountdownMS(timer, timeout * 1000);
}

int TimerLeftMS(Timer* timer) {
    uint32_t elapsed = HAL_GetTick() - timer->start_ms;
    if (elapsed >= timer->timeout_ms) {
        return 0;
    }
    return (int)(timer->timeout_ms - elapsed);
}

/* Network 实现 */
static int mqtt_network_read(Network* n, unsigned char* buffer, int len, int timeout_ms) {
    int recv_len = 0;
    int rc;
    Timer timer;
    
    TimerInit(&timer);
    TimerCountdownMS(&timer, timeout_ms);
    
    do {
        rc = tcp_client_recv(buffer + recv_len, len - recv_len);
        if (rc > 0) {
            recv_len += rc;
        } else if (rc < 0) {
            return -1;
        }
        if (recv_len < len) {
            HAL_Delay(1);
        }
    } while (recv_len < len && !TimerIsExpired(&timer));
    
    return recv_len;
}

static int mqtt_network_write(Network* n, unsigned char* buffer, int len, int timeout_ms) {
    int sent_len = 0;
    int rc;
    Timer timer;
    
    TimerInit(&timer);
    TimerCountdownMS(&timer, timeout_ms);
    
    do {
        rc = tcp_client_send(buffer + sent_len, len - sent_len);
        if (rc > 0) {
            sent_len += rc;
        } else if (rc < 0) {
            return -1;
        }
        if (sent_len < len) {
            HAL_Delay(1);
        }
    } while (sent_len < len && !TimerIsExpired(&timer));
    
    return sent_len;
}

static void mqtt_network_disconnect(Network* n) {
    tcp_client_disconnect();
}

void NetworkInit(Network* n) {
    n->mqttread = mqtt_network_read;
    n->mqttwrite = mqtt_network_write;
    n->disconnect = mqtt_network_disconnect;
}

