# Project Progress

## Status
- Overall: in progress
- Last updated: 2026-01-28

## Milestones
- [x] Project skeleton (firmware + pc receiver)
- [ ] Hardware bring-up (SensairShuttle + BMI270 shuttle board)
- [ ] BMI270 driver (use official driver if available, otherwise minimal I2C)
- [ ] Sensor data pipeline on ESP32-C5 (sampling, timestamping, buffering)
- [ ] Transport to PC via Wi-Fi UDP
- [ ] Python receiver + storage format
- [ ] Trajectory capture & labeling workflow
- [ ] Pattern learning baseline (DTW/feature matching)
- [ ] End-to-end demo (record -> learn -> send -> receive -> classify)

## Current Focus
- Wire BMI270 over I2C and confirm chip ID read

## Notes
- UDP frame format: little-endian <q6h (ts_us + ax,ay,az,gx,gy,gz)
- Update Wi-Fi SSID/PASS and UDP destination IP in `firmware/main/udp_sender.c`.
