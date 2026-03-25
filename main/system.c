#include "system.h"
#include "priorities.h"
#include "wifi_service.h"
#include "gt911_service.h"
#include "esp_log.h"

void wifi_supervisor(void *arg)
{
    static const char *TAG = "wifi-super";
    ESP_LOGI(TAG, "Starting WiFi supervisor");
    wifi_service_stop();
    wifi_service_start();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}

void gt911_supervisor(void *arg)
{
    static const char *TAG = "gt911-super";
    ESP_LOGI(TAG, "Starting GT911 supervisor");
    gt911_service_stop();
    gt911_service_start();
    while (1) {
        if (!gt911_service_is_running()) {
            ESP_LOGW(TAG, "GT911 service stopped");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}

/* Service registry - WiFi and GT911 only for now */
const service_def_t services[] = {
    {"wifi",  wifi_supervisor,   8192, PRIO_WIFI_SUPERVISOR,  RESTART_ALWAYS, true,  NULL, 30},
    {"gt911", gt911_supervisor,  4096, PRIO_GT911_SUPERVISOR, RESTART_ALWAYS, false, NULL, 5},
    {NULL, NULL, 0, 0, RESTART_NEVER, false, NULL, 0}
};