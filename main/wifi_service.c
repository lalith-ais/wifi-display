#include "wifi_service.h"
#include "wifi_setup.h"
#include "supervisor.h"
#include "priorities.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "wifi-service";

typedef struct {
    QueueHandle_t event_queue;
    volatile bool is_running;
    volatile bool is_connected;
    volatile bool has_ip;
    TaskHandle_t task_handle;
    uint8_t mac[6];
    uint8_t retry_count;
} wifi_service_ctx_t;

static wifi_service_ctx_t s_ctx = {0};

/* Callbacks from wifi_setup */
static void connected_cb(void)
{
    if (!s_ctx.event_queue) return;
    wifi_service_message_t msg = { .type = WIFI_EVENT_CONNECTED };
    xQueueSend(s_ctx.event_queue, &msg, 0);
    s_ctx.retry_count = 0; /* Reset retry count on successful connection */
}

static void disconnected_cb(void)
{
    if (!s_ctx.event_queue) return;
    wifi_service_message_t msg = { .type = WIFI_EVENT_DISCONNECTED };
    xQueueSend(s_ctx.event_queue, &msg, 0);
}

static void got_ip_cb(void)
{
    if (!s_ctx.event_queue) return;
    wifi_service_message_t msg = { .type = WIFI_EVENT_GOT_IP };
    wifi_get_ip(msg.data.got_ip.ip, sizeof(msg.data.got_ip.ip));
    xQueueSend(s_ctx.event_queue, &msg, 0);
}

/* Main service task */
static void wifi_service_task(void *arg)
{
    ESP_LOGI(TAG, "WiFi service task starting");
    
    s_ctx.event_queue = xQueueCreate(10, sizeof(wifi_service_message_t));
    s_ctx.is_running = true;
    s_ctx.task_handle = xTaskGetCurrentTaskHandle();
    s_ctx.retry_count = 0;
    
    if (!s_ctx.event_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        vTaskDelete(NULL);
        return;
    }
    
    /* Initialize WiFi hardware (including hosted transport) */
    esp_err_t init_ret = wifi_init();
    if (init_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(init_ret));
        wifi_service_message_t msg = { .type = WIFI_EVENT_ERROR };
        msg.data.error.error = init_ret;
        xQueueSend(s_ctx.event_queue, &msg, 0);
        s_ctx.is_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    /* Set callbacks */
    wifi_set_connected_cb(connected_cb);
    wifi_set_disconnected_cb(disconnected_cb);
    wifi_set_got_ip_cb(got_ip_cb);
    
    /* Get MAC for node ID */
    wifi_get_mac(s_ctx.mac);
    ESP_LOGI(TAG, "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_ctx.mac[0], s_ctx.mac[1], s_ctx.mac[2],
             s_ctx.mac[3], s_ctx.mac[4], s_ctx.mac[5]);
    
    /* Connect to WiFi */
    const char *ssid = CONFIG_WIFI_SSID;
    const char *password = CONFIG_WIFI_PASSWORD;
    
    if (strlen(ssid) == 0) {
        ESP_LOGE(TAG, "WiFi SSID not configured!");
        wifi_service_message_t msg = { .type = WIFI_EVENT_ERROR };
        msg.data.error.error = ESP_ERR_INVALID_ARG;
        xQueueSend(s_ctx.event_queue, &msg, 0);
        s_ctx.is_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    esp_err_t start_ret = wifi_start(ssid, password);
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(start_ret));
        wifi_service_message_t msg = { .type = WIFI_EVENT_ERROR };
        msg.data.error.error = start_ret;
        xQueueSend(s_ctx.event_queue, &msg, 0);
        s_ctx.is_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    wifi_service_message_t started = { .type = WIFI_EVENT_STARTED };
    xQueueSend(s_ctx.event_queue, &started, 0);
    
    ESP_LOGI(TAG, "WiFi service running, waiting for connection...");
    
    /* Main loop */
    bool reconnecting = false;
    TickType_t last_reconnect_attempt = 0;
    
    while (s_ctx.is_running) {
        wifi_service_message_t msg;
        
        if (xQueueReceive(s_ctx.event_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (msg.type) {
                case WIFI_EVENT_CONNECTED:
                    ESP_LOGI(TAG, "Connected to AP");
                    s_ctx.is_connected = true;
                    reconnecting = false;
                    break;
                    
                case WIFI_EVENT_DISCONNECTED:
                    ESP_LOGW(TAG, "Disconnected from AP");
                    s_ctx.is_connected = false;
                    s_ctx.has_ip = false;
                    break;
                    
                case WIFI_EVENT_GOT_IP:
                    ESP_LOGI(TAG, "Got IP: %s", msg.data.got_ip.ip);
                    ESP_LOGI(TAG, "Hostname: %s", wifi_service_get_hostname());
                    s_ctx.has_ip = true;
                    break;
                    
                case WIFI_EVENT_STOP_REQUESTED:
                    s_ctx.is_running = false;
                    break;
                    
                case WIFI_EVENT_ERROR:
                    ESP_LOGE(TAG, "WiFi error: %s", esp_err_to_name(msg.data.error.error));
                    break;
                    
                default:
                    break;
            }
        }
        
        /* Auto-reconnect logic with exponential backoff */
        if (!s_ctx.is_connected && s_ctx.is_running && !reconnecting) {
            TickType_t now = xTaskGetTickCount();
            uint32_t backoff_ms = 1000 * (1 << (s_ctx.retry_count > 5 ? 5 : s_ctx.retry_count));
            
            if (now - last_reconnect_attempt >= pdMS_TO_TICKS(backoff_ms)) {
                if (s_ctx.retry_count < CONFIG_WIFI_MAX_RETRY) {
                    ESP_LOGI(TAG, "Reconnecting... (attempt %d/%d)", 
                             s_ctx.retry_count + 1, CONFIG_WIFI_MAX_RETRY);
                    reconnecting = true;
                    esp_err_t ret = wifi_reconnect();
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(ret));
                        reconnecting = false;
                        s_ctx.retry_count++;
                    }
                    last_reconnect_attempt = now;
                } else {
                    ESP_LOGE(TAG, "Max retry count reached, stopping reconnection attempts");
                    /* Supervisor will handle restart if essential */
                    s_ctx.is_running = false;
                }
            }
        } else if (reconnecting && s_ctx.is_connected) {
            reconnecting = false;
            s_ctx.retry_count = 0;
        }
        
        /* Heartbeat for supervisor */
        supervisor_heartbeat("wifi");
    }
    
    /* Cleanup */
    ESP_LOGI(TAG, "Stopping WiFi...");
    wifi_stop();
    if (s_ctx.event_queue) {
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
    }
    
    ESP_LOGI(TAG, "WiFi service stopped");
    s_ctx.task_handle = NULL;
    vTaskDelete(NULL);
}

/* Public API */
void wifi_service_start(void)
{
    if (s_ctx.is_running || s_ctx.task_handle != NULL) {
        ESP_LOGW(TAG, "WiFi service already running");
        return;
    }
    
    ESP_LOGI(TAG, "Starting WiFi service");
    
    BaseType_t ret = xTaskCreate(
        wifi_service_task, 
        "wifi-service",
        8192, 
        NULL, 
        PRIO_WIFI_SERVICE, 
        &s_ctx.task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi service task");
    } else {
        ESP_LOGI(TAG, "WiFi service task created (priority %d)", PRIO_WIFI_SERVICE);
    }
}

void wifi_service_stop(void)
{
    if (!s_ctx.is_running && s_ctx.task_handle == NULL) {
        ESP_LOGW(TAG, "WiFi service not running");
        return;
    }
    
    ESP_LOGI(TAG, "Stopping WiFi service");
    s_ctx.is_running = false;
    
    if (s_ctx.event_queue) {
        wifi_service_message_t msg = { .type = WIFI_EVENT_STOP_REQUESTED };
        xQueueSend(s_ctx.event_queue, &msg, pdMS_TO_TICKS(200));
    }
    
    /* Wait for task to finish */
    if (s_ctx.task_handle != NULL) {
        TickType_t timeout = pdMS_TO_TICKS(3000);
        while (s_ctx.task_handle != NULL && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            timeout -= pdMS_TO_TICKS(100);
        }
        if (s_ctx.task_handle != NULL) {
            ESP_LOGW(TAG, "Task didn't stop, force deleting");
            vTaskDelete(s_ctx.task_handle);
            s_ctx.task_handle = NULL;
        }
    }
    
    ESP_LOGI(TAG, "WiFi service stopped");
}

QueueHandle_t wifi_service_get_queue(void) 
{ 
    return s_ctx.event_queue; 
}

bool wifi_service_is_connected(void) 
{ 
    return s_ctx.is_connected; 
}

bool wifi_service_has_ip(void) 
{ 
    return s_ctx.has_ip; 
}

bool wifi_service_is_running(void)
{
    return s_ctx.is_running;
}

const char *wifi_service_get_ip(void)
{
    static char ip[16] = {0};
    if (s_ctx.has_ip) {
        wifi_get_ip(ip, sizeof(ip));
    } else {
        strcpy(ip, "0.0.0.0");
    }
    return ip;
}

const uint8_t *wifi_service_get_mac(void) 
{ 
    return s_ctx.mac; 
}

const char *wifi_service_get_hostname(void)
{
    return wifi_get_hostname();
}