idf_component_register(
    SRCS "main.c" "alarm_project.c" "hardware.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_timer esp_driver_gpio
             esp_driver_i2c esp_adc esp_rom
)