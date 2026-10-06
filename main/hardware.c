#include <stdio.h>
#include <string.h>
#include "hardware.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if OLED_HEIGHT != 32 && OLED_HEIGHT != 64
#error "OLED_HEIGHT must be 32 or 64"
#endif

static adc_oneshot_unit_handle_t adc;
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t lcd, oled;
static bool lcd_ready, oled_ready;
static portMUX_TYPE dht_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t framebuffer[128 * OLED_HEIGHT / 8];

/* Bounded polling. A missing sensor never blocks forever. */
static bool wait_level(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(DHT_GPIO) != level) {
        if (esp_timer_get_time() - start >= timeout_us) return false;
    }
    return true;
}

// Đọc từ cảm biến DHT:
bool hardware_read_dht(float *temperature, float *humidity)
{
    uint8_t bytes[5] = {0};
    bool ok = false;
    /* Open drain: 0 drives low, 1 releases line to external 3.3V pull-up. */
    gpio_set_level(DHT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    /* Mask preemption only during the bounded ~4ms data transaction. */
    portENTER_CRITICAL(&dht_lock);
    gpio_set_level(DHT_GPIO, 1);
    esp_rom_delay_us(30);
    if (!wait_level(0, 100) || !wait_level(1, 120) || !wait_level(0, 120))
        goto done;
    for (int bit = 0; bit < 40; ++bit) {
        if (!wait_level(1, 100)) goto done;
        int64_t rise = esp_timer_get_time();
        if (!wait_level(0, 120)) goto done;
        bytes[bit / 8] = (uint8_t)((bytes[bit / 8] << 1) |
                          (esp_timer_get_time() - rise > 40));
    }
    ok = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) == bytes[4];
done:
    gpio_set_level(DHT_GPIO, 1);
    portEXIT_CRITICAL(&dht_lock);
    if (!ok) return false;
    // Lấy dữ liệu độ ẩm:
    *humidity = bytes[0] + bytes[1] * 0.1f;                   
    // Lấy dữ liệu nhiệt độ:
    *temperature = (bytes[2] & 0x7f) + (bytes[3] & 0x7f) * 0.1f;
    if (bytes[3] & 0x80) *temperature = -*temperature;
    return *humidity >= 0 && *humidity <= 100 &&
           *temperature >= 0 && *temperature <= 50;
}

// Đọc dữ liệu từ MQ2:
bool hardware_read_mq2(int *raw)
{
    int sum = 0;
    for (int i = 0; i < 16; ++i) {
        int sample;
        if (adc_oneshot_read(adc, MQ2_ADC_CHANNEL, &sample) != ESP_OK) return false;
        sum += sample;
    }
    *raw = sum / 16;
    return true; /* ADC success does not prove MQ2 is connected/healthy. */
}

// Cảnh báo:
void hardware_alarm(bool led, bool buzzer)
{
    // Set còi:
    gpio_set_level(BUZZER_GPIO, buzzer ? BUZZER_ON_LEVEL : !BUZZER_ON_LEVEL);
    // Set đèn LED:
    if (LED_GPIO >= 0)
        gpio_set_level((gpio_num_t)LED_GPIO, led ? LED_ON_LEVEL : !LED_ON_LEVEL);
}

/* Common backpack wiring: P0=RS P1=RW P2=E P3=BL P4..P7=D4..D7. */
static esp_err_t lcd_nibble(uint8_t value, bool rs)
{
    uint8_t base = (value & 0xf0) | 0x08 | (rs ? 1 : 0);
    uint8_t sequence[] = {base, (uint8_t)(base | 0x04), base};
    return i2c_master_transmit(lcd, sequence, sizeof(sequence), 100);
}

static esp_err_t lcd_byte(uint8_t value, bool rs)
{
    esp_err_t err = lcd_nibble(value, rs);
    if (err == ESP_OK) err = lcd_nibble((uint8_t)(value << 4), rs);
    esp_rom_delay_us(50);
    if (!rs && (value == 1 || value == 2)) vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}

static bool lcd_init(void)
{
    if (i2c_master_probe(bus, LCD_I2C_ADDR, 100) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    if (lcd_nibble(0x30, false) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (lcd_nibble(0x30, false) != ESP_OK) return false;
    esp_rom_delay_us(150);
    if (lcd_nibble(0x30, false) != ESP_OK) return false;
    if (lcd_nibble(0x20, false) != ESP_OK) return false;
    const uint8_t commands[] = {0x28, 0x08, 0x01, 0x06, 0x0c};
    for (size_t i = 0; i < sizeof(commands); ++i)
        if (lcd_byte(commands[i], false) != ESP_OK) return false;
    return true;
}

static bool lcd_line(int row, const char *text)
{
    if (lcd_byte(row == 0 ? 0x80 : 0xc0, false) != ESP_OK) return false;
    size_t length = strlen(text);
    for (int col = 0; col < 16; ++col)
        if (lcd_byte(col < length ? (uint8_t)text[col] : ' ', true) != ESP_OK)
            return false;
    return true;
}

static esp_err_t oled_commands(const uint8_t *commands, size_t count)
{
    uint8_t packet[40] = {0};
    if (count > sizeof(packet) - 1) return ESP_ERR_INVALID_SIZE;
    memcpy(packet + 1, commands, count);
    return i2c_master_transmit(oled, packet, count + 1, 100);
}

static bool oled_init(void)
{
    if (i2c_master_probe(bus, OLED_I2C_ADDR, 100) != ESP_OK) return false;
    const uint8_t commands[] = {
        0xae, 0xd5, 0x80, 0xa8, OLED_HEIGHT - 1, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda,
        OLED_HEIGHT == 64 ? 0x12 : 0x02, 0x81, 0x7f, 0xd9, 0xf1,
        0xdb, 0x40, 0xa4, 0xa6, 0xaf
    };
    return oled_commands(commands, sizeof(commands)) == ESP_OK;
}

/* Compact column font: only digits / uppercase / punctuation used below. */
static const uint8_t digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e}
};
static const uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},
    {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,1},
    {0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};

static void oled_text(int page, const char *text)
{
    if (page < 0 || page >= OLED_HEIGHT / 8) return;
    for (int col = 0; text[col] && col < 21; ++col) {
        char c = text[col];
        uint8_t special[5] = {0};
        const uint8_t *glyph = special;
        if (c >= '0' && c <= '9') glyph = digits[c - '0'];
        else if (c >= 'A' && c <= 'Z') glyph = letters[c - 'A'];
        else if (c == '-') memset(special, 8, 5);
        else if (c == '.') special[2] = 0x60;
        else if (c == ':') special[2] = 0x36;
        else if (c == '%') {
            const uint8_t percent[5] = {0x63,0x13,8,0x64,0x63};
            memcpy(special, percent, 5);
        }
        memcpy(framebuffer + page * 128 + col * 6, glyph, 5);
    }
}

static bool oled_flush(void)
{
    const uint8_t range[] = {0x21,0,127,0x22,0,OLED_HEIGHT / 8 - 1};
    if (oled_commands(range, sizeof(range)) != ESP_OK) return false;
    for (int page = 0; page < OLED_HEIGHT / 8; ++page) {
        uint8_t packet[129];
        packet[0] = 0x40;
        memcpy(packet + 1, framebuffer + 128 * page, 128);
        if (i2c_master_transmit(oled, packet, sizeof(packet), 100) != ESP_OK)
            return false;
    }
    return true;
}

static const char *state_text(int state)
{
    switch (state) {
    case NORMAL: return "NORMAL";
    case HIGH_TEMP: return "HIGH TEMP";
    case HIGH_GAS: return "HIGH GAS";
    case HIGH_BOTH: return "HIGH BOTH";
    case SENSOR_ERROR: return "SENS ERR";
    default: return "STARTING";
    }
}

/* Called by OLEDTask ONLY. No application-level I2C mutex is required. */
void hardware_display(const AlarmData *data)
{
    static int64_t retry_ms = -5000;
    int64_t now = esp_timer_get_time() / 1000;
    if (now - retry_ms >= 5000) {
        if (!lcd_ready) lcd_ready = lcd_init();
        if (!oled_ready) oled_ready = oled_init();
        retry_ms = now;
    }
    char temperature[12] = "--", humidity[12] = "--", gas[12] = "--";
    if (data->temperature_valid) {
        snprintf(temperature, sizeof(temperature), "%.1f", data->temperature);
        snprintf(humidity, sizeof(humidity), "%.0f", data->humidity);
    }
    if (data->gas_valid) snprintf(gas, sizeof(gas), "%d", data->gas);
    char line[64];
    if (lcd_ready) {
        snprintf(line, sizeof(line), "T:%sC H:%s%%", temperature, humidity);
        lcd_ready = lcd_line(0, line);
        if (lcd_ready) {
            snprintf(line, sizeof(line), "G:%-4s %-9s", gas, state_text(data->state));
            lcd_ready = lcd_line(1, line);
        }
        if (!lcd_ready) ESP_LOGW("LCD", "Write failed; retry in 5s");
    }
    if (oled_ready) {
        memset(framebuffer, 0, sizeof(framebuffer));
        snprintf(line, sizeof(line), "TEMP:%s C", temperature);
        oled_text(0, line);
        snprintf(line, sizeof(line), "HUM:%s %%", humidity);
        oled_text(1, line);
        snprintf(line, sizeof(line), "MQ2:%s RAW", gas);
        oled_text(2, line);
        oled_text(3, state_text(data->state));
        oled_ready = oled_flush();
        if (!oled_ready) ESP_LOGW("OLED", "Write failed; retry in 5s");
    }
}

esp_err_t hardware_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = 1ULL << BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT
    };
    /* Preload off level before enabling output (active-low modules included). */
    gpio_set_level(BUZZER_GPIO, !BUZZER_ON_LEVEL);
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) return err;
    if (LED_GPIO >= 0) {
        out.pin_bit_mask = 1ULL << (unsigned)(LED_GPIO & 63);
        gpio_set_level((gpio_num_t)LED_GPIO, !LED_ON_LEVEL);
        err = gpio_config(&out);
        if (err != ESP_OK) return err;
    }
    hardware_alarm(false, false);
    gpio_set_level(DHT_GPIO, 1);
    gpio_config_t dht = {
        .pin_bit_mask = 1ULL << DHT_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    err = gpio_config(&dht);
    if (err != ESP_OK) return err;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    err = adc_oneshot_new_unit(&unit, &adc);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    err = adc_oneshot_config_channel(adc, MQ2_ADC_CHANNEL, &channel);
    if (err != ESP_OK) return err;
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    err = i2c_new_master_bus(&config, &bus);
    if (err != ESP_OK) return err;
    for (int address = 0x08; address < 0x78; ++address)
        if (i2c_master_probe(bus, address, 20) == ESP_OK)
            ESP_LOGI("I2C", "Found address 0x%02X", address);
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_I2C_ADDR,
        .scl_speed_hz = 100000
    };
    err = i2c_master_bus_add_device(bus, &dev, &lcd);
    if (err != ESP_OK) return err;
    dev.device_address = OLED_I2C_ADDR;
    err = i2c_master_bus_add_device(bus, &dev, &oled);
    if (err != ESP_OK) return err;
    /* Actual LCD/OLED initialization occurs in OLEDTask, after startup. */
    return ESP_OK;
}
