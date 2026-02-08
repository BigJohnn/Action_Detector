#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import time
from pathlib import Path

from dtw_baseline import (
    calibrate_label_thresholds,
    load_labeled_sequences,
    read_manifest,
)


def utc_now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build offline IMU action model artifact for fast live startup."
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("data/labels/manifest.jsonl"),
        help="Label manifest path",
    )
    parser.add_argument("--session", default="", help="Optional session_id filter")
    parser.add_argument(
        "--labels",
        default="",
        help="Optional comma-separated label filter (e.g. swipe_left,idle)",
    )
    parser.add_argument(
        "--model-out",
        type=Path,
        default=Path("data/model/action_model.json"),
        help="Output model path",
    )
    parser.add_argument("--max-points", type=int, default=180, help="Sequence resample points")
    parser.add_argument(
        "--no-znorm",
        action="store_true",
        help="Disable per-sequence z-normalization",
    )
    parser.add_argument(
        "--window-frac",
        type=float,
        default=0.2,
        help="Sakoe-Chiba window fraction for DTW",
    )
    parser.add_argument("--per-label-k", type=int, default=3, help="Top-k per label scoring")
    parser.add_argument(
        "--score-mode",
        choices=("dtw", "hybrid", "xcorr"),
        default="hybrid",
        help="Scoring mode used for thresholds and live decision",
    )
    parser.add_argument(
        "--hybrid-alpha",
        type=float,
        default=0.35,
        help="Weight of xcorr penalty in hybrid score",
    )
    parser.add_argument(
        "--xcorr-max-lag-frac",
        type=float,
        default=0.15,
        help="Max lag as fraction of sequence length for cross-correlation",
    )
    parser.add_argument(
        "--xcorr-min-overlap-frac",
        type=float,
        default=0.50,
        help="Minimum overlap as fraction of shorter sequence for cross-correlation",
    )
    parser.add_argument(
        "--reject-quantile",
        type=float,
        default=1.0,
        help="In-class score quantile for per-label threshold",
    )
    parser.add_argument(
        "--reject-scale",
        type=float,
        default=1.10,
        help="Threshold scale (higher less strict)",
    )
    parser.add_argument(
        "--reject-margin",
        type=float,
        default=1.03,
        help="Runner-up score / best score margin for reject gate",
    )
    parser.add_argument(
        "--reject-threshold-grace",
        type=float,
        default=1.03,
        help="Allow slight exceed over threshold by this ratio",
    )
    parser.add_argument(
        "--unknown-label",
        default="unknown",
        help="Label emitted on rejection",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    labels = {x.strip().lower() for x in args.labels.split(",") if x.strip()}
    use_znorm = not args.no_znorm

    t0 = time.perf_counter()
    rows = read_manifest(args.manifest, session=args.session, labels=labels)
    if not rows:
        raise ValueError("no samples selected from manifest")

    refs = load_labeled_sequences(rows, max_points=args.max_points, use_znorm=use_znorm)
    if not refs:
        raise ValueError("no references loaded")

    thresholds = calibrate_label_thresholds(
        refs,
        window_frac=args.window_frac,
        per_label_k=args.per_label_k,
        q=args.reject_quantile,
        scale=args.reject_scale,
        score_mode=args.score_mode,
        hybrid_alpha=args.hybrid_alpha,
        xcorr_max_lag_frac=args.xcorr_max_lag_frac,
        xcorr_min_overlap_frac=args.xcorr_min_overlap_frac,
    )
    t1 = time.perf_counter()

    model = {
        "version": 1,
        "built_utc": utc_now_iso(),
        "params": {
            "max_points": args.max_points,
            "use_znorm": use_znorm,
            "window_frac": args.window_frac,
            "per_label_k": args.per_label_k,
            "score_mode": args.score_mode,
            "hybrid_alpha": args.hybrid_alpha,
            "xcorr_max_lag_frac": args.xcorr_max_lag_frac,
            "xcorr_min_overlap_frac": args.xcorr_min_overlap_frac,
            "reject_quantile": args.reject_quantile,
            "reject_scale": args.reject_scale,
            "reject_margin": args.reject_margin,
            "reject_threshold_grace": args.reject_threshold_grace,
            "unknown_label": args.unknown_label,
        },
        "thresholds": thresholds,
        "labels": sorted({x.label for x in refs}),
        "references": [
            {
                "label": x.label,
                "path": str(x.path),
                "seq": [list(p) for p in x.seq],
            }
            for x in refs
        ],
    }

    args.model_out.parent.mkdir(parents=True, exist_ok=True)
    with args.model_out.open("w", encoding="utf-8") as f:
        json.dump(model, f, ensure_ascii=True)

    print(f"saved model: {args.model_out}")
    print(f"labels={model['labels']} refs={len(refs)}")
    print(f"thresholds={thresholds}")
    print(f"build_seconds={t1 - t0:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
