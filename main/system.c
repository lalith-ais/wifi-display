#include <stdbool.h>
#include "system.h"
#include "priorities.h"
#include "wifi_service.h"
#include "esp_log.h"

static const char *TAG = "system";

/* WiFi supervisor - monitors WiFi service and logs only on state changes */
void wifi_supervisor(void *arg)
{
    ESP_LOGI(TAG, "WiFi supervisor started - monitoring WiFi service");
    
    /* Ensure WiFi service is started */
    if (!wifi_service_is_running()) {
        ESP_LOGW(TAG, "WiFi service not running, starting...");
        wifi_service_start();
    }
    
    /* Track connection state to log only on changes */
    bool last_connected = false;
    bool last_has_ip = false;
    char last_ip[16] = {0};
    bool logged_stable = false;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); /* Check every second */
        
        bool current_connected = wifi_service_is_connected();
        bool current_has_ip = wifi_service_has_ip();
        const char *current_ip = wifi_service_get_ip();
        
        /* Log only when state changes */
        if (current_connected != last_connected) {
            if (current_connected) {
                ESP_LOGI(TAG, "WiFi connected");
            } else {
                ESP_LOGW(TAG, "WiFi disconnected");
            }
            last_connected = current_connected;
        }
        
        if (current_has_ip != last_has_ip) {
            if (current_has_ip) {
                ESP_LOGI(TAG, "Got IP: %s", current_ip);
                strncpy(last_ip, current_ip, sizeof(last_ip) - 1);
            } else {
                ESP_LOGW(TAG, "IP lost");
                last_ip[0] = '\0';
            }
            last_has_ip = current_has_ip;
        } else if (current_has_ip && strcmp(current_ip, last_ip) != 0) {
            /* IP address changed (shouldn't normally happen, but handle it) */
            ESP_LOGI(TAG, "IP changed to: %s", current_ip);
            strncpy(last_ip, current_ip, sizeof(last_ip) - 1);
        }
        
        /* Log once after connection is stable */
        if (current_connected && current_has_ip && !logged_stable) {
            ESP_LOGI(TAG, "WiFi stable - IP: %s, Hostname: %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     current_ip,
                     wifi_service_get_hostname(),
                     wifi_service_get_mac()[0],
                     wifi_service_get_mac()[1],
                     wifi_service_get_mac()[2],
                     wifi_service_get_mac()[3],
                     wifi_service_get_mac()[4],
                     wifi_service_get_mac()[5]);
            logged_stable = true;
        } else if (!current_connected || !current_has_ip) {
            logged_stable = false;
        }
    }
    
    vTaskDelete(NULL);
}

/* Service registry - WiFi service only */
const service_def_t services[] = {
    {"wifi", wifi_supervisor, 4096, PRIO_WIFI_SUPERVISOR, RESTART_ALWAYS, true, NULL, 30},
    {NULL, NULL, 0, 0, RESTART_NEVER, false, NULL, 0}
};