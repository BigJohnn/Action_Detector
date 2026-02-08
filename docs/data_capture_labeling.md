# Data Capture and Labeling Workflow

## Goal
Define a repeatable way to capture labeled IMU motion samples from the ESP32-C5 UDP stream.

## Data Layout
Use the following layout under repository root:

- `data/raw/<session_id>/`: raw CSV files, one file per labeled repeat
- `data/labels/manifest.jsonl`: append-only label metadata (one JSON object per line)

Example:

```text
data/
  raw/
    20260208_194500/
      swipe_left_r01.csv
      swipe_left_r02.csv
      swipe_right_r01.csv
  labels/
    manifest.jsonl
```

## File Format

### Raw CSV
Columns:

`ts_us,ax,ay,az,gx,gy,gz`

Frame format matches firmware UDP payload:

- Little-endian `<q6h`
- `ts_us` is firmware timestamp in microseconds
- `ax..gz` are BMI270 raw int16 values

### Label Manifest (`manifest.jsonl`)
Each line is one JSON object with these keys:

- `session_id`
- `label`
- `repeat_index` (1-based)
- `csv_path`
- `sample_count`
- `capture_started_utc`
- `capture_finished_utc`
- `duration_sec`
- `target_duration_sec`
- `udp_host`
- `udp_port`
- `frame_format`
- `notes`

## Capture Protocol

1. Prepare one action label at a time (for example `swipe_left`).
2. Collect `N` repeats for each label under one `session_id`.
3. Each repeat should contain:
- A short still segment before action.
- One clear action execution.
- A short still segment after action.
4. Keep same body posture and device orientation for a baseline dataset.
5. Repeat for all target labels plus an optional `idle` label.
6. Capture uses device timestamp (`ts_us`) for duration control and drains stale UDP packets before each repeat.

Recommended baseline:

- repeats per label: `20`
- target duration per repeat: `2.5` to `4.0` seconds
- rest between repeats: `1.0` to `2.0` seconds

Idle-specific recommendation:

- Capture `idle` under varied micro-movements (breathing, slight wrist drift, posture shift),
  not only perfectly still states. This reduces false triggers in online mode.
- Add hard negatives where user prepares to move but cancels action.

## Command
Run from repo root:

```bash
python3 pc/capture_labeled.py \
  --label swipe_left \
  --repeats 20 \
  --duration-sec 3.0 \
  --rest-sec 1.0 \
  --notes "standing, right hand"
```

To keep samples grouped, set a fixed session:

```bash
python3 pc/capture_labeled.py --session 20260208_evening --label swipe_right
```

## Validation Checklist
- CSV files are generated under the expected `session_id`.
- `manifest.jsonl` includes one entry per generated CSV.
- Sample counts are non-zero for all repeats.
- Labels are consistent and lowercase with underscores.
