# Complex Action Strategy (Design Notes)

## Scope
This document describes how to extend current action detection from atomic gestures
(`swipe_left`, `swipe_right`, `idle`) to complex gestures (for example `circle`)
without implementing code changes yet.

## Core Principle
Do not treat all gestures as one flat label set.
Use a hierarchical pipeline:

1. Detect motion segments from continuous stream.
2. Classify short segments into atomic primitives.
3. Parse primitive sequence into complex gesture.

## Why Circle Is Hard
`circle` can contain local patterns that resemble `swipe_left/right`.
If model is single-stage and flat, it may emit the dominant local primitive and
miss the global pattern.

## Proposed Architecture

### Stage A: Atomic Primitive Classifier
- Input: short triggered windows.
- Output: primitive label probabilities + confidence
  (`idle`, `swipe_left`, `swipe_right`, `arc_cw`, `arc_ccw`, etc.).
- Keep current DTW/XCorr stack as baseline.

### Stage B: Temporal Composition Layer
- Input: time-ordered primitive stream from Stage A.
- Output: complex gesture label (`circle`, `zigzag`, etc.) or `unknown`.
- Candidate implementations:
  - rule/FSM parser (first baseline),
  - HMM/CRF,
  - lightweight sequence model (TCN/GRU) after enough data.

## Circle vs Swipe Disambiguation Signals
Use global trajectory-style constraints, not local similarity alone:

- Direction continuity:
  `circle` keeps angular direction over longer horizon.
- Turning accumulation:
  integrated yaw/pseudo-heading rotates near one full loop.
- Closure:
  segment start and end orientation/features become close again.
- Temporal pattern:
  `circle` has multi-phase curvature; `swipe` is mostly single impulse.
- Multi-axis consistency:
  circular motion should show coupled changes across gyro axes.

## Decision Strategy
Apply open-set gating at both levels:

1. Stage A may output `unknown_primitive` when margin is weak.
2. Stage B emits complex label only when sequence constraints are met;
   otherwise return best atomic label or `unknown`.

## Data Plan for Complex Actions
- Keep existing atomic datasets.
- Add dedicated complex sessions with variable speed/amplitude.
- Label both:
  - segment-level complex labels (`circle`),
  - optional sub-phase tags (`circle_q1...q4`) if feasible.
- Include confusing negatives:
  repeated left-right swipes, partial circle, aborted circle.

## Rollout Plan
1. Freeze current atomic classifier as Stage A baseline.
2. Add parser-only Stage B rules for `circle` (no deep model yet).
3. Run confusion analysis specifically on `circle` vs `swipe_left/right`.
4. If rule-based parser saturates, upgrade Stage B to trainable sequence model.
