"""Create new-board factory images with explicit profiles; never use on existing tanks."""
from pathlib import Path
import subprocess
import sys
import tempfile

from board_profiles import BOARDS

PROFILES = {"cyd": 1, "elegoo": 2}


def provision_factory(factory, board):
    profile_id = PROFILES[board]
    if factory[0x20120:0x20140] != BOARDS["unified"].marker:
        raise ValueError("Profile provisioning requires the unified factory image")
    with tempfile.TemporaryDirectory(prefix="tank-factory-profile-") as temp:
        csv, output = Path(temp) / "profile.csv", Path(temp) / "profile.bin"
        csv.write_text("key,type,encoding,value\npt_hardware,namespace,,\n"
                       f"profile,data,u8,{profile_id}\n", encoding="ascii")
        subprocess.run([sys.executable, "-m", "esp_idf_nvs_partition_gen",
                        "generate", str(csv), str(output), "0x6000"], check=True)
        nvs = output.read_bytes()
        if len(nvs) != 0x6000:
            raise ValueError("NVS generator returned an unexpected partition size")
    result = bytearray(factory)
    result[0x9000:0xf000] = nvs
    return bytes(result)
