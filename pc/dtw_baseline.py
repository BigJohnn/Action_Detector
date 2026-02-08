#!/usr/bin/env python3
import argparse
import csv
import json
import math
import random
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


FEATURES = ("ax", "ay", "az", "gx", "gy", "gz")


@dataclass
class LabeledSequence:
    label: str
    path: Path
    seq: list[tuple[float, ...]]


@dataclass
class PairMetric:
    label: str
    path: Path
    dtw: float
    xcorr: float
    lag: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DTW baseline for IMU action classification.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--manifest",
        type=Path,
        default=Path("data/labels/manifest.jsonl"),
        help="Label manifest path",
    )
    common.add_argument("--session", default="", help="Optional session_id filter")
    common.add_argument(
        "--labels",
        default="",
        help="Optional comma-separated label filter (e.g. swipe_left,idle)",
    )
    common.add_argument(
        "--max-points",
        type=int,
        default=180,
        help="Downsample each sequence to at most this many points",
    )
    common.add_argument(
        "--window-frac",
        type=float,
        default=0.2,
        help="Sakoe-Chiba window fraction for DTW (0 means full matrix)",
    )
    common.add_argument(
        "--no-znorm",
        action="store_true",
        help="Disable per-sequence feature z-normalization",
    )
    common.add_argument("--k", type=int, default=1, help="K in k-NN over DTW distances")

    ev = sub.add_parser(
        "evaluate", parents=[common], help="Stratified split evaluation on manifest data"
    )
    ev.add_argument("--test-ratio", type=float, default=0.3, help="Test split ratio")
    ev.add_argument("--seed", type=int, default=7, help="Random seed")

    clf = sub.add_parser(
        "classify", parents=[common], help="Classify one CSV using manifest as references"
    )
    clf.add_argument("--csv", type=Path, required=True, help="CSV file to classify")
    clf.add_argument(
        "--per-label-k",
        type=int,
        default=3,
        help="Average DTW distance over top-k neighbors inside each label",
    )
    clf.add_argument(
        "--disable-reject",
        action="store_true",
        help="Disable unknown rejection gate",
    )
    clf.add_argument(
        "--reject-quantile",
        type=float,
        default=1.0,
        help="Per-label in-class score quantile used to set threshold",
    )
    clf.add_argument(
        "--reject-scale",
        type=float,
        default=1.10,
        help="Scale on per-label threshold (higher = less strict)",
    )
    clf.add_argument(
        "--reject-margin",
        type=float,
        default=1.03,
        help="Require runner-up score / best score >= margin, otherwise reject",
    )
    clf.add_argument(
        "--unknown-label",
        default="unknown",
        help="Label emitted on rejection",
    )
    clf.add_argument(
        "--reject-threshold-grace",
        type=float,
        default=1.03,
        help="Allow slight exceed over calibrated threshold by this ratio",
    )
    clf.add_argument(
        "--score-mode",
        choices=("dtw", "hybrid", "xcorr"),
        default="hybrid",
        help="Scoring mode used for label decision",
    )
    clf.add_argument(
        "--hybrid-alpha",
        type=float,
        default=0.35,
        help="Weight of xcorr penalty in hybrid score",
    )
    clf.add_argument(
        "--xcorr-max-lag-frac",
        type=float,
        default=0.15,
        help="Max lag as fraction of sequence length for cross-correlation",
    )
    clf.add_argument(
        "--xcorr-min-overlap-frac",
        type=float,
        default=0.50,
        help="Minimum overlap as fraction of shorter sequence for cross-correlation",
    )

    return parser.parse_args()


def read_manifest(path: Path, session: str, labels: set[str]) -> list[dict]:
    rows: list[dict] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            item = json.loads(line)
            if session and item.get("session_id") != session:
                continue
            label = str(item.get("label", "")).strip().lower()
            if labels and label not in labels:
                continue
            item["label"] = label
            rows.append(item)
    return rows


def read_sequence(csv_path: Path) -> list[tuple[float, ...]]:
    seq: list[tuple[float, ...]] = []
    with csv_path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            seq.append(tuple(float(row[k]) for k in FEATURES))
    if not seq:
        raise ValueError(f"no samples in {csv_path}")
    return seq


def downsample(seq: list[tuple[float, ...]], max_points: int) -> list[tuple[float, ...]]:
    n = len(seq)
    if max_points <= 0 or n <= max_points:
        return seq
    if max_points == 1:
        return [seq[0]]
    step = (n - 1) / (max_points - 1)
    idxs = [min(n - 1, round(i * step)) for i in range(max_points)]
    return [seq[i] for i in idxs]


def znormalize(seq: list[tuple[float, ...]]) -> list[tuple[float, ...]]:
    dims = len(seq[0])
    means = [0.0] * dims
    for p in seq:
        for i, x in enumerate(p):
            means[i] += x
    inv_n = 1.0 / len(seq)
    means = [m * inv_n for m in means]

    vars_ = [0.0] * dims
    for p in seq:
        for i, x in enumerate(p):
            d = x - means[i]
            vars_[i] += d * d
    stds = [math.sqrt(v * inv_n) if v > 0 else 1.0 for v in vars_]
    stds = [s if s > 1e-12 else 1.0 for s in stds]

    out: list[tuple[float, ...]] = []
    for p in seq:
        out.append(tuple((x - means[i]) / stds[i] for i, x in enumerate(p)))
    return out


def prep_sequence(
    seq: list[tuple[float, ...]], max_points: int, use_znorm: bool
) -> list[tuple[float, ...]]:
    seq = downsample(seq, max_points)
    if use_znorm:
        seq = znormalize(seq)
    return seq


def load_labeled_sequences(
    manifest_rows: Iterable[dict], max_points: int, use_znorm: bool
) -> list[LabeledSequence]:
    items: list[LabeledSequence] = []
    for row in manifest_rows:
        path = Path(row["csv_path"])
        seq = read_sequence(path)
        seq = prep_sequence(seq, max_points=max_points, use_znorm=use_znorm)
        items.append(LabeledSequence(label=row["label"], path=path, seq=seq))
    return items


def point_cost(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return sum((x - y) * (x - y) for x, y in zip(a, b))


def dtw_distance(a: list[tuple[float, ...]], b: list[tuple[float, ...]], window: int) -> float:
    n = len(a)
    m = len(b)
    if window <= 0:
        window = max(n, m)
    window = max(window, abs(n - m))
    inf = float("inf")

    prev = [inf] * (m + 1)
    curr = [inf] * (m + 1)
    prev[0] = 0.0

    for i in range(1, n + 1):
        start = max(1, i - window)
        end = min(m, i + window)
        curr[0] = inf
        for j in range(1, m + 1):
            if j < start or j > end:
                curr[j] = inf
                continue
            c = point_cost(a[i - 1], b[j - 1])
            curr[j] = c + min(curr[j - 1], prev[j], prev[j - 1])
        prev, curr = curr, prev
    return prev[m]


def classify_knn(
    query: list[tuple[float, ...]],
    train: list[LabeledSequence],
    k: int,
    window_frac: float,
) -> tuple[str, list[tuple[float, str, Path]]]:
    dists: list[tuple[float, str, Path]] = []
    for item in train:
        window = int(round(max(len(query), len(item.seq)) * window_frac))
        d = dtw_distance(query, item.seq, window=window)
        dists.append((d, item.label, item.path))
    dists.sort(key=lambda x: x[0])
    k = max(1, min(k, len(dists)))
    topk = dists[:k]
    votes = Counter(label for _, label, _ in topk)
    best_label, _ = sorted(votes.items(), key=lambda x: (-x[1], x[0]))[0]
    return best_label, topk


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def max_normalized_xcorr(
    a: list[tuple[float, ...]],
    b: list[tuple[float, ...]],
    max_lag: int,
    min_overlap: int,
) -> tuple[float, int]:
    n = len(a)
    m = len(b)
    dims = len(a[0])
    best = -1.0
    best_lag = 0
    max_lag = max(0, max_lag)
    min_overlap = max(1, min_overlap)

    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            a0 = lag
            b0 = 0
            overlap = min(n - lag, m)
        else:
            a0 = 0
            b0 = -lag
            overlap = min(n, m + lag)
        if overlap < min_overlap:
            continue

        dot = 0.0
        for i in range(overlap):
            pa = a[a0 + i]
            pb = b[b0 + i]
            for d in range(dims):
                dot += pa[d] * pb[d]
        corr = dot / (overlap * dims)
        corr = clamp(corr, -1.0, 1.0)
        if corr > best:
            best = corr
            best_lag = lag

    return best, best_lag


def compute_pair_metrics(
    query: list[tuple[float, ...]],
    refs: list[LabeledSequence],
    window_frac: float,
    xcorr_max_lag_frac: float,
    xcorr_min_overlap_frac: float,
) -> list[PairMetric]:
    out: list[PairMetric] = []
    for item in refs:
        window = int(round(max(len(query), len(item.seq)) * window_frac))
        dtw = dtw_distance(query, item.seq, window=window)
        max_lag = int(round(max(len(query), len(item.seq)) * xcorr_max_lag_frac))
        min_overlap = int(round(min(len(query), len(item.seq)) * xcorr_min_overlap_frac))
        min_overlap = max(4, min_overlap)
        xcorr, lag = max_normalized_xcorr(
            query, item.seq, max_lag=max_lag, min_overlap=min_overlap
        )
        out.append(PairMetric(label=item.label, path=item.path, dtw=dtw, xcorr=xcorr, lag=lag))
    out.sort(key=lambda x: x.dtw)
    return out


def compute_distances(
    query: list[tuple[float, ...]],
    refs: list[LabeledSequence],
    window_frac: float,
) -> list[tuple[float, str, Path]]:
    out: list[tuple[float, str, Path]] = []
    for item in refs:
        window = int(round(max(len(query), len(item.seq)) * window_frac))
        d = dtw_distance(query, item.seq, window=window)
        out.append((d, item.label, item.path))
    out.sort(key=lambda x: x[0])
    return out


def compute_label_scores(
    distances: list[tuple[float, str, Path]], per_label_k: int
) -> dict[str, float]:
    grouped: dict[str, list[float]] = defaultdict(list)
    for d, label, _path in distances:
        grouped[label].append(d)

    scores: dict[str, float] = {}
    for label, vals in grouped.items():
        k = max(1, min(per_label_k, len(vals)))
        scores[label] = sum(vals[:k]) / k
    return scores


def compute_query_scores(
    query: list[tuple[float, ...]],
    refs: list[LabeledSequence],
    window_frac: float,
    per_label_k: int,
    score_mode: str,
    hybrid_alpha: float,
    xcorr_max_lag_frac: float,
    xcorr_min_overlap_frac: float,
) -> tuple[dict[str, float], dict[str, float], dict[str, float], list[PairMetric]]:
    metrics = compute_pair_metrics(
        query,
        refs,
        window_frac=window_frac,
        xcorr_max_lag_frac=xcorr_max_lag_frac,
        xcorr_min_overlap_frac=xcorr_min_overlap_frac,
    )

    by_label: dict[str, list[PairMetric]] = defaultdict(list)
    for m in metrics:
        by_label[m.label].append(m)

    dtw_scores: dict[str, float] = {}
    xcorr_scores: dict[str, float] = {}
    for label, rows in by_label.items():
        k = max(1, min(per_label_k, len(rows)))
        dtw_vals = sorted(m.dtw for m in rows)[:k]
        xcorr_vals = sorted((m.xcorr for m in rows), reverse=True)[:k]
        dtw_scores[label] = sum(dtw_vals) / k
        xcorr_scores[label] = sum(xcorr_vals) / k

    final_scores: dict[str, float] = {}
    if score_mode == "dtw":
        final_scores = dict(dtw_scores)
    elif score_mode == "xcorr":
        for label, corr in xcorr_scores.items():
            corr01 = (clamp(corr, -1.0, 1.0) + 1.0) * 0.5
            final_scores[label] = 1.0 - corr01
    else:
        best_dtw = min(dtw_scores.values()) if dtw_scores else 1.0
        best_dtw = max(best_dtw, 1e-9)
        for label in dtw_scores:
            corr = xcorr_scores.get(label, -1.0)
            corr01 = (clamp(corr, -1.0, 1.0) + 1.0) * 0.5
            xcorr_penalty = 1.0 - corr01
            dtw_norm = dtw_scores[label] / best_dtw
            final_scores[label] = dtw_norm + hybrid_alpha * xcorr_penalty

    return final_scores, dtw_scores, xcorr_scores, metrics


def quantile(vals: list[float], q: float) -> float:
    if not vals:
        raise ValueError("cannot compute quantile of empty list")
    q = min(1.0, max(0.0, q))
    s = sorted(vals)
    idx = int(round((len(s) - 1) * q))
    return s[idx]


def calibrate_label_thresholds(
    refs: list[LabeledSequence],
    window_frac: float,
    per_label_k: int,
    q: float,
    scale: float,
    score_mode: str,
    hybrid_alpha: float,
    xcorr_max_lag_frac: float,
    xcorr_min_overlap_frac: float,
) -> dict[str, float]:
    by_label: dict[str, list[LabeledSequence]] = defaultdict(list)
    for item in refs:
        by_label[item.label].append(item)

    thresholds: dict[str, float] = {}
    for label, items in by_label.items():
        scores: list[float] = []
        for item in items:
            pool = [x for x in refs if x.path != item.path]
            label_scores, _dtw_scores, _xcorr_scores, _metrics = compute_query_scores(
                item.seq,
                pool,
                window_frac=window_frac,
                per_label_k=per_label_k,
                score_mode=score_mode,
                hybrid_alpha=hybrid_alpha,
                xcorr_max_lag_frac=xcorr_max_lag_frac,
                xcorr_min_overlap_frac=xcorr_min_overlap_frac,
            )
            if label in label_scores:
                scores.append(label_scores[label])
        if not scores:
            continue
        thresholds[label] = quantile(scores, q) * scale
    return thresholds


def predict_with_rejection(
    label_scores: dict[str, float],
    thresholds: dict[str, float] | None,
    margin: float,
    threshold_grace: float,
    unknown_label: str,
) -> tuple[str, str | None]:
    if not label_scores:
        return unknown_label, "no_label_scores"

    ranked = sorted(label_scores.items(), key=lambda x: x[1])
    best_label, best_score = ranked[0]
    second_score = ranked[1][1] if len(ranked) > 1 else float("inf")

    if thresholds is not None and best_label in thresholds:
        soft_threshold = thresholds[best_label] * max(1.0, threshold_grace)
        if best_score > soft_threshold:
            return (
                unknown_label,
                f"best_score_above_threshold({best_score:.2f}>{soft_threshold:.2f})",
            )
    if margin > 0 and second_score < float("inf"):
        if second_score / max(best_score, 1e-9) < margin:
            return (
                unknown_label,
                f"margin_too_small({second_score / max(best_score, 1e-9):.3f}<{margin:.3f})",
            )
    return best_label, None


def split_stratified(
    items: list[LabeledSequence], test_ratio: float, seed: int
) -> tuple[list[LabeledSequence], list[LabeledSequence]]:
    grouped: dict[str, list[LabeledSequence]] = defaultdict(list)
    for item in items:
        grouped[item.label].append(item)

    rng = random.Random(seed)
    train: list[LabeledSequence] = []
    test: list[LabeledSequence] = []
    for label, rows in grouped.items():
        if len(rows) < 2:
            raise ValueError(f"label '{label}' has <2 samples; cannot split")
        rows = rows[:]
        rng.shuffle(rows)
        n_test = max(1, int(round(len(rows) * test_ratio)))
        n_test = min(n_test, len(rows) - 1)
        test.extend(rows[:n_test])
        train.extend(rows[n_test:])
    return train, test


def metrics(y_true: list[str], y_pred: list[str]) -> dict[str, float]:
    assert len(y_true) == len(y_pred)
    labels = sorted(set(y_true) | set(y_pred))
    total = len(y_true)
    correct = sum(1 for t, p in zip(y_true, y_pred) if t == p)
    accuracy = correct / total if total else 0.0

    f1s: list[float] = []
    for label in labels:
        tp = sum(1 for t, p in zip(y_true, y_pred) if t == label and p == label)
        fp = sum(1 for t, p in zip(y_true, y_pred) if t != label and p == label)
        fn = sum(1 for t, p in zip(y_true, y_pred) if t == label and p != label)
        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0
        f1s.append(f1)
    macro_f1 = sum(f1s) / len(f1s) if f1s else 0.0
    return {"accuracy": accuracy, "macro_f1": macro_f1}


def print_confusion(y_true: list[str], y_pred: list[str]) -> None:
    labels = sorted(set(y_true) | set(y_pred))
    matrix = {t: Counter() for t in labels}
    for t, p in zip(y_true, y_pred):
        matrix[t][p] += 1

    width = max(10, max(len(x) for x in labels) + 2)
    header = "truth\\pred".ljust(width) + "".join(l.ljust(width) for l in labels)
    print(header)
    for t in labels:
        row = t.ljust(width)
        for p in labels:
            row += str(matrix[t][p]).ljust(width)
        print(row)


def run_evaluate(args: argparse.Namespace) -> int:
    labels = {x.strip().lower() for x in args.labels.split(",") if x.strip()}
    rows = read_manifest(args.manifest, session=args.session, labels=labels)
    if not rows:
        raise ValueError("no samples selected from manifest")

    items = load_labeled_sequences(
        rows, max_points=args.max_points, use_znorm=not args.no_znorm
    )
    label_set = sorted({x.label for x in items})
    print(f"loaded {len(items)} samples, labels={label_set}")
    if len(label_set) < 2:
        print("warning: only one label found; evaluation is not discriminative yet.")

    train, test = split_stratified(items, test_ratio=args.test_ratio, seed=args.seed)
    print(f"train={len(train)} test={len(test)} k={args.k}")

    y_true: list[str] = []
    y_pred: list[str] = []
    for item in test:
        pred, _topk = classify_knn(item.seq, train, k=args.k, window_frac=args.window_frac)
        y_true.append(item.label)
        y_pred.append(pred)

    m = metrics(y_true, y_pred)
    print(f"accuracy={m['accuracy']:.4f}")
    print(f"macro_f1={m['macro_f1']:.4f}")
    print("confusion_matrix:")
    print_confusion(y_true, y_pred)
    return 0


def run_classify(args: argparse.Namespace) -> int:
    labels = {x.strip().lower() for x in args.labels.split(",") if x.strip()}
    rows = read_manifest(args.manifest, session=args.session, labels=labels)
    if not rows:
        raise ValueError("no reference samples selected from manifest")

    references = load_labeled_sequences(
        rows, max_points=args.max_points, use_znorm=not args.no_znorm
    )
    thresholds = None
    if not args.disable_reject:
        thresholds = calibrate_label_thresholds(
            references,
            window_frac=args.window_frac,
            per_label_k=args.per_label_k,
            q=args.reject_quantile,
            scale=args.reject_scale,
            score_mode=args.score_mode,
            hybrid_alpha=args.hybrid_alpha,
            xcorr_max_lag_frac=args.xcorr_max_lag_frac,
            xcorr_min_overlap_frac=args.xcorr_min_overlap_frac,
        )

    query_path = args.csv.resolve()
    train = [x for x in references if x.path.resolve() != query_path]
    if not train:
        raise ValueError("no reference samples left after excluding query file")

    query_seq = prep_sequence(
        read_sequence(args.csv), max_points=args.max_points, use_znorm=not args.no_znorm
    )
    label_scores, dtw_scores, xcorr_scores, pair_metrics = compute_query_scores(
        query_seq,
        train,
        window_frac=args.window_frac,
        per_label_k=args.per_label_k,
        score_mode=args.score_mode,
        hybrid_alpha=args.hybrid_alpha,
        xcorr_max_lag_frac=args.xcorr_max_lag_frac,
        xcorr_min_overlap_frac=args.xcorr_min_overlap_frac,
    )
    pred, reject_reason = predict_with_rejection(
        label_scores,
        thresholds=thresholds,
        margin=args.reject_margin,
        threshold_grace=args.reject_threshold_grace,
        unknown_label=args.unknown_label,
    )
    print(f"prediction={pred}")
    if reject_reason:
        print(f"reject_reason={reject_reason}")
    print(f"score_mode={args.score_mode}")
    print("label_scores (final | dtw | xcorr):")
    for label, score in sorted(label_scores.items(), key=lambda x: x[1]):
        dtw = dtw_scores.get(label, float("nan"))
        xcorr = xcorr_scores.get(label, float("nan"))
        print(f"- {label}: {score:.4f} | {dtw:.4f} | {xcorr:.4f}")
    print("top_neighbors:")
    k = max(1, min(args.k, len(pair_metrics)))
    for rank, m in enumerate(pair_metrics[:k], start=1):
        print(
            f"{rank}. label={m.label} dist={m.dtw:.4f} xcorr={m.xcorr:.4f} "
            f"lag={m.lag} ref={m.path}"
        )
    return 0


def main() -> int:
    args = parse_args()
    if args.k <= 0:
        raise ValueError("--k must be > 0")
    if args.window_frac < 0:
        raise ValueError("--window-frac must be >= 0")
    if getattr(args, "per_label_k", 1) <= 0:
        raise ValueError("--per-label-k must be > 0")
    if getattr(args, "reject_margin", 0.0) < 0:
        raise ValueError("--reject-margin must be >= 0")
    if getattr(args, "reject_scale", 1.0) <= 0:
        raise ValueError("--reject-scale must be > 0")
    if getattr(args, "reject_quantile", 0.9) < 0 or getattr(args, "reject_quantile", 0.9) > 1:
        raise ValueError("--reject-quantile must be in [0,1]")
    if getattr(args, "reject_threshold_grace", 1.03) < 1:
        raise ValueError("--reject-threshold-grace must be >= 1")
    if getattr(args, "hybrid_alpha", 0.35) < 0:
        raise ValueError("--hybrid-alpha must be >= 0")
    if getattr(args, "xcorr_max_lag_frac", 0.15) < 0:
        raise ValueError("--xcorr-max-lag-frac must be >= 0")
    if (
        getattr(args, "xcorr_min_overlap_frac", 0.5) <= 0
        or getattr(args, "xcorr_min_overlap_frac", 0.5) > 1
    ):
        raise ValueError("--xcorr-min-overlap-frac must be in (0,1]")

    if args.cmd == "evaluate":
        return run_evaluate(args)
    if args.cmd == "classify":
        return run_classify(args)
    raise ValueError(f"unknown cmd {args.cmd}")


if __name__ == "__main__":
    raise SystemExit(main())
