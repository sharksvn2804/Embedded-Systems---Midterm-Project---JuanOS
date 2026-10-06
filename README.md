# JuanOS — Temperature and Gas Alarm System

An Embedded Systems midterm project built with **ESP32-C3**, **ESP-IDF**, and **FreeRTOS**. The system monitors temperature, humidity, and gas sensor readings, displays the latest information, and generates LED and buzzer alerts.

## Features

- Measure temperature and humidity using a **DHT11** sensor.
- Read gas sensor values from an **MQ2** through the ESP32-C3 ADC. Gas readings are **raw ADC values**, not calibrated ppm.
- Display sensor readings and system status on an **SSD1306 OLED** and an **I2C 16×2 LCD**.
- Activate an external LED and an active buzzer for high temperature, high gas readings, or both.
- Use separate activation and reset thresholds to avoid repeated alarm switching near a threshold.
- Detect invalid or stale sensor data and provide sensor error indications.
- Apply an MQ2 startup warm-up period and output diagnostic logs through **ESP-IDF Monitor**.
- Run five FreeRTOS tasks communicating through two queues. `MonitoringTask` receives sensor samples; `AlarmTask` and `OLEDTask` read the latest shared status without removing it from the queue.

## Download and Run

### 1. Requirements

- Git and an installed ESP-IDF development environment with ESP32-C3 support.
- An ESP32-C3 board and a USB data cable.
- DHT11, MQ2, SSD1306 OLED, I2C 16×2 LCD, external LED, and active buzzer.

ESP-IDF installation instructions: [ESP32-C3 Getting Started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/).

### 2. Download the project

```bash
git clone https://github.com/sharksvn2804/Embedded-Systems---Midterm-Project---JuanOS.git
cd Embedded-Systems---Midterm-Project---JuanOS
```

Alternatively, select **Code → Download ZIP** on GitHub, extract the archive, and open the extracted project folder.

### 3. Connect and configure the hardware

Check the GPIO definitions and alarm settings in `main/hardware.h` before flashing. The provided configuration uses:

| Connection | GPIO / setting |
| --- | --- |
| DHT11 data | GPIO3 |
| MQ2 analog input | GPIO0 / ADC1 channel 0 |
| LCD and OLED I2C SDA | GPIO4 |
| LCD and OLED I2C SCL | GPIO5 |
| Active buzzer control | GPIO6 |
| External LED | GPIO7 |
| LCD I2C address | `0x27` |
| OLED I2C address | `0x3C` |

Use a common ground. Scale the MQ2 analog output to the ADC input range; the project hardware uses a voltage divider with **22 kΩ from AO to the ADC node** and **10 kΩ from the ADC node to GND**. Connect GPIO0 to that node.

Adjust the pin assignments, display addresses, `TEMP_ON` / `TEMP_OFF`, and `GAS_ON` / `GAS_OFF` for your hardware. `MQ2_WARMUP_MS` is set to **60,000 ms** for startup settling in the demonstration; it does not replace initial sensor conditioning or calibration.

### 4. Build the firmware

Open an **ESP-IDF terminal** in the project root, where the top-level `CMakeLists.txt` is located. In VS Code, use **ESP-IDF: Open ESP-IDF Terminal**.

```bash
idf.py set-target esp32c3
idf.py build
```

### 5. Flash and monitor

Connect the board, identify its serial port, and run:

```bash
idf.py -p COM5 flash monitor
```

Replace `COM5` with your board's port. On Linux, a port may be `/dev/ttyACM0` or `/dev/ttyUSB0`.

After flashing, the board starts automatically. During startup, the system may display `STARTING` and unavailable readings. After warm-up, valid sensor readings are used for normal monitoring and alarm decisions. Press **Ctrl + ]** to exit the serial monitor.

| State | Meaning |
| --- | --- |
| `NORMAL` | Both sensors have valid data and no alarm is active |
| `HIGH_TEMP` | Temperature alarm is active |
| `HIGH_GAS` | Gas alarm is active |
| `HIGH_BOTH` | Both alarms are active |
| `SENSOR_ERROR` | Sensor data is invalid or stale, with no active hazard alarm |
| `STARTING` | Waiting for initial sensor readiness |

An invalid or stale sample does not clear an already active hazard alarm. Hazard alerts take priority over the sensor error state.
