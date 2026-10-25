"""Package a trusted local CYD image; optionally copy it over the USB console."""
import argparse
import hashlib
from pathlib import Path
import struct
import time
from board_profiles import BOARDS

HEADER = struct.Struct("<8s24sI32s16s")
BOARD = b"cyd2432s028"


def package(image, release, board=BOARD):
    if board not in (profile.identity.encode("ascii") for profile in BOARDS.values()):
        raise ValueError("Unsupported display board")
    label = release.encode("ascii")
    if not label or len(label) > 15 or any(c < 32 or c > 126 for c in label):
        raise ValueError("Release must be 1..15 printable ASCII characters")
    if not 0x140 <= len(image) <= 0x1e0000 or image[0] != 0xe9:
        raise ValueError("Not an ESP32 application image or image exceeds the OTA slot")
    if image[0x120:0x128] != b"PTBOARD\0" or image[0x128:0x140].rstrip(b"\0") != board:
        raise ValueError("Not a Pocket Tank application for the requested board")
    return HEADER.pack(b"PTCYDSD1", board, len(image), hashlib.sha256(image).digest(), label) + image


def upload(data, port_name):
    import serial
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = port_name, 115200, 0.2
    port.dtr = port.rts = False
    try:
        port.open()
        port.reset_input_buffer()
        port.write(f"sd update-upload {len(data)}\n".encode())

        def wait_for(expected, timeout=15):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                line = port.readline().decode("utf-8", "replace")
                if expected in line:
                    return
                if "SD update:" in line or "SD FAIL" in line:
                    raise RuntimeError(line.strip())
            raise TimeoutError(f"Device did not report {expected}")

        wait_for(f"SD UPDATE READY {len(data)}")
        for offset in range(0, len(data), 256):
            block = data[offset:offset + 256]
            port.write(block)
            port.flush()
            wait_for(f"SD UPDATE ACK {offset + len(block)}")
        wait_for("SD UPDATE UPLOADED verified", 60)
    finally:
        port.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Application .bin, NOT a merged flash image")
    parser.add_argument("--release", required=True, help="Display label, e.g. 1.0.0")
    parser.add_argument("--out", required=True, type=Path, help="Output update.ptu")
    parser.add_argument("--port", help="Optional USB copy to SD, e.g. COM4; requires pyserial")
    parser.add_argument("--board", choices=BOARDS, default="cyd")
    args = parser.parse_args()
    data = package(args.image.read_bytes(), args.release, BOARDS[args.board].identity.encode("ascii"))
    with args.out.open("xb") as file:
        file.write(data)
    print(f"Created {args.out}: {len(data)} bytes (SHA-256, trusted local package, NOT signed)")
    if args.port:
        upload(data, args.port)
        print("Verified package copied to SD. Open Settings > Updates to install.")


if __name__ == "__main__":
    main()
