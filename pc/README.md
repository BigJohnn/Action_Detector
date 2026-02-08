# pc

Python UDP receiver for BMI270 samples.

## Run
- `python3 udp_receiver.py`

## Output
- `samples.csv` with columns: `ts_us,ax,ay,az,gx,gy,gz`

## Labeled Capture
Run from repository root:

- `python3 pc/capture_labeled.py --label swipe_left --repeats 20 --duration-sec 3.0 --rest-sec 1.0`

Outputs:

- `data/raw/<session_id>/<label>_rXX.csv`
- `data/labels/manifest.jsonl`

## DTW/XCorr Baseline
Evaluate from manifest:

- `python3 pc/dtw_baseline.py evaluate --manifest data/labels/manifest.jsonl --test-ratio 0.3 --k 1`

Classify one CSV using manifest as references:

- `python3 pc/dtw_baseline.py classify --manifest data/labels/manifest.jsonl --csv data/raw/<session_id>/<label>_r01.csv --score-mode hybrid --k 5`

## Build Offline Model
Build once, use many times for fast startup:

- `python3 pc/build_model.py --manifest data/labels/manifest.jsonl --model-out data/model/action_model.json`

## Live Demo (UDP -> Detect -> Classify)
With firmware streaming live UDP frames (uses prebuilt model):

- `python3 pc/live_classify.py --model data/model/action_model.json --mode trigger --k 5`

Optional one-shot mode:

- `python3 pc/live_classify.py --model data/model/action_model.json --mode trigger --once`

### Speak Label on Board Speaker (TTS)
When prediction is not `unknown`, synthesize label TTS on PC and stream PCM to board:

- `python3 pc/live_classify.py --model data/model/action_model.json --mode trigger --k 5 --tts-enable --tts-dest-ip auto --tts-port 9001`

Board-local playback mode (recommended for robustness):

- `python3 pc/live_classify.py --model data/model/action_model.json --mode trigger --k 5 --tts-enable --tts-output-mode board-local --tts-dest-ip auto --tts-port 9001`

Notes:
- Current implementation uses macOS `say` command as TTS backend.
- `--tts-dest-ip auto` sends audio to source IP of incoming IMU packets.
- `--tts-output-mode board-local` sends only the label (`LABL` packet). Board plays preloaded PCM clip.
- `board-local` mode avoids most Wi-Fi streaming distortion and is now the preferred mode.
- In `board-local` mode, same-label detections are announced by default (still rate-limited by `--tts-cooldown-sec`).
- In `board-local` mode, `--tts-voice`, `--tts-language`, `--tts-gain`, `--tts-target-peak`, `--tts-fade-ms`
  do not affect board playback audio content (they are only used in `stream` mode).
- `board-local` mode requires firmware that embeds clips from `firmware/main/audio_labels/*.pcm`.
- Voice/language tuning:
  - `--tts-voice Tingting --tts-language zh`
  - `--tts-voice Samantha --tts-language en`
- Anti-pop tuning:
  - `--tts-gain 0.18` to `--tts-gain 0.30` (lower if distorted)
  - `--tts-target-peak 0.20` to `--tts-target-peak 0.30` (auto limits peak level)
  - `--tts-fade-ms 15`

If model file is missing, temporary fallback is available (slow startup):

- `python3 pc/live_classify.py --build-on-start --manifest data/labels/manifest.jsonl`

## Current Quality Notes
- `swipe_left` / `swipe_right` are currently the strongest classes in live mode.
- `idle` is currently the weakest class and needs dedicated hard-negative tuning.
- Prefer using trigger mode instead of fixed windows for online inference.

Trigger tuning:

- `--score-mode` (`hybrid` recommended, supports `dtw` and `xcorr`)
- `--trigger-on` / `--trigger-off`
- `--pre-sec` / `--post-sec`
- `--min-action-sec` / `--max-action-sec`

## Next Expansion
- Complex/compositional gesture plan is documented in `docs/complex_action_strategy.md`.
