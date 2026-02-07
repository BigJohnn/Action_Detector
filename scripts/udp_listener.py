#!/usr/bin/env python3
import socket
import struct
import time


def main():
    host = "0.0.0.0"
    port = 9000

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    print(f"Listening on {host}:{port}")

    while True:
        data, addr = sock.recvfrom(2048)
        ts = time.time()
        if len(data) == 12 and data[:4] == b"HB01":
            (ts_us,) = struct.unpack("<q", data[4:])
            print(f"[{ts:.3f}] HB from {addr[0]}:{addr[1]} ts_us={ts_us}")
            continue

        if len(data) == 20:
            ts_us, ax, ay, az, gx, gy, gz = struct.unpack("<qhhhhhh", data)
            print(
                f"[{ts:.3f}] SAMPLE {addr[0]}:{addr[1]} ts_us={ts_us} "
                f"acc=({ax},{ay},{az}) gyro=({gx},{gy},{gz})"
            )
            continue

        print(f"[{ts:.3f}] {addr[0]}:{addr[1]} len={len(data)} data={data.hex()}")


if __name__ == "__main__":
    main()
