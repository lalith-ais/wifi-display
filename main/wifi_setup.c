#include "wifi_setup.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"  /* Key addition for hosted mode */
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

static const char *TAG = "wifi_setup";

static bool s_connected = false;
static bool s_has_ip = false;
static char s_ip_addr[16] = {0};
static uint8_t s_mac[6] = {0};

static wifi_connected_cb_t s_connected_cb = NULL;
static wifi_disconnected_cb_t s_disconnected_cb = NULL;
static wifi_got_ip_cb_t s_got_ip_cb = NULL;

/* Event handlers - these work the same as standard ESP-IDF */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started (via hosted)");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected to AP");
                s_connected = true;
                if (s_connected_cb) s_connected_cb();
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t *disconnected = event_data;
                    ESP_LOGI(TAG, "WiFi disconnected, reason: %d", disconnected->reason);
                    s_connected = false;
                    s_has_ip = false;
                    if (s_disconnected_cb) s_disconnected_cb();
                    
                    /* Auto-reconnect with backoff - handled by service */
                }
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
            s_has_ip = true;
            ESP_LOGI(TAG, "Got IP: %s", s_ip_addr);
            if (s_got_ip_cb) s_got_ip_cb();
        }
    }
}

esp_err_t wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi via hosted/remote interface...");
    
    /* Initialize network interface - standard */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    /* Initialize WiFi with remote config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    /* For hosted mode, we need to ensure the transport is initialized
     * This is typically handled by esp_hosted component's init function */
    
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Register event handlers */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                &wifi_event_handler, NULL);
    
    /* Get MAC address - this should work through hosted interface */
    ret = esp_wifi_get_mac(WIFI_IF_STA, s_mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);
    } else {
        ESP_LOGW(TAG, "Could not get MAC: %s", esp_err_to_name(ret));
        /* Fallback - maybe read from efuse or use default */
    }
    
    return ESP_OK;
}

esp_err_t wifi_start(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) return ret;
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) return ret;
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "WiFi started, connecting to SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_stop(void)
{
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi stopped");
    }
    return ret;
}

bool wifi_is_connected(void) { return s_connected; }
bool wifi_has_ip(void) { return s_has_ip; }

esp_err_t wifi_get_ip(char *ip_buffer, size_t len)
{
    if (!ip_buffer || len < 16) return ESP_ERR_INVALID_ARG;
    strncpy(ip_buffer, s_ip_addr, len - 1);
    ip_buffer[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t wifi_get_mac(uint8_t *mac)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    memcpy(mac, s_mac, 6);
    return ESP_OK;
}

void wifi_set_connected_cb(wifi_connected_cb_t cb) { s_connected_cb = cb; }
void wifi_set_disconnected_cb(wifi_disconnected_cb_t cb) { s_disconnected_cb = cb; }
void wifi_set_got_ip_cb(wifi_got_ip_cb_t cb) { s_got_ip_cb = cb; }