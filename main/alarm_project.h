#ifndef ALARM_PROJECT_H
#define ALARM_PROJECT_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define TASK_COUNT  5
#define QUEUE_COUNT 2

#define SENSOR_TEMPERATURE 1
#define SENSOR_GAS         2

#define NORMAL      0
#define HIGH_TEMP   1
#define HIGH_GAS    2
#define HIGH_BOTH   3
#define SENSOR_ERROR 4
#define STARTING    5

/* Chi 2 struct phang de gui du lieu qua 2 queue. Khong struct long/union. */
typedef struct {
    int sensor;          // 1: DHT, 2: MQ2
    float value;         // nhiet do hoac ADC raw
    float humidity;      // chi dung voi DHT
    bool valid;
    int64_t time_ms;     // thoi diem lay mau
} SensorData;

typedef struct {
    float temperature;
    float humidity;
    int gas;
    bool temperature_valid;
    bool gas_valid;
    int state;           // 0..5, theo cac define phia tren
} AlarmData;

extern QueueHandle_t SensorDataQueue;
extern QueueHandle_t AlarmQueue;
extern int64_t start_time_ms;

void TemperatureTask(void *parameter);
void GasSensorTask(void *parameter);
void MonitoringTask(void *parameter);
void AlarmTask(void *parameter);
void OLEDTask(void *parameter);

#endif
