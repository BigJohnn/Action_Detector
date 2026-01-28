# firmware

ESP-IDF firmware for SensairShuttle + BMI270 (I2C) streaming over UDP.

## Quick start
- Configure Wi-Fi and destination IP in `main/udp_sender.c`.
- Configure I2C pins in `main/app_main.c`.

## Build
- `idf.py set-target esp32c5`
- `idf.py build`
- `idf.py flash monitor`
