#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "alarm_project.h"
#include "hardware.h"

QueueHandle_t SensorDataQueue;
QueueHandle_t AlarmQueue;
int64_t start_time_ms;

void app_main(void)
{
    ESP_ERROR_CHECK(hardware_init());

    // Tao 2 queue truyen du lieu, khong dung semaphore/mutex.
    SensorDataQueue = xQueueCreate(16, sizeof(SensorData));
    AlarmQueue = xQueueCreate(1, sizeof(AlarmData));

    if (SensorDataQueue == NULL || AlarmQueue == NULL) {
        ESP_LOGE("MAIN", "Khong tao duoc queue");
        abort();
    }

    start_time_ms = esp_timer_get_time() / 1000;
    AlarmData initial = {0};
    initial.state = STARTING;
    xQueueOverwrite(AlarmQueue, &initial);

    ESP_LOGI("MAIN", "HARDWARE: %d tasks, %d queues", TASK_COUNT, QUEUE_COUNT);

    // ESP-IDF tinh stack theo byte. Moi task tao bang xTaskCreate rieng le.
    if (xTaskCreate(TemperatureTask, "TemperatureTask", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE("MAIN", "Loi tao TemperatureTask");
        abort();
    }
    if (xTaskCreate(GasSensorTask, "GasSensorTask", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE("MAIN", "Loi tao GasSensorTask");
        abort();
    }
    if (xTaskCreate(MonitoringTask, "MonitoringTask", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE("MAIN", "Loi tao MonitoringTask");
        abort();
    }
    if (xTaskCreate(AlarmTask, "AlarmTask", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE("MAIN", "Loi tao AlarmTask");
        abort();
    }
    if (xTaskCreate(OLEDTask, "OLEDTask", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE("MAIN", "Loi tao OLEDTask");
        abort();
    }
    ESP_LOGI("MAIN", "Da tao du 5 task");
}