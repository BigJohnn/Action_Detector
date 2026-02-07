# Action Detector

ESP32-C5 firmware and PC tools for motion/action detection using the
ESP-SensairShuttle (BMI270 IMU). The device samples IMU data, timestamps it,
and streams frames over UDP to a host for capture and analysis.

## Contents
- `firmware/` ESP-IDF firmware (Wi-Fi + UDP streaming, BMI270 sampling)
- `pc/` Host-side utilities (receiver/analysis)
- `scripts/` Small helper scripts (e.g., UDP listener)

## Status
Bring-up complete: I2C/BMI270 verified, UDP streaming verified, basic receiver
working.
