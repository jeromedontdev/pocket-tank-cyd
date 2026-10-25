"""Explicit release identities for the two supported display boards."""
from dataclasses import dataclass


@dataclass(frozen=True)
class Board:
    identity: str
    name: str
    image_prefix: str
    asset_prefix: str

    @property
    def marker(self):
        return b"PTBOARD\0" + self.identity.encode("ascii").ljust(24, b"\0")


BOARDS = {
    "cyd": Board("cyd2432s028", "ESP32-2432S028 CYD", "pocket-tank-cyd", ""),
    "elegoo": Board("elegoo28", "ELEGOO ESP32-32E 2.8-inch ILI9341V",
                    "pocket-tank-elegoo", "elegoo-"),
    "unified": Board("cydesp32profiles", "CYD / ELEGOO saved hardware profiles",
                     "pocket-tank-unified", "unified-"),
}
