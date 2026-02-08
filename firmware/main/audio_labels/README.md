# Local Label Audio Assets

This folder stores preloaded PCM clips for board-local playback.

Required format:
- sample rate: `24000 Hz`
- channels: `mono`
- sample format: `signed 16-bit little-endian PCM`
- file extension: `.pcm`

If you use a different sample rate, also update firmware menuconfig:
- `Action Detect -> Sample rate for embedded board-local label clips (Hz)`

Current mapping:
- `swipe_left.pcm` -> label `swipe_left`
- `swipe_right.pcm` -> label `swipe_right`
- `idle.pcm` -> label `idle` (currently a short tone placeholder)

To replace clips:
1. Prepare a WAV file at `24kHz`, mono, 16-bit.
2. Extract raw frames to `.pcm`.
3. Keep the same filename and rebuild firmware.
