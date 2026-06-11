#include "canbus/canbus.h"
#include "canbus/canbus_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/ui.h"

#if CANBUS_USB_RECOVERY_MODE
static void usb_recovery_task(void *arg)
{
    (void)arg;
    static const char *TAG = "usb_recovery";
    uint32_t counter = 0;

    ESP_LOGI(TAG, "USB recovery firmware booted");
    while (1) {
        ESP_LOGI(TAG, "heartbeat %lu", (unsigned long)counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

static void app_startup_task(void *arg)
{
    (void)arg;
    static const char *TAG = "canbus_app";
#if CANBUS_USB_RECOVERY_MODE
    ESP_LOGI(TAG, "Starting USB recovery mode");
    usb_recovery_task(NULL);
#endif
#if !CANBUS_MINIMAL_APP_MODE
    ui_init();
#else
    ESP_LOGI(TAG, "Starting in minimal CAN diagnostic mode (UI disabled)");
#endif
    if (canbus_start() != ESP_OK) {
        ESP_LOGE(TAG, "CANBUS start failed");
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    static const char *TAG = "canbus_app";
    BaseType_t ok = xTaskCreatePinnedToCore(app_startup_task, "canbus_startup", 10240, NULL, 5, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to start app startup task");
    }
}
