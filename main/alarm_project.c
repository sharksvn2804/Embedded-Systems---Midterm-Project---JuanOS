#include "esp_log.h"
#include "esp_timer.h"
#include "alarm_project.h"
#include "hardware.h"

/* Hardware version: same 5 tasks / 2 queues. Only OLEDTask writes I2C. */

void TemperatureTask(void *parameter)
{
    (void)parameter;
    SensorData data = {0};
    data.sensor = SENSOR_TEMPERATURE;
    vTaskDelay(pdMS_TO_TICKS(1000)); // DHT power-up settling

    while (1) {
        data.valid = hardware_read_dht(&data.value, &data.humidity);
        // Thông báo rằng DHT bị lỗi:
        if (!data.valid) ESP_LOGW("TEMP", "DHT11 timeout/checksum error");
        data.time_ms = esp_timer_get_time() / 1000;
        
        if (xQueueSend(SensorDataQueue, &data, pdMS_TO_TICKS(20)) != pdPASS) {
            ESP_LOGW("TEMP", "SensorDataQueue day, bo mau DHT");
        }
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

void GasSensorTask(void *parameter)
{
    (void)parameter;
    SensorData data = {0};
    data.sensor = SENSOR_GAS;

    while (1) {
        // Đọc cảm biến MQ2:
        int64_t now = esp_timer_get_time() / 1000;
        int raw = 0;
        bool read_ok = hardware_read_mq2(&raw);
        data.value = raw;
        data.valid = read_ok && (now - start_time_ms >= MQ2_WARMUP_MS);

        // Kiểm tra lỗi dọc ADC:
        if (!read_ok) ESP_LOGW("GAS", "ADC read error");
        data.time_ms = now;

        if (xQueueSend(SensorDataQueue, &data, pdMS_TO_TICKS(20)) != pdPASS) {
            ESP_LOGW("GAS", "SensorDataQueue day, bo mau MQ2");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void MonitoringTask(void *parameter)
{
    (void)parameter;
    SensorData sensor;
    AlarmData data = {0};
    bool temperature_alarm = false;
    bool gas_alarm = false;
    bool temperature_valid = false;
    bool gas_valid = false;
    int64_t temperature_time = start_time_ms;
    int64_t gas_time = start_time_ms;

    while (1) {
        // Chi MonitoringTask lay mau RA KHOI SensorDataQueue.
        if (xQueueReceive(SensorDataQueue, &sensor, pdMS_TO_TICKS(100)) == pdPASS) {
            if (sensor.sensor == SENSOR_TEMPERATURE) {
                temperature_valid = sensor.valid;
                temperature_time = sensor.time_ms;
                if (sensor.valid) {
                    data.temperature = sensor.value;
                    data.humidity = sensor.humidity;
                    // Hai nguong tranh bat/tat lien tuc gan nguong.
                    if (data.temperature >= TEMP_ON) temperature_alarm = true;
                    else if (data.temperature <= TEMP_OFF) temperature_alarm = false;
                }
            } else if (sensor.sensor == SENSOR_GAS) {
                gas_valid = sensor.valid;
                gas_time = sensor.time_ms;
                if (sensor.valid) {
                    data.gas = (int)sensor.value;
                    if (data.gas >= GAS_ON) gas_alarm = true;
                    else if (data.gas <= GAS_OFF) gas_alarm = false;
                }
            }
        }

        int64_t now = esp_timer_get_time() / 1000;
        data.temperature_valid = temperature_valid && (now - temperature_time <= 8000);
        data.gas_valid = gas_valid && (now - gas_time <= 2000);

        // Mau loi/qua han KHONG xoa canh bao da bat.
        // Uu tien hien canh bao nguy hiem; OLED van hien loi qua valid.
        if (temperature_alarm && gas_alarm) data.state = HIGH_BOTH;
        else if (temperature_alarm) data.state = HIGH_TEMP;
        else if (gas_alarm) data.state = HIGH_GAS;
        else if (!data.temperature_valid || !data.gas_valid) {
            if (now - start_time_ms < 5000 ||
                (data.temperature_valid && now - start_time_ms < MQ2_WARMUP_MS))
                data.state = STARTING;
            else data.state = SENSOR_ERROR;
        } else data.state = NORMAL;

        // Queue dai 1: ghi de bang BAN TONG HOP MOI NHAT.
        xQueueOverwrite(AlarmQueue, &data);
    }
}

void AlarmTask(void *parameter)
{
    (void)parameter;
    AlarmData data;
    bool led = false;
    bool buzzer = false;
    bool previous_led = false;
    bool previous_buzzer = false;
    int previous_state = -1;

    while (1) {
        // Peek chi COPY, KHONG lay phan tu ra khoi queue.
        // OLEDTask vi vay van doc duoc cung trang thai.
        if (xQueuePeek(AlarmQueue, &data, pdMS_TO_TICKS(100)) == pdPASS) {
            int64_t now = esp_timer_get_time() / 1000;
            led = false;
            buzzer = false;

            if (data.state == HIGH_TEMP || data.state == HIGH_GAS) {
                led = (now % 1000 < 500);
                buzzer = led;
            } else if (data.state == HIGH_BOTH) {
                led = (now % 400 < 200);
                buzzer = led;
            } else if (data.state == SENSOR_ERROR) {
                led = true;
                buzzer = (now % 3000 < 100);
            }

            if (data.state != previous_state) {
                ESP_LOGI("ALARM", "State=%d (0=NORMAL 1=TEMP 2=GAS 3=BOTH 4=ERROR 5=START)", data.state);
                previous_state = data.state;
            }
            if (led != previous_led || buzzer != previous_buzzer) {
                ESP_LOGI("ALARM", "LED=%d BUZZER=%d", led, buzzer);
                previous_led = led;
                previous_buzzer = buzzer;
            }
            hardware_alarm(led, buzzer);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void OLEDTask(void *parameter)
{
    (void)parameter;
    AlarmData data;

    while (1) {
        // Peek cung AlarmQueue voi AlarmTask: KHONG dung Receive tai day.
        if (xQueuePeek(AlarmQueue, &data, pdMS_TO_TICKS(100)) == pdPASS) {
            if (data.temperature_valid) {
                ESP_LOGI("OLED", "Temperature=%.1f C, Humidity=%.1f %%", data.temperature, data.humidity);
            } else {
                ESP_LOGI("OLED", "Temperature=--, Humidity=-- (DHT chua co du lieu/loi)");
            }
            if (data.gas_valid) ESP_LOGI("OLED", "MQ2=%d, State=%d", data.gas, data.state);
            else ESP_LOGI("OLED", "MQ2=-- (chua co du lieu/loi), State=%d", data.state);
            hardware_display(&data);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
