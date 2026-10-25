"""Backed-up, app-only migration to unified firmware with explicit hardware."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from board_profiles import BOARDS

SLOT_SIZE = 0x1e0000
SLOTS = {"ota_0": 0x20000, "ota_1": 0x200000}


def detect_slot(text):
    matches = re.findall(r"running from (ota_[01]), next boot (ota_[01])", text)
    if not matches or len(set(matches)) != 1 or matches[0][0] != matches[0][1]:
        raise ValueError("Missing/conflicting OTA slot response or pending update; no flash written")
    return matches[0][0]


def validate_image(image):
    if (not 0x140 <= len(image) <= SLOT_SIZE or image[0] != 0xe9 or
            image[12:14] != b"\0\0" or image[0x120:0x140] != BOARDS["unified"].marker):
        raise ValueError("Expected unified ESP32 app within the existing slot size")


def validate_backup(data, slot, hardware):
    if len(data) != 0x400000:
        raise ValueError("Full backup is not 4 MiB; no flash written")
    layout = [(1, 2, 0x9000, 0x6000, "nvs"), (1, 0, 0xf000, 0x2000, "otadata"),
              (1, 1, 0x11000, 0x1000, "phy_init"), (0, 16, 0x20000, SLOT_SIZE, "ota_0"),
              (0, 17, 0x200000, SLOT_SIZE, "ota_1"), (1, 130, 0x3e0000, 0x20000, "storage")]
    import struct
    import zlib
    for index, expected in enumerate(layout):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from(
            "<HBBII16sI", data, 0x8000 + 32 * index)
        if (magic != 0x50aa or flags != 0 or
                (kind, subtype, address, size, label.rstrip(b"\0").decode("ascii")) != expected):
            raise ValueError("Unexpected partition layout; no flash written")
    if struct.unpack_from("<H", data, 0x80c0)[0] not in (0xebeb, 0xffff):
        raise ValueError("Unexpected additional partitions; no flash written")
    records, seen = [], 0
    for offset in (0xf000, 0x10000):
        record = data[offset:offset + 32]
        if record == b"\xff" * 32:
            continue
        seen += 1
        sequence, state, crc = (struct.unpack_from("<I", record, position)[0]
                                for position in (0, 24, 28))
        expected_crc = zlib.crc32(record[:4], 0xffffffff)
        if (sequence in (0, 0xffffffff) or crc != expected_crc or
                state not in (2, 3, 4, 0xffffffff)):
            raise ValueError("Corrupt/pending OTA selection; no flash written")
        if state in (2, 0xffffffff):
            records.append(sequence)
    selected = (max(records) - 1) % 2 if records else 0
    if (seen and not records) or slot != f"ota_{selected}":
        raise ValueError("OTA metadata disagrees with running slot; no flash written")
    marker = data[SLOTS[slot] + 0x120:SLOTS[slot] + 0x140]
    if marker not in (BOARDS[hardware].marker, BOARDS["unified"].marker):
        raise ValueError("Installed family differs from selected hardware; no flash written")
    return marker


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--hardware", choices=("cyd", "elegoo"), required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    args = parser.parse_args()
    image = args.image.read_bytes()
    validate_image(image)
    # Reserve a new file before touching the device; never overwrite a recovery dump.
    with args.backup.open("xb"):
        pass
    import serial

    def connect():
        port = serial.Serial()
        port.port, port.baudrate, port.timeout = args.port, 115200, .1
        port.dtr = port.rts = False
        port.open()
        return port

    def read(port, seconds):
        result = ""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            result += port.readline().decode("utf-8", "replace")
        if any(s in result for s in ("Guru Meditation", "CORRUPT HEAP", "assert failed")):
            raise RuntimeError("Firmware crash reported")
        return result

    def command(port, text, seconds=3):
        port.write((text + "\n").encode("ascii"))
        port.flush()
        return read(port, seconds)

    def reset(port):
        port.dtr = False
        port.rts = True
        time.sleep(.1)
        port.rts = False

    def tool(*parameters):
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32",
                        "--port", args.port, "--baud", "460800", *map(str, parameters)], check=True)

    hardware = BOARDS[args.hardware].identity
    with connect() as port:
        read(port, 1)
        slot = detect_slot(command(port, "ota"))
        # A fresh banner binds the selected legacy identity, or the saved unified profile.
        reset(port)
        boot = read(port, 30)
        version = re.search(r"pocket-tank v(\d+\.\d+\.\d+) CYD ", boot)
        target = image[48:80].split(b"\0", 1)[0].decode("ascii")
        if not version or not re.fullmatch(r"\d+\.\d+\.\d+", target):
            raise RuntimeError("Release version was not confirmed; no flash written")
        if tuple(map(int, version[1].split("."))) > tuple(map(int, target.split("."))):
            raise RuntimeError("Downgrade refused; no flash written")
        if f"for {hardware} boot" not in boot:
            if "for cydesp32profiles boot" not in boot:
                raise RuntimeError("Boot identity differs from selected hardware; no flash written")
            if f"hardware profile {hardware} | image cydesp32profiles" not in command(port, "hardware"):
                raise RuntimeError("Saved hardware profile differs from selection; no flash written")
        if detect_slot(command(port, "ota")) != slot:
            raise RuntimeError("Slot changed across reboot; no flash written")
        saved = command(port, "save")
        if "saved" not in saved.lower() or "fail" in saved.lower():
            raise RuntimeError("Aquarium save was not confirmed; no flash written")
    tool("read-flash", "0", "0x400000", args.backup)
    data = args.backup.read_bytes()
    validate_backup(data, slot, args.hardware)
    address = SLOTS[slot]
    with tempfile.TemporaryDirectory(prefix="unified-flash-") as temp:
        rollback, readback = Path(temp) / "rollback.bin", Path(temp) / "readback.bin"
        rollback.write_bytes(data[address:address + SLOT_SIZE])
        try:
            tool("write-flash", hex(address), args.image)
            with connect() as port:
                reset(port)
                boot = read(port, 4)
                if "profile required:" in boot:
                    boot += command(port, "hardware " + args.hardware, 30)
                else:
                    boot += read(port, 26)
                expected = f"hardware profile {hardware} | image cydesp32profiles"
                if expected not in command(port, "hardware"):
                    raise RuntimeError("Unified hardware profile was not confirmed")
                if detect_slot(command(port, "ota")) != slot:
                    raise RuntimeError("Unified slot differs from detected slot")
            tool("read-flash", hex(address), len(image), readback)
            if readback.read_bytes() != image:
                raise RuntimeError("Full app readback differs")
            with connect() as port:
                reset(port)
                read(port, 30)
                if expected not in command(port, "hardware"):
                    raise RuntimeError("Saved profile did not survive reboot")
        except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt):
            print("Unified update failed: restoring the backed-up app slot; NVS/storage were not flashed",
                  file=sys.stderr, flush=True)
            tool("write-flash", hex(address), rollback)
            raise
    print(f"Verified unified app on {args.port}, profile {hardware}, {slot}; "
          f"SHA256 {hashlib.sha256(image).hexdigest()}. Full recovery backup: {args.backup}")


if __name__ == "__main__":
    main()
