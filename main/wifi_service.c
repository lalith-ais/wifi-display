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
} wifi_service_ctx_t;

static wifi_service_ctx_t s_ctx = {0};

/* Callbacks from wifi_setup */
static void connected_cb(void)
{
    if (!s_ctx.event_queue) return;
    wifi_service_message_t msg = { .type = WIFI_EVENT_CONNECTED };
    xQueueSend(s_ctx.event_queue, &msg, 0);
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
    ESP_LOGI(TAG, "Starting");
    
    s_ctx.event_queue = xQueueCreate(10, sizeof(wifi_service_message_t));
    s_ctx.is_running = true;
    s_ctx.task_handle = xTaskGetCurrentTaskHandle();
    
    if (!s_ctx.event_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        vTaskDelete(NULL);
        return;
    }
    
    /* Initialize WiFi hardware */
    if (wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed");
        wifi_service_message_t msg = { .type = WIFI_EVENT_ERROR };
        msg.data.error.error = ESP_FAIL;
        xQueueSend(s_ctx.event_queue, &msg, 0);
        vTaskDelete(NULL);
        return;
    }
    
    /* Set callbacks */
    wifi_set_connected_cb(connected_cb);
    wifi_set_disconnected_cb(disconnected_cb);
    wifi_set_got_ip_cb(got_ip_cb);
    
    /* Get MAC for node ID */
    wifi_get_mac(s_ctx.mac);
    
    /* Connect to WiFi */
    const char *ssid = CONFIG_WIFI_SSID;
    const char *password = CONFIG_WIFI_PASSWORD;
    
    if (wifi_start(ssid, password) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed");
        wifi_service_message_t msg = { .type = WIFI_EVENT_ERROR };
        msg.data.error.error = ESP_FAIL;
        xQueueSend(s_ctx.event_queue, &msg, 0);
        vTaskDelete(NULL);
        return;
    }
    
    wifi_service_message_t started = { .type = WIFI_EVENT_STARTED };
    xQueueSend(s_ctx.event_queue, &started, 0);
    
    /* Main loop */
    while (s_ctx.is_running) {
        wifi_service_message_t msg;
        
        if (xQueueReceive(s_ctx.event_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (msg.type) {
                case WIFI_EVENT_CONNECTED:
                    ESP_LOGI(TAG, "Connected to AP");
                    s_ctx.is_connected = true;
                    break;
                case WIFI_EVENT_DISCONNECTED:
                    ESP_LOGI(TAG, "Disconnected");
                    s_ctx.is_connected = false;
                    s_ctx.has_ip = false;
                    break;
                case WIFI_EVENT_GOT_IP:
                    ESP_LOGI(TAG, "Got IP: %s", msg.data.got_ip.ip);
                    s_ctx.has_ip = true;
                    break;
                case WIFI_EVENT_STOP_REQUESTED:
                    s_ctx.is_running = false;
                    break;
                default:
                    break;
            }
        }
        
        /* Heartbeat for supervisor */
        supervisor_heartbeat("wifi");
    }
    
    /* Cleanup */
    wifi_stop();
    if (s_ctx.event_queue) {
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
    }
    
    ESP_LOGI(TAG, "Stopped");
    vTaskDelete(NULL);
}

/* Public API */
void wifi_service_start(void)
{
	    if (s_ctx.is_running || s_ctx.task_handle) {
        ESP_LOGW(TAG, "Already running");
        return;
    }
    
    if (s_ctx.task_handle) {
        ESP_LOGW(TAG, "Already running");
        return;
    }
    xTaskCreate(wifi_service_task, "wifi-service",
                8192, NULL, PRIO_WIFI_SERVICE, &s_ctx.task_handle);
}

void wifi_service_stop(void)
{
	if (!s_ctx.is_running) return;
	
    if (s_ctx.event_queue) {
        wifi_service_message_t msg = { .type = WIFI_EVENT_STOP_REQUESTED };
        xQueueSend(s_ctx.event_queue, &msg, pdMS_TO_TICKS(200));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
     s_ctx.is_running = false;
    s_ctx.task_handle = NULL;
}

QueueHandle_t wifi_service_get_queue(void) { return s_ctx.event_queue; }
bool wifi_service_is_connected(void) { return s_ctx.is_connected; }
bool wifi_service_has_ip(void) { return s_ctx.has_ip; }

const char *wifi_service_get_ip(void)
{
    static char ip[16] = {0};
    if (s_ctx.has_ip) {
        wifi_get_ip(ip, sizeof(ip));
    }
    return ip;
}

const uint8_t *wifi_service_get_mac(void) { return s_ctx.mac; }
