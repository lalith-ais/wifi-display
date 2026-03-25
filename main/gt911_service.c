#include "gt911_service.h"
#include "supervisor.h"
#include "priorities.h"
#include "esp_log.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "gt911";

/* Touch hardware configuration (from your working code) */
#define PIN_TOUCH_SCL               8
#define PIN_TOUCH_SDA               7
#define PIN_TOUCH_RST               35
#define PIN_TOUCH_INT               3
#define TOUCH_I2C_PORT              I2C_NUM_0
#define TOUCH_I2C_FREQ_HZ           400000

/* Display resolution (will be set later when display is added) */
#define DISPLAY_WIDTH               480
#define DISPLAY_HEIGHT              800

typedef struct {
    TaskHandle_t task_handle;
    QueueHandle_t event_queue;
    volatile bool is_running;
    esp_lcd_touch_handle_t touch;
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_io_handle_t tp_io;
} gt911_ctx_t;

static gt911_ctx_t s_ctx = {0};

/* GT911 initialization - exactly from your working code */
static esp_err_t gt911_hardware_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller (SDA=%d SCL=%d RST=%d INT=%d)",
             PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_RST, PIN_TOUCH_INT);

    /* I2C master bus configuration */
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port          = TOUCH_I2C_PORT,
        .sda_io_num        = PIN_TOUCH_SDA,
        .scl_io_num        = PIN_TOUCH_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &s_ctx.i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* I2C panel IO configuration */
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_FREQ_HZ;
    
    ret = esp_lcd_new_panel_io_i2c(s_ctx.i2c_bus, &tp_io_cfg, &s_ctx.tp_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel IO creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* GT911 specific configuration */
    esp_lcd_touch_io_gt911_config_t gt911_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,  // 0x5D
    };

    /* Touch configuration */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = DISPLAY_WIDTH,
        .y_max        = DISPLAY_HEIGHT,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data  = &gt911_cfg,
    };

    /* Create GT911 touch driver */
    ret = esp_lcd_touch_new_i2c_gt911(s_ctx.tp_io, &tp_cfg, &s_ctx.touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 driver creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GT911 initialized successfully (480x800)");
    return ESP_OK;
}

/* Main GT911 task */
static void gt911_task(void *arg)
{
    ESP_LOGI(TAG, "GT911 task starting");
    
    /* Create event queue */
    s_ctx.event_queue = xQueueCreate(20, sizeof(gt911_touch_event_t));
    if (!s_ctx.event_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        vTaskDelete(NULL);
        return;
    }
    
    /* Initialize touch hardware */
    if (gt911_hardware_init() != ESP_OK) {
        ESP_LOGE(TAG, "Hardware init failed");
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    s_ctx.is_running = true;
    ESP_LOGI(TAG, "GT911 service running - tap screen to see events");
    
    gt911_touch_event_t touch_data;
    esp_lcd_touch_point_data_t point;
    uint8_t point_cnt;
    
    while (s_ctx.is_running) {
        /* Read touch data */
        esp_lcd_touch_read_data(s_ctx.touch);
        
        /* Get touch points using the correct API */
        esp_lcd_touch_get_data(s_ctx.touch, &point, &point_cnt, 1);
        
        if (point_cnt > 0) {
            touch_data.touches = point_cnt;
            touch_data.points[0].x = point.x;
            touch_data.points[0].y = point.y;
            touch_data.points[0].strength = point.strength;
            
            /* Send to queue */
            if (xQueueSend(s_ctx.event_queue, &touch_data, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Queue full - dropping touch event");
            }
            
            ESP_LOGI(TAG, "Touch: x=%d, y=%d (strength=%d, touches=%d)", 
                     touch_data.points[0].x, 
                     touch_data.points[0].y,
                     touch_data.points[0].strength,
                     point_cnt);
        }
        
        supervisor_heartbeat("gt911");
        vTaskDelay(pdMS_TO_TICKS(20));  /* 50Hz polling */
    }
    
    /* Cleanup */
    if (s_ctx.touch) {
        esp_lcd_touch_del(s_ctx.touch);
    }
    if (s_ctx.tp_io) {
        esp_lcd_panel_io_del(s_ctx.tp_io);
    }
    if (s_ctx.i2c_bus) {
        i2c_del_master_bus(s_ctx.i2c_bus);
    }
    if (s_ctx.event_queue) {
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
    }
    
    ESP_LOGI(TAG, "GT911 task stopped");
    vTaskDelete(NULL);
}

/* Public API */
void gt911_service_start(void)
{
    if (s_ctx.is_running) {
        ESP_LOGW(TAG, "Already running");
        return;
    }
    
    xTaskCreate(gt911_task, "gt911", 4096, NULL, 
                PRIO_GT911_SERVICE, &s_ctx.task_handle);
}

void gt911_service_stop(void)
{
    if (!s_ctx.is_running) return;
    s_ctx.is_running = false;
    if (s_ctx.task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_ctx.task_handle = NULL;
    }
}

QueueHandle_t gt911_service_get_queue(void)
{
    return s_ctx.event_queue;
}

bool gt911_service_is_running(void)
{
    return s_ctx.is_running;
}