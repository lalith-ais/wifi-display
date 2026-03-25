#ifndef GT911_SERVICE_H
#define GT911_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Touch event structure - matches esp_lcd_touch_point_data_t */
typedef struct {
    uint8_t touches;
    esp_lcd_touch_point_data_t points[5];
} gt911_touch_event_t;

/* Public API */
void gt911_service_start(void);
void gt911_service_stop(void);
QueueHandle_t gt911_service_get_queue(void);
bool gt911_service_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* GT911_SERVICE_H */