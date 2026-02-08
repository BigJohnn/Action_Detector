# firmware

ESP-IDF firmware for SensairShuttle + BMI270 (I2C) streaming over UDP.

## Quick start
- Configure Wi-Fi and destination IP in `main/udp_sender.c`.
- Configure I2C pins in `main/app_main.c`.
- Optional: configure action audio command listen port in menuconfig:
  - `Action Detect -> UDP listen port for audio command/stream` (default `9001`)
- Optional: if embedded clip sample rate differs from default:
  - `Action Detect -> Sample rate for embedded board-local label clips (Hz)` (default `24000`)

## Build
- `idf.py set-target esp32c5`
- `idf.py build`
- `idf.py flash monitor`

## Action TTS Playback (from PC)
- Firmware listens UDP audio command packets on port `9001` by default.
- Packet protocol:
  - `AUDS` + `uint32_le sample_rate`
  - `AUDD` + `uint16_le seq` + `uint16_le sample_count` + PCM16LE mono samples
  - `AUDE` + `uint16_le seq`
  - `LABL` + UTF-8 label bytes (e.g., `swipe_left`) for board-local clip playback
- Audio output follows ESP-SensairShuttle PDM speaker wiring reference
  (`P=GPIO7`, `N=GPIO8`, `PA_CTL=GPIO1`).
- Firmware applies PCM attenuation and start/end silence padding to reduce pop noise.
- Firmware keeps audio path alive for short idle periods (instead of stop per phrase) and
  fills small UDP packet gaps with silence to reduce burst/pop artifacts from jitter/loss.
- Board-local label playback assets are embedded from `main/audio_labels/*.pcm`.
- Local label playback adds warm-up silence before clip output to avoid first-frame/syllable loss.

### Replacing board-local label clips
1. Prepare source WAV for each label at `24kHz`, mono, 16-bit PCM.
2. Convert to raw PCM and overwrite target files under `main/audio_labels/`:
   - `swipe_left.pcm`
   - `swipe_right.pcm`
   - `idle.pcm`
3. Rebuild and flash firmware.
4. If you changed clip sample rate, update menuconfig
   `Action Detect -> Sample rate for embedded board-local label clips (Hz)` accordingly.

Example conversion (macOS):
- `afconvert -f WAVE -d LEI16@24000 -c 1 in.wav out.wav`
- Extract PCM payload (e.g. with Python `wave` module) and save as `.pcm`.
