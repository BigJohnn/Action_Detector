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
