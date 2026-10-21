#!/usr/bin/env python3
"""Upload build_cyd/cyd_assets/icons.bin through the CYD director console.

Requires pyserial. The firmware refuses to overwrite an existing bank.
"""
import argparse
import pathlib
import re
import time
import zlib
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bank", type=pathlib.Path)
    parser.add_argument("--port", default="COM4")
    args = parser.parse_args()
    data = args.bank.read_bytes()
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.1
    port.dtr = port.rts = False
    port.open()
    try:
        def command(text, pattern, timeout=10):
            port.write((text + "\n").encode("ascii"))
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                line = port.readline().decode("utf-8", "replace").strip()
                if "SD FAIL" in line:
                    raise RuntimeError(line)
                match = re.search(pattern, line)
                if match:
                    return match
            raise TimeoutError(f"No response to {text.split()[0:2]}")

        status = command("sd status", r"SD READY (\d+) ([0-9a-f]+) (verified|missing)")
        if int(status[1]) != len(data) or int(status[2], 16) != zlib.crc32(data):
            raise ValueError("Bank does not match the flashed firmware")
        if status[3] == "verified":
            print("Matching SD icon bank already verified; nothing overwritten.")
            return
        command("sd begin", r"SD BEGIN")
        for offset in range(0, len(data), 64):
            chunk = data[offset:offset + 64]
            command(f"sd chunk {offset} {chunk.hex()}", rf"SD ACK {offset + len(chunk)}$")
            if offset % 8192 == 0:
                print(f"{offset}/{len(data)} bytes", flush=True)
        command("sd finish", r"SD COMPLETE verified", timeout=30)
        print(f"Saved and readback-verified {len(data)} bytes; CRC32 {zlib.crc32(data):08x}")
    finally:
        port.close()


if __name__ == "__main__":
    main()
