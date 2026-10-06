#ifndef HARDWARE_H
#define HARDWARE_H
#include "esp_err.h"
#include "alarm_project.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

/* GPIO numbers, not physical header pin numbers. Check your board schematic. */
#define DHT_GPIO          GPIO_NUM_3        // GPIO3
#define MQ2_GPIO          GPIO_NUM_0        // GPIO0
#define MQ2_ADC_CHANNEL   ADC_CHANNEL_0 /* GPIO0 = ADC1_CH0 on ESP32-C3 */
#define I2C_SDA_GPIO      GPIO_NUM_4        // GPIO4
#define I2C_SCL_GPIO      GPIO_NUM_5        // GPIO5
#define BUZZER_GPIO       GPIO_NUM_6        // GPIO6
#define BUZZER_ON_LEVEL   1 /* set 0 for an active-low buzzer MODULE */
#define LED_GPIO          7 /* optional external LED; e.g. 7, otherwise -1 */
#define LED_ON_LEVEL      1
#define LCD_I2C_ADDR      0x27 /* often 0x27 or 0x3F; see boot I2C scan */
#define OLED_I2C_ADDR     0x3C /* often 0x3C or 0x3D */
#define OLED_HEIGHT       64  /* SSD1306: 128x64 or 128x32 */
#define MQ2_WARMUP_MS     60000 /* startup settling for demo, NOT first burn-in */
#define TEMP_ON           40.0f
#define TEMP_OFF          37.0f
#define GAS_ON            2200 /* raw ADC demo thresholds; calibrate on hardware */
#define GAS_OFF           1800

esp_err_t hardware_init(void);
bool hardware_read_dht(float *temperature, float *humidity);
bool hardware_read_mq2(int *raw);
void hardware_alarm(bool led, bool buzzer);
void hardware_display(const AlarmData *data);
#endif
