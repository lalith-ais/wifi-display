#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_EVENT_CONNECTED,
    WIFI_EVENT_DISCONNECTED,
    WIFI_EVENT_GOT_IP,
    WIFI_EVENT_STARTED,
    WIFI_EVENT_STOPPED,
    WIFI_EVENT_ERROR,
    WIFI_EVENT_STOP_REQUESTED
} wifi_event_type_t;

typedef struct {
    wifi_event_type_t type;
    union {
        struct { char ip[16]; } got_ip;
        struct { esp_err_t error; } error;
    } data;
} wifi_service_message_t;

void wifi_service_start(void);
void wifi_service_stop(void);
QueueHandle_t wifi_service_get_queue(void);
bool wifi_service_is_connected(void);
bool wifi_service_has_ip(void);
bool wifi_service_is_running(void);
const char *wifi_service_get_ip(void);
const char *wifi_service_get_hostname(void);
const uint8_t *wifi_service_get_mac(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SERVICE_H */