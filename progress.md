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
- [x] Trajectory capture & labeling workflow
- [x] Pattern learning baseline (DTW nearest-neighbor on labeled CSV)
- [ ] End-to-end demo (record -> learn -> send -> receive -> classify)

## Current Focus
- Stabilize `idle` in online inference while keeping `swipe_left/right` accuracy.
- Prepare architecture for compositional/complex gestures (e.g., circle).

## Notes
- UDP frame format: little-endian <q6h (ts_us + ax,ay,az,gx,gy,gz)
- BMI270 I2C address detected at 0x69; chip_id read OK; live samples confirmed over UDP.
- Pin defs aligned to ESP-SensairShuttle v1.0: I2C SDA=GPIO2, SCL=GPIO3; EXT_IO2=GPIO5, EXT_IO1=GPIO4; WS2812_CTRL=GPIO1.
- NVS-backed Wi-Fi/UDP config with optional boot provisioning; UDP heartbeat enabled for bring-up.
- Workflow skeleton added: `docs/data_capture_labeling.md` + `pc/capture_labeled.py` (CSV + manifest JSONL).
- Baseline classifier added: `pc/dtw_baseline.py` (`evaluate` + `classify` subcommands).
- Live inference script added: `pc/live_classify.py` (UDP receive + DTW classify), pending full on-device demo run.
- System update in progress: timestamp-based windowing + UDP drain, offline model artifact (`pc/build_model.py`), trigger-based online capture.
- Latest empirical quality: `swipe_left/right` good; `idle` not yet production-ready.
- Complex action design notes added: `docs/complex_action_strategy.md`.
