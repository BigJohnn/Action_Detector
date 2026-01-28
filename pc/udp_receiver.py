#!/usr/bin/env python3
import socket
import struct
import time
from pathlib import Path

HOST = "0.0.0.0"
PORT = 9000
OUT = Path("samples.csv")

# Frame: <q6h (little endian) -> ts_us, ax, ay, az, gx, gy, gz
FMT = "<q6h"
SIZE = struct.calcsize(FMT)


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((HOST, PORT))
    print(f"listening on {HOST}:{PORT}")

    with OUT.open("w") as f:
        f.write("ts_us,ax,ay,az,gx,gy,gz\n")
        while True:
            data, addr = sock.recvfrom(1024)
            if len(data) < SIZE:
                continue
            ts_us, ax, ay, az, gx, gy, gz = struct.unpack(FMT, data[:SIZE])
            f.write(f"{ts_us},{ax},{ay},{az},{gx},{gy},{gz}\n")
            # simple flush for safety during early dev
            f.flush()


if __name__ == "__main__":
    main()
