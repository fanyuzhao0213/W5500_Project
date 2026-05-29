#ifndef _MQTT_PORT_H_
#define _MQTT_PORT_H_

#include <stdint.h>
#include <string.h>

/* Timer 结构体 */
typedef struct {
    uint32_t start_ms;
    uint32_t timeout_ms;
} Timer;

/* Network 结构体 */
typedef struct Network Network;

struct Network {
    int (*mqttread)(Network*, unsigned char*, int, int);
    int (*mqttwrite)(Network*, unsigned char*, int, int);
    void (*disconnect)(Network*);
};

/* Timer 函数声明 */
void TimerInit(Timer*);
char TimerIsExpired(Timer*);
void TimerCountdownMS(Timer*, unsigned int);
void TimerCountdown(Timer*, unsigned int);
int TimerLeftMS(Timer*);

/* Network 函数声明 */
void NetworkInit(Network*);

#endif /* _MQTT_PORT_H_ */

