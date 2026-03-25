#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "system.h"

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI("main", "NVS needs erase, doing it...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI("main", "Starting WiFi-only system...");
    
    /* Start supervisor with WiFi service */
    supervisor_start(services);
    
    /* Main task can delete itself */
    vTaskDelete(NULL);
}