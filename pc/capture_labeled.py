#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import socket
import struct
import time
from pathlib import Path

FMT = "<q6h"
SIZE = struct.calcsize(FMT)


def utc_now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def make_session_id() -> str:
    return dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def parse_args() -> argparse.Namespace:
    default_base = Path(__file__).resolve().parents[1] / "data"

    parser = argparse.ArgumentParser(
        description="Capture labeled UDP IMU samples into per-repeat CSV files."
    )
    parser.add_argument("--host", default="0.0.0.0", help="UDP bind host")
    parser.add_argument("--port", type=int, default=9000, help="UDP bind port")
    parser.add_argument("--base-dir", type=Path, default=default_base, help="Data root")
    parser.add_argument(
        "--session",
        default=make_session_id(),
        help="Session id for output folder (default: timestamp)",
    )
    parser.add_argument("--label", required=True, help="Action label, e.g. swipe_left")
    parser.add_argument("--repeats", type=int, default=10, help="Number of repeats")
    parser.add_argument(
        "--duration-sec", type=float, default=3.0, help="Capture duration per repeat"
    )
    parser.add_argument(
        "--rest-sec", type=float, default=1.0, help="Rest time between repeats"
    )
    parser.add_argument("--notes", default="", help="Optional notes written to manifest")
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing CSV file for same label/repeat",
    )
    return parser.parse_args()


def valid_sample_packet(data: bytes) -> bool:
    return len(data) >= SIZE


def normalize_label(label: str) -> str:
    return label.strip().lower().replace(" ", "_")


def append_manifest(path: Path, entry: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, ensure_ascii=True) + "\n")


def drain_socket(sock: socket.socket, max_packets: int = 50000) -> int:
    drained = 0
    old_timeout = sock.gettimeout()
    sock.setblocking(False)
    try:
        while drained < max_packets:
            try:
                sock.recvfrom(2048)
                drained += 1
            except BlockingIOError:
                break
    finally:
        sock.setblocking(True)
        sock.settimeout(old_timeout)
    return drained


def recv_sample(sock: socket.socket) -> tuple[int, int, int, int, int, int, int] | None:
    try:
        data, _addr = sock.recvfrom(1024)
    except socket.timeout:
        return None
    if not valid_sample_packet(data):
        return None
    return struct.unpack(FMT, data[:SIZE])


def capture_repeat(
    sock: socket.socket, csv_path: Path, duration_sec: float, overwrite: bool
) -> tuple[int, str, str]:
    if csv_path.exists() and not overwrite:
        raise FileExistsError(f"{csv_path} already exists (use --overwrite to replace)")

    csv_path.parent.mkdir(parents=True, exist_ok=True)
    drained = drain_socket(sock)
    if drained > 0:
        print(f"drained {drained} stale packets before capture")

    start_iso = utc_now_iso()
    sample_count = 0
    duration_us = int(duration_sec * 1_000_000)
    start_ts_us: int | None = None

    with csv_path.open("w", encoding="utf-8") as f:
        f.write("ts_us,ax,ay,az,gx,gy,gz\n")
        while True:
            sample = recv_sample(sock)
            if sample is None:
                continue
            ts_us, ax, ay, az, gx, gy, gz = sample

            if start_ts_us is None:
                start_ts_us = ts_us
            if ts_us < start_ts_us:
                continue

            f.write(f"{ts_us},{ax},{ay},{az},{gx},{gy},{gz}\n")
            sample_count += 1
            if ts_us - start_ts_us >= duration_us:
                break

    end_iso = utc_now_iso()
    return sample_count, start_iso, end_iso


def main() -> None:
    args = parse_args()
    if args.repeats <= 0:
        raise ValueError("--repeats must be > 0")
    if args.duration_sec <= 0:
        raise ValueError("--duration-sec must be > 0")
    if args.rest_sec < 0:
        raise ValueError("--rest-sec must be >= 0")

    label = normalize_label(args.label)
    session_id = args.session
    base_dir = args.base_dir.resolve()
    raw_dir = base_dir / "raw" / session_id
    manifest_path = base_dir / "labels" / "manifest.jsonl"

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.settimeout(0.25)
    print(f"listening on {args.host}:{args.port}")
    print(f"session={session_id} label={label} repeats={args.repeats}")
    print(f"raw output: {raw_dir}")
    print(f"manifest: {manifest_path}")

    for i in range(1, args.repeats + 1):
        if i > 1 and args.rest_sec > 0:
            print(f"rest {args.rest_sec:.1f}s before repeat {i}...")
            time.sleep(args.rest_sec)

        csv_name = f"{label}_r{i:02d}.csv"
        csv_path = raw_dir / csv_name
        print(f"capturing repeat {i}/{args.repeats}: {csv_name}")
        sample_count, started, finished = capture_repeat(
            sock=sock,
            csv_path=csv_path,
            duration_sec=args.duration_sec,
            overwrite=args.overwrite,
        )

        entry = {
            "session_id": session_id,
            "label": label,
            "repeat_index": i,
            "csv_path": str(csv_path),
            "sample_count": sample_count,
            "capture_started_utc": started,
            "capture_finished_utc": finished,
            "duration_sec": args.duration_sec,
            "target_duration_sec": args.duration_sec,
            "udp_host": args.host,
            "udp_port": args.port,
            "frame_format": FMT,
            "notes": args.notes,
        }
        append_manifest(manifest_path, entry)
        print(f"saved {sample_count} samples")

    print("capture completed")


if __name__ == "__main__":
    main()
