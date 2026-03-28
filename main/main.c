#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "system.h"

void app_main(void)
{
    /* Set global log level */
    esp_log_level_set("*", ESP_LOG_INFO);
    
    /* Suppress verbose RPC and transport logs */
    esp_log_level_set("rpc_core", ESP_LOG_WARN);      /* Suppress RPC messages */
    esp_log_level_set("rpc_rsp", ESP_LOG_WARN);       /* Suppress RPC response messages */
    esp_log_level_set("rpc_evt", ESP_LOG_WARN);       /* Suppress RPC event messages */
    esp_log_level_set("RPC_WRAP", ESP_LOG_WARN);      /* Suppress RPC wrapper messages */
    esp_log_level_set("transport", ESP_LOG_WARN);     /* Suppress transport messages */
    esp_log_level_set("H_SDIO_DRV", ESP_LOG_WARN);    /* Suppress SDIO driver messages */
    esp_log_level_set("sdio_wrapper", ESP_LOG_WARN);  /* Suppress SDIO wrapper messages */
    esp_log_level_set("H_API", ESP_LOG_WARN);         /* Suppress Hosted API messages */
    esp_log_level_set("esp_cli", ESP_LOG_WARN);       /* Suppress CLI messages */
    esp_log_level_set("os_wrapper_esp", ESP_LOG_WARN);/* Suppress OS wrapper messages */
    esp_log_level_set("hci_stub_drv", ESP_LOG_WARN);  /* Suppress HCI stub messages */
    
    /* Keep your application logs at INFO level */
    esp_log_level_set("main", ESP_LOG_INFO);
    esp_log_level_set("init", ESP_LOG_INFO);
    esp_log_level_set("system", ESP_LOG_INFO);
    esp_log_level_set("wifi-service", ESP_LOG_INFO);
    esp_log_level_set("wifi_setup", ESP_LOG_INFO);
    esp_log_level_set("debug", ESP_LOG_INFO);
    
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