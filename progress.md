# Project Progress

## Status
- Overall: in progress
- Last updated: 2026-02-08

## Milestones
- [x] Project skeleton (firmware + pc receiver)
- [x] Hardware bring-up (SensairShuttle + BMI270 shuttle board)
- [x] BMI270 driver (integrated via `bmi270_sensor` component)
- [x] Sensor data pipeline on ESP32-C5 (sampling, timestamping, buffering)
- [x] Transport to PC via Wi-Fi UDP
- [x] Python receiver + storage format
- [ ] Trajectory capture & labeling workflow
- [ ] Pattern learning baseline (DTW/feature matching)
- [ ] End-to-end demo (record -> learn -> send -> receive -> classify)

## Current Focus
- Verify sensor scaling/calibration and define data capture + labeling workflow

## Notes
- UDP frame format: little-endian <q6h (ts_us + ax,ay,az,gx,gy,gz)
- BMI270 I2C address detected at 0x69; chip_id read OK; live samples confirmed over UDP.
- Pin defs aligned to ESP-SensairShuttle v1.0: I2C SDA=GPIO2, SCL=GPIO3; EXT_IO2=GPIO5, EXT_IO1=GPIO4; WS2812_CTRL=GPIO1.
- NVS-backed Wi-Fi/UDP config with optional boot provisioning; UDP heartbeat enabled for bring-up.
