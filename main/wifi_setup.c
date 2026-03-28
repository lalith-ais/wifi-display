#include "wifi_setup.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "esp_hosted.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

static const char *TAG = "wifi_setup";

static bool s_connected = false;
static bool s_has_ip = false;
static char s_ip_addr[16] = {0};
static uint8_t s_mac[6] = {0};
static char s_hostname[32] = {0};  /* Buffer for hostname */
static bool s_hosted_initialized = false;

static wifi_connected_cb_t s_connected_cb = NULL;
static wifi_disconnected_cb_t s_disconnected_cb = NULL;
static wifi_got_ip_cb_t s_got_ip_cb = NULL;

/* Convert MAC address to hostname string (without colons) */
static void mac_to_hostname(const uint8_t *mac, char *hostname, size_t len)
{
    /* Format: AABBCCA1B2C3 (12 hex characters) */
    snprintf(hostname, len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Event handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started (via SDIO hosted)");
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
                    ESP_LOGW(TAG, "WiFi disconnected, reason: %d", disconnected->reason);
                    s_connected = false;
                    s_has_ip = false;
                    if (s_disconnected_cb) s_disconnected_cb();
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
            ESP_LOGI(TAG, "Hostname: %s", s_hostname);
            if (s_got_ip_cb) s_got_ip_cb();
        }
    }
}

esp_err_t wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi via SDIO hosted interface...");
    
    /* Initialize SDIO hosted transport */
    if (!s_hosted_initialized) {
        ESP_LOGI(TAG, "Initializing ESP-Hosted with SDIO transport");
        esp_err_t ret = esp_hosted_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_hosted_init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_hosted_initialized = true;
        ESP_LOGI(TAG, "SDIO Hosted transport initialized successfully");
    }
    
    /* Initialize network interface */
    ESP_LOGI(TAG, "Initializing network interfaces...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    
    /* Initialize WiFi with remote config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
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
    
    /* Get MAC address via SDIO hosted interface */
    ret = esp_wifi_get_mac(WIFI_IF_STA, s_mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);
        
        /* Generate hostname from MAC address */
        mac_to_hostname(s_mac, s_hostname, sizeof(s_hostname));
        ESP_LOGI(TAG, "Generated hostname: %s", s_hostname);
        
        /* Set hostname on the network interface */
        if (sta_netif != NULL) {
            esp_netif_set_hostname(sta_netif, s_hostname);
            ESP_LOGI(TAG, "Hostname set to: %s", s_hostname);
        } else {
            ESP_LOGW(TAG, "Network interface not available for hostname");
        }
    } else {
        ESP_LOGW(TAG, "Could not get MAC: %s", esp_err_to_name(ret));
        /* Fallback MAC address */
        uint8_t default_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
        memcpy(s_mac, default_mac, 6);
        
        /* Fallback hostname */
        strcpy(s_hostname, "ESP32P4_DEFAULT");
        ESP_LOGW(TAG, "Using fallback hostname: %s", s_hostname);
        
        if (sta_netif != NULL) {
            esp_netif_set_hostname(sta_netif, s_hostname);
        }
    }
    
    ESP_LOGI(TAG, "WiFi initialization complete");
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
    
    /* Ensure SSID and password are null-terminated */
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    
    ESP_LOGI(TAG, "Setting WiFi mode to STA");
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Setting WiFi configuration for SSID: %s", ssid);
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Starting WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi started, connecting to SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi...");
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi stopped");
    } else {
        ESP_LOGW(TAG, "WiFi stop returned: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t wifi_reconnect(void)
{
    ESP_LOGI(TAG, "Attempting to reconnect via SDIO...");
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Reconnect initiated");
    }
    return ret;
}

bool wifi_is_connected(void) 
{ 
    return s_connected; 
}

bool wifi_has_ip(void) 
{ 
    return s_has_ip; 
}

const char *wifi_get_hostname(void)
{
    return s_hostname;
}

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

void wifi_set_connected_cb(wifi_connected_cb_t cb) 
{ 
    s_connected_cb = cb; 
}

void wifi_set_disconnected_cb(wifi_disconnected_cb_t cb) 
{ 
    s_disconnected_cb = cb; 
}

void wifi_set_got_ip_cb(wifi_got_ip_cb_t cb) 
{ 
    s_got_ip_cb = cb; 
}