#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_connected_cb_t)(void);
typedef void (*wifi_disconnected_cb_t)(void);
typedef void (*wifi_got_ip_cb_t)(void);

esp_err_t wifi_init(void);
esp_err_t wifi_start(const char *ssid, const char *password);
esp_err_t wifi_stop(void);
esp_err_t wifi_reconnect(void);
bool wifi_is_connected(void);
bool wifi_has_ip(void);
const char *wifi_get_hostname(void);
esp_err_t wifi_get_ip(char *ip_buffer, size_t len);
esp_err_t wifi_get_mac(uint8_t *mac);
void wifi_set_connected_cb(wifi_connected_cb_t cb);
void wifi_set_disconnected_cb(wifi_disconnected_cb_t cb);
void wifi_set_got_ip_cb(wifi_got_ip_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SETUP_H */