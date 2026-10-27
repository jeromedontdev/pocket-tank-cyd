"""Create a CYD-only GitHub release bundle from an existing ESP-IDF build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

from make_sd_update import package
from board_profiles import BOARDS
from provision_factory import provision_factory


def release_version(value):
    value = value.removeprefix("v")
    if (not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value) or len(value) > 15 or
            any(int(part) > 255 for part in value.split(".")) or value == "0.0.0"):
        raise ValueError("Use vMAJOR.MINOR.PATCH, at most 15 characters without v")
    return value


def create_bundle(build, output, version, board="cyd"):
    profile = BOARDS[board]
    prefix, asset_prefix = profile.image_prefix, profile.asset_prefix
    version = release_version(version)
    build, output = Path(build), Path(output)
    image = (build / "pocket_tank.bin").read_bytes()
    if image[48:80].split(b"\0", 1)[0] != version.encode("ascii"):
        raise ValueError("Embedded ESP-IDF app release differs from the requested release")
    update = package(image, version, profile.identity.encode("ascii"))
    args = json.loads((build / "flasher_args.json").read_text())
    expected = {0x1000, 0x8000, 0xf000, 0x20000}
    if ({int(offset, 0) for offset in args["flash_files"]} != expected or
            args["extra_esptool_args"]["chip"] != "esp32"):
        raise ValueError("Unexpected CYD initial-install flash layout")
    if output.exists():
        raise FileExistsError(f"Release output already exists: {output}")
    output.mkdir(parents=True)
    factory = output / f"{prefix}-factory.bin"
    command = [sys.executable, "-m", "esptool", "--chip", "esp32", "merge-bin",
               "-o", str(factory.resolve()), *args["write_flash_args"]]
    for offset, name in args["flash_files"].items():
        command.extend((offset, str((build / name).resolve())))
    subprocess.run(command, check=True)
    if board == "unified":
        for hardware in ("cyd", "elegoo"):
            (output / f"{prefix}-{hardware}-factory.bin").write_bytes(
                provision_factory(factory.read_bytes(), hardware))
    # These assets are shared by the standalone downloads and the all-in-one ZIP.
    (output / f"{prefix}-app.bin").write_bytes(image)
    (output / f"{asset_prefix}ota.json").write_text(json.dumps({
        "format": 1, "fork": "pocket-tank-cyd", "board": profile.identity,
        "release": version, "min_from": "2.3.1",
        "note": "Stable CYD release. Back up your aquarium before updating.",
        "app": {
            "url": f"https://github.com/jeromedontdev/pocket-tank-cyd/releases/download/v{version}/{prefix}-app.bin",
            "size": len(image), "sha256": hashlib.sha256(image).hexdigest(),
        },
    }, indent=2) + "\n", encoding="utf-8")
    (output / f"{asset_prefix}update.ptu").write_bytes(update)
    source = Path(__file__).resolve().parent.parent
    shutil.copyfile(source / "LICENSE", output / f"{asset_prefix}LICENSE")
    instructions = f"""Pocket Tank CYD {version}
ESP32-2432S028 only. Not for other ESP32 displays.

EXISTING CYD INSTALLATION (preserve your tanks)
1. Use Settings > SD BACKUPS > BACKUP TO SD before updating.
2. Power off before moving the SD card.
3. Menu artwork is embedded in firmware; no SD icons are required.
4. Copy update.ptu to pocket-tank/update.ptu on the SD card.
5. Reinsert the card, power on, and use Settings > Device > Firmware Updates to install.
   Published 2.3.4 and older call that entry SD Updates.
The SD updater verifies board, size and SHA-256. Packages are not signed:
only install files from a source you trust.

WI-FI UPGRADE (once running 2.3.2 or newer)
Settings > Device > Firmware Updates > Check GitHub via Wi-Fi checks the latest
stable release from this fork and asks before installing a newer version.
The aquarium must save before restarting into update mode. Keep power on.
HTTPS, exact size, CYD board, embedded release and full-file SHA-256 are checked
before the inactive slot is selected. Standard images are not publisher-signed:
this trusts the GitHub release account. USB/SD remain available offline.

USB APP-ONLY UPGRADE
pocket-tank-cyd-app.bin is NOT a factory image.
Browser option: https://jeromedontdev.github.io/pocket-tank-cyd/flash.html
Choose save-preserving USB update, NOT the factory installer. It validates
the live board/OTA layout and writes only the detected running application.
Back up first, close serial monitors and keep USB power connected.
Use the source checkout's tools\\build_cyd.ps1 -Flash, which detects the
active app slot, or query `ota` over serial and write the app at the reported
active slot (ota_0: 0x20000; ota_1: 0x200000).
Do not assume the active slot or overwrite the partition table or NVS.

INTERNAL AQUARIUM STORAGE (2.3.4+)
Keep the original SD inserted for the first upgraded boot to import existing
inactive tanks. Import leaves SD originals intact; they stop updating afterward.
Buying/switching all eight tanks then works without SD. A missing import needs
the original card again; never erase internal storage to force stale fallback.
For current JSON backups visit each tank and back it up; a single JSON file
does not include all slots. A full flash recovery image preserves the collection
identity and internal archive files. Backups/photos/offline updates remain on SD.
Do not downgrade to SD-only tank firmware: it sees old, unsynchronized snapshots.

NEW BOARD / DELIBERATE FACTORY RESET ONLY
WARNING: the factory image overwrites NVS, saved tanks, Wi-Fi credentials,
touch calibration and OTA slot selection. Never use it for a normal upgrade.
With esptool v5 installed:
python -m esptool --chip esp32 --port COM4 --baud 460800 write-flash 0x0 pocket-tank-cyd-factory.bin
Complete first-time setup; no SD artwork installation is required.
Back up the entire SD card separately; NVS reset loses collection identity.

Do not downgrade: the extended CYD save format is not supported upstream.
Source, build instructions, full feature guide and screenshots:
https://github.com/jeromedontdev/pocket-tank-cyd
"""
    if board == "unified":
        instructions = f"""Pocket Tank CYD {version}: one application for CYD and ELEGOO

EXISTING AQUARIUM: back up first. Use the website save-preserving USB updater,
selecting the actual hardware. It verifies the running slot, partition layout,
installed family, download and written app, then provisions the saved profile.
Legacy board-specific firmware must migrate by USB; its Wi-Fi/SD channels stay
board-specific. Never use a factory image to migrate an existing aquarium.
Local alternative: tools\\build_cyd.ps1 -Board unified -HardwareProfile cyd
  -Port YOUR_PORT -Flash -Backup YOUR_NEW_BACKUP_FILE
Choose elegoo instead of cyd for the ELEGOO 2.8-inch ILI9341V board.

ONCE UNIFIED: use unified-ota.json for Wi-Fi, or rename unified-update.ptu to
pocket-tank/update.ptu on SD. Updates preserve the saved hardware profile,
calibration, Wi-Fi, all internal aquariums and collection identity.

NEW BOARD / DELIBERATE RESET ONLY:
The unified-cyd-factory and unified-elegoo-factory images contain the EXACT
SAME application but different blank NVS hardware profiles. Choose the actual
board. Flash the chosen merged image at 0x0; THIS ERASES EXISTING SAVES.
The unconfigured pocket-tank-unified-factory.bin waits over USB at 115200 baud
for hardware cyd or hardware elegoo before initializing the display.

SCREEN-INDEPENDENT RECOVERY at 115200 baud:
hardware                  query saved profile and image family
hardware cyd              save aquarium, persist CYD profile and restart
hardware elegoo           save aquarium, persist ELEGOO profile and restart
Calibration is not erased. Do not guess pins or rely on all-zero controller IDs.
ELEGOO GPIO34 is battery sensing, not an LDR. Manual brightness works.
This supports only the two tested ESP32/4 MiB/SPI/XPT2046 boards, not all CYDs.

Backups/photos/offline updates remain optional SD features. Eight aquariums
are stored internally. A JSON backup covers the active aquarium/shared state,
not all archives; a full 4 MiB flash dump preserves the complete collection.
Only install trusted downloads; these images are not publisher-signed.
"""
    if board == "elegoo":
        instructions = instructions.replace("ESP32-2432S028 only", profile.name + " only")
        instructions = instructions.replace("pocket-tank-cyd-app.bin", f"{prefix}-app.bin")
        instructions = instructions.replace("pocket-tank-cyd-factory.bin", f"{prefix}-factory.bin")
        instructions = instructions.replace("Copy update.ptu", "Copy elegoo-update.ptu")
        instructions = instructions.replace("tools\\build_cyd.ps1 -Flash", "tools\\build_cyd.ps1 -Board elegoo -Port YOUR_PORT -Flash")
        instructions = instructions.replace("--port COM4", "--port YOUR_PORT")
        instructions = instructions.replace("Read INSTALL.txt", "Read elegoo-INSTALL.txt")
        instructions += "\nSelect ELEGOO in the web installer. Do not use the original CYD image.\nReplace YOUR_PORT with the verified ELEGOO port; never guess another aquarium's port.\nIf the board already has the wrong CYD identity/white LCD, use the documented backed-up app-only board conversion, not factory reset.\nGPIO34 is battery sensing, not room light; audio and battery charging are not game features.\n"
    instructions += "\n2.3.10 SAVE COMPATIBILITY: back up before updating. The version-6 companion\nrecord stores mussel colors in a reserved byte; save size/offsets are unchanged.\nOlder firmware cannot read this record. Do not downgrade after saving with 2.3.10.\n"
    (output / f"{asset_prefix}INSTALL.txt").write_text(instructions, encoding="utf-8")
    notes = f"""<p align="center"><a href="https://jeromedontdev.github.io/pocket-tank-cyd/"><img src="https://raw.githubusercontent.com/jeromedontdev/pocket-tank-cyd/main/web/logo.svg" width="320" alt="Pocket Tank CYD aquarium logo"></a></p>

## Pocket Tank CYD {version}

Release for the **ESP32-2432S028 CYD and ELEGOO 2.8-inch ILI9341V**.
The website uses one unified application with an explicit saved hardware profile.

Pocket Tank CYD began as a board port of upstream Pocket Tank v0.3.8. It is
now **a distinct aquarium game**, retaining and crediting that simulation
while adding its own modes, shared progression, genetics, stories and games.

### New in 2.3.16: exact rendering caches, hourly weather and background SD backups

- The 2.3.15 attempt stopped before publication because strict Linux C mode hid a POSIX declaration in the new backup test harness. This release fixes the harness and includes the full optimization follow-up; no 2.3.15 firmware release was published.
- Weather checks at boot and hourly, including failed attempts. Offline acquisition is bounded; optional rendering caches release RAM for verified HTTPS, then warm again after the radio stops.
- Tile-band clock command indexing and exact alpha-255 blending avoid repeated work without changing pixels. Static ornament RGB565 palettes, bounded helper/castle/crown/grass caches and compressed backdrop rows retain live-render fallbacks.
- Simulation reuses exact shrimp/fish calculations without reducing fish update frequency. Diagnostics now separate helper upkeep, social/advisor work and fish steering.
- Daily SD backups capture and semantically validate immutable JSON on the tank task, then a CPU1 worker below LCD priority writes, verifies and prunes files. Completion is identity-safe and the daily marker advances only on success. Other SD/network operations are excluded while busy.
- COM4 testing reduced observed tank-task backup service from 718 to 109 ms; worker storage/readback took 442 ms. The diagnostic frame window still reached 221 ms: snapshot validation is not hitch-free.
- Controlled adaptive five-fish fixtures measured 19.51 / 18.75 / 14.00 / 16.72 FPS versus 19.40 / 18.75 / 13.80 / 16.79 before this follow-up. Clock work fell from 8.24 to 7.88 ms in the clock-heavy fixture; p95 frame times were unchanged. Gains are small and mixed, not a universal speedup.
- Exact reference scene/frame hashes, flat/rounded tile/cache tests, backup/identity/failure tests, all sixteen COM4 hardware fixtures and verified HTTPS weather passed. Original fish and saves were retained.
- Save schema 19 / 6336 bytes and companion version 7 are unchanged from 2.3.14. Hardware profiles, partitions and calibration are unchanged. Back up first; do not downgrade to firmware that cannot read the expanded save.

### Retained from 2.3.14: fish-first rendering and detailed harbor/garden content

- Aquarium fish draw 33% smaller at every life stage, including wearables and selection rings. Saved growth, simulation sizes and tap targets are unchanged; menu portraits remain readable.
- Twenty detailed static shop decorations and six accessories: Sailor Cap, Pirate Hat, Beret, Sun Hat, Viking Helm and Scarf. Artwork stays procedural and uses bounded indexed caches, without new animation banks.
- All twelve accessories use species-aware fitting. Jellyfish support ties, bowties, bandanas and headwear; only the wraparound scarf is unavailable. Existing ownership/colors are preserved.
- Item panels use BUY, PLACE and SELL STORED. Hold a decoration to move it; confirmed REMOVE returns either an original or extra copy to shared storage without selling/refunding.
- Two alternating 40-row tiles overlap rendering and LCD transfer inside the existing framebuffer allocation. Explicit buffer ownership and frame drains preserve display correctness.
- Cost-based partial damage replaces the fixed 40% full-frame threshold. Background effects/grass use slower independent cadences while fish retain full-rate animation.
- Frame-pacing percentiles, slow-frame counts and phase maxima expose stalls. Diagnostic sync/pipeline and full/partial/adaptive comparisons are available; no universal FPS gain is promised.
- New saves use schema 19 / 6336 bytes and companion version 7. Schemas 1-18 migrate; back up first and do not downgrade after saving. No partition, calibration or hardware-profile changes.

### Retained from 2.3.11: creature polish, exact rendering caches and lossless artwork

- Overlapping helpers select the nearest eligible creature, retaining fish-first priority. Only visible shrimp and the visible seahorse receive taps; shrimp card counts follow scene detail.
- Snail cards describe grazing, seeking algae, exploring sand or returning to sand. COLORS owns its press/drag/release gesture; sliding off cancels instead of feeding or dragging the tank behind it.
- Exact snail/seahorse geometry, lazy urchin palettes and prepared fish yaw/colors reduce repeated tile work. Repeated static ornaments share one bounded indexed sprite and silhouette; animated artwork remains procedural.
- Partial LCD fingerprints use exact physical rectangles and overlap invalidation. Unaffected canonical stripes remain valid; adjacent matching damage rows merge within existing 80-row buffers. No faster SPI, reduced detail or new framebuffer.
- Embedded alpha uses lossless RLE; RGB565 artwork uses exact 4-bit/8-bit palette indices only when smaller. Every source RGB/alpha pixel is preserved, and the SD/browser bank remains 120,384 bytes with CRC32 b3de6773.
- Menu/card layout sharing and bounded menu/mini-game size optimization retain all games, artwork and TLS trust. The final unified preview was 1,728,176 bytes, leaving 237,904 bytes per OTA slot; packaged asset sizes and SHA-256 are authoritative.
- Frozen decor/clock v1 replaces the changing live-copy diagnostic. Three paired desktop cache runs retained all 48 sampled hashes; decorated CPU render times fell 5.7-11.3% in that stage. Final COM4 diagnostic scenes measured 14.12/13.59/11.73/11.62 FPS. Small final-pass differences are single-run observations, not a universal gameplay speedup.
- Verified actual LCD overlap/delayed DMA, every embedded blit path, both scales, all scene details, save migrations, browser acceptance, native simulator and fresh-backup/app-readback COM4 flashing.
- Save schema, companion version, partitions and calibration are unchanged from 2.3.10. **Back up first; do not downgrade after saving with the version-6 companion format.**

### Retained from 2.3.10: mussel creature cards and saved colors

- Tap the resident mussel for its automatic-income progress, shared daily allowance and current earning, care-pause or save-pending status. No collection action or extra income.
- Helper cards have visible next arrows to cycle through owned residents, including the mussel. Fish cards use high-contrast graphical chevrons.
- Mussel COLORS opens the shared free creature editor, also reachable through NEXT TYPE. PRIMARY tints its shell; DETAIL tints the inner mantle. SAVE COLORS commits the draft, and NATURAL remains a preview until saved.
- Saved colors are reflected in the tank, information card and shop artwork, and survive binary/JSON saves and reloads. Failed saves roll back.
- Existing saves migrate with Natural colors. Save size, offsets, partitions, hardware profiles and calibration remain unchanged.
- **Back up before updating; do not downgrade after saving with 2.3.10.** Mussel colors use a reserved byte in the version-6 companion record, which older firmware cannot read.
- Verified both-size browser tap/color/save/reload flows, five-creature palette coverage, legacy migrations, save-failure rollback, tiled rendering, menus and the unified firmware build.

### Retained from 2.3.9: aquarium performance and interaction polish

- A sandy center opening removes grass work while preserving outer beds, saved blade heights and matching shelter/trim geometry.
- Exact fish-motion and jellyfish-strand calculations are reused across tiles; partial-damage LCD regions skip checksums that cannot be reused. No reduction in visual detail, animation rate, SPI speed or color depth.
- All 36 controlled baseline frame hashes match. Desktop timings are modest/mixed; this release does not claim a measured on-device FPS increase.
- Placement outlines the selected copy and marks its starting position. Tap overlapping placed decorations to cycle different types/copies; drag horizontally to move only the selection. Clear original/extra and stored/placed feedback, cancellation and failed-save rollback.
- Calm fish occasionally investigate the resident mussel; resting fish can settle near a familiar resting companion without overriding feeding priorities.
- Successful automatic mussel payouts produce a subtle 1.6-second local gold sparkle. No permanent icon, popup, collection chore or success effect on failed payout/save load/Creative mode.
- Save schema, partitions, hardware profiles and calibration remain unchanged. Save/placement, renderer/DMA, browser and menu regressions pass.

### Retained from 2.3.8: one shop and a passive-income creature

- Pearl Mussel is a normal shop creature: 120 sand dollars per aquarium, free in Creative. No separate mussel menu or daily-care unlock.
- Healthy live aquariums automatically receive one sand dollar per ten minutes, capped at 15/day across the profile. No collection button, offline income or XP; Creative residents generate no income.
- Decoration storage and additional copies are managed from each item's normal shop entry after its first purchase, instead of a separate Inventory screen.
- Existing earned mussels and banked pearls migrate without resetting aquariums. Failed purchases and payouts roll back safely.
- The resident mask requires 2.3.8 or newer after purchasing a mussel; do not downgrade that profile, even though the save size/schema remain unchanged.
- Verified native save/migration and Creative regressions, 428 menu screens at both scales, actual browser purchase/storage/passive-income flows, and the unified firmware build.

### Retained from 2.3.7: quieter defaults and ELEGOO touch correction

- Trophies are hidden by default in new aquariums, including Creative mode. Enable them in Life > Rewards > Trophies; existing saved preferences are preserved.
- ELEGOO's fresh affine touch default reverses X and preserves Y, matching the previously working calibration rather than reversing the wrong axis. Raw coordinates and saved calibrations remain unchanged.
- Touch defaults are regression-tested for both board profiles, unified runtime selection, both UI sizes and rotation. The user confirmed touch and trophy defaults fixed; existing calibration is preserved.
- ELEGOO brightness diagnostics already show maximum duty255 with night/room dimming off, using the vendor panel/PWM settings. No brightness increase is claimed in this release.

### Unified CYD / ELEGOO profiles

- The unified application is byte-identical on the original CYD and ELEGOO ESP32-32E 2.8-inch ILI9341V. The website selects a saved hardware profile, not a different game binary. New-board factory downloads contain that same app and a board-specific blank NVS profile.
- Existing tanks migrate through the save-preserving USB updater. Legacy board-specific Wi-Fi/SD assets remain available; once unified, use unified-update.ptu / unified-ota.json. Unrelated family markers, unknown hardware, pending slots and unexpected partitions are still refused.
- ELEGOO's corrected default touch transform preserves existing saved calibration. Screen-independent USB commands hardware cyd / hardware elegoo save the aquarium before switching profiles and rebooting.
- Failed SD mounting no longer tears down the bus and disrupts LCD DMA; normal 40 MHz rendering and upright ELEGOO orientation are restored.
- ELEGOO uses ILI9341 initialization, red LED GPIO22 and disabled audio enable GPIO4. GPIO34 is battery sensing, not an LDR; automatic room-light dimming is unavailable. Touch and SD use the documented matching pins. No battery charging/audio application is added.
- The original CYD keeps its proven display initialization and pins. Both boards retain the same saves, partitions, games and menu fixes.

### Previously in 2.3.5: consistent menus and clearer updates

- Settings > Device > Firmware Updates exposes separate Check SD Card and Check GitHub via Wi-Fi actions. Versions 2.3.4 and older called this page SD Updates.
- Screenshot to SD is only under Device; weather text size is only under Clock/Weather > Weather. Removed duplicate shortcuts without removing either feature.
- Aligned Device rows and their touch targets, kept screenshot results visible and separated Wipe All Progress from ordinary actions. Cancellation, timeout and drag-off safety remain tested.
- Corrected locked-trick labels, Genes/header and journal spacing, inventory/mussel parent controls, long Wi-Fi display names, maximum game records, setup/help text and Enlarged-mode clipping.
- 434 measured menu-state renders cover both actual framebuffer sizes, plus native, browser and simulator navigation/save-preservation regressions. No partition, save-schema or gameplay changes.

### Previously in 2.3.4: all aquariums in internal flash

- Buy and switch among all eight aquariums without SD. The active profile, global Bank and decoration inventory remain internal; SD remains useful for JSON backups, screenshots and offline updates.
- Keep the original card inserted for the first upgraded boot. Missing inactive slots import from pocket-tank/tanks with CRC/readback verification; originals are retained but no longer refreshed by gameplay. Missing imports report failure and can retry with the card on a later boot.
- Never fall back to stale SD data when an internal archive exists but is corrupt. No partition or save-schema changes, no erase, no tank-price or capacity changes.
- Active saves retain two generations. Inactive archive replacements are written and readback-verified before their previous generation is retired. Eight archives plus active redundancy and one replacement pack into 80 KiB with the actual IDF SPIFFS geometry, below 75% of the existing 128 KiB partition.
- Individual JSON backups still do not contain the whole collection. Visit each tank to make current backups; preserve a full-flash recovery image for collection identity/internal slots. Old imported SD copies are historical, not current backups.

### Previously in 2.3.3: screenshot shortcut and update-mode memory

- Settings > Device added SCREENSHOT TO SD. Versions 2.3.3 and 2.3.4 also kept PHOTO TO SD under SD Backups; 2.3.5 removes that duplicate. Aquarium BMPs save to pocket-tank/photos/.
- Dedicated Wi-Fi update mode defers fish/decor scratch until returning to care. The manifest buffer is allocated after TLS connects, leaving more internal heap for GitHub release-asset certificate verification.
- Certificate validation, complete image SHA-256/size/board/version checks, explicit approval and A/B boot validation remain required. SD and Wi-Fi updates remain separate choices on the update page.

### Previously in 2.3.2: on-device GitHub updates

- Settings > Device > SD Updates now includes Check GitHub via Wi-Fi, alongside the existing offline SD flow. The aquarium must save before entering update mode.
- The default channel checks this fork's latest stable GitHub release via ota.json. Each release manifest pins its app download to the exact release tag. Newer versions require explicit UPDATE confirmation; equal/older versions report UP TO DATE.
- Require format/fork/board, strict version, HTTPS app URL, bounded exact size and SHA-256. Follow HTTPS manifest redirects without permitting an HTTP downgrade. Reject incomplete/oversized manifests.
- Verify the complete downloaded asset's SHA-256, board marker, embedded release and exact length before selecting the inactive slot. Cancellation or verification failure leaves the old boot selection intact. ESP-IDF image validation and A/B rollback remain enabled; no partition/save erase or factory image download.
- Standard releases use certificate-verified HTTPS and manifest hashes, not publisher signatures or secure boot. This trusts the release host/account; hashes do not protect against a compromised publisher. USB and SD remain supported.
- Install 2.3.2 once over save-preserving USB/SD to enable the default channel on boards running older firmware. Keep power connected and back up first.

### Previously in 2.3.1: smaller firmware and clearer inventory

- Inventory item panels have one explicit Back button. Returning to the list cancels unconfirmed sales without changing items or money; inactive list-footer controls are hidden.
- The pearl mussel explains when growth is paused because fish need feeding/calming. UI and income use the same health predicate, with bank/day-limit messages taking precedence.
- Selective size optimization for setup/menus, snapshot I/O, weather and device-management paths leaves simulation, rendering, games and per-frame progression speed-optimized. TLS is client-only because firmware initiates HTTPS rather than serving it; certificate verification and rollback stay enabled.
- Local CYD app size is 1,777,840 bytes, 19,312 smaller than 2.3.0, leaving 188,240 bytes per OTA slot. No features, artwork, save storage or update slots removed.
- Actual CYD weather and GitHub Pages HTTPS certificate validation passed. The temporary test URL was removed; the intentionally unconfigured legacy Wi-Fi OTA channel remains unchanged. The installer JSON was fetched over HTTPS but is not a legacy OTA manifest; no end-to-end Wi-Fi firmware installation is claimed.
- Extended five-fish tests passed 161,728 flat-edge and 35,175 rounded-edge full/tiled/retained frame sets. All twelve fixed aquarium reference scenes matched 2.3.0. Storage corruption, migration, weather, DMA, settings, brightness, ambient-light and browser inventory/save regressions passed.
- Existing schema-18 saves remain compatible. Use the save-preserving app update, not Factory reset.

### Previously in 2.3.0: shared decoration inventory and care-first income

- Shop > Inventory stores up to 64 physical pieces profile-wide. Buy duplicate copies, place owned pieces, move a selected copy, or store without selling; store/switch/place transfers between aquariums. Up to eight extra basic ornaments per tank have independent positions and front/back depths. Complex growing decorations remain one placed copy per type per tank; helpers, upgrades, themes and fixed scenery remain outside inventory.
- Existing layouts and purchases migrate intact. Paid/free resale provenance, two-tap stored sales, transactional save rollback and stale-backup transfer protection prevent duplicate refunds or reclaimed copies.
- Daily care pays 30/30/65 sand dollars (125 total), with unchanged 50 XP. Claiming all three tasks unlocks one free profile-wide pearl mussel. It earns one coin per 600 healthy running seconds, up to 15/day and 15 banked, collected explicitly through Shop or Inventory. No offline income, extra XP or multiplied income from extra tanks. It rests for 344 seconds then gently crawls for 16 seconds.
- Each of eighteen games now pays one dollar per qualifying saved round, up to five per game per day (90 total), with no XP. Challenge/Relaxed and all aquariums share each game's quota; save failures roll back rewards and records. Old same-day shared payouts conservatively reduce each game's remaining allowance until reset.
- Retire obsolete new-fry welcome flags for fish that have already grown. Genuine unfinished fry welcomes still resume. Welcome completion only closes after a successful save; failed saves show a retry message. No fish, names or family history are reset.
- Schema 18 / 6,168-byte saves preserve the previous 6,032-byte prefix. Back up before upgrading and do not downgrade an upgraded profile. Use the save-preserving app/OTA update, never the factory image for an ordinary upgrade.

### Previously in 2.2.7: measured CYD performance improvements

- LCD transfers fill the existing DMA halves instead of capping an 80-column band at 16 rows. Normal full-frame submissions fall from 60 to 42 without additional image buffers or a faster 40 MHz SPI clock.
- Skip vertically clipped polygon palettes, reuse identical fish-pitch calculations, shade urchin tones per row, conservatively cull seahorse coronet distance tests and memoize exact quantized tail-width math in 96 bytes. Cached translucent clock runs replay as clipped spans while preserving alpha-255 blending.
- Real CYD Normal/Balanced diagnostics measured 14.28/13.82 FPS in two fixed five-adult/elder scenes and 10.79 FPS over 1,200 frames in a five-fish copy of the saved decorated tank with clock enabled. Its lowest 120-frame window was 10.22 FPS including a 5 ms/frame service reserve. These local pre-release measurements exclude normal progression/touch/network services; ordinary four-fish monitoring still included sub-10 windows. This is not a guaranteed gameplay minimum.
- Improvements apply across both scales, all three scene details and all gameplay modes where the paths are used; LCD DMA savings are CYD-only. No artwork, fish-limit, gameplay/economy or save-schema changes.
- Serial benchmark runs two seeded fixtures and a current-tank copy in isolated RAM, uses the real LCD and automatically returns without saving synthetic residents. Final five-minute current-renderer stress passed 283,024 full/tiled/retained frame sets; all twelve reference hashes match 2.2.6. DMA tests assert capacity, ownership, transfer count and exact pixels.

### Previously in 2.2.6: menu consistency and five-fish rendering

- Wardrobe Colors uses one shared header BACK, with the selected fish's name above its preview. The old duplicate button and touch target are removed. Leaving without SAVE COLORS discards the draft; Creature Colors retains NEXT TYPE.
- Identical neighboring backdrop rows share compressed runs within the same 12 KB budget. Simple batches contiguous uncached scenery rows instead of rebuilding geometry per scanline. Fish polygon edges avoid an unnecessary integer remainder.
- Three controlled native five-fish benchmark repeats measured approximately 33-37% less render time in Simple and 15-22% less in heavier themed scenes versus 2.2.5. These are desktop renderer timings, not CYD FPS claims; all twelve reference images are unchanged.
- Perched-seahorse contact shadows now survive tile/region boundaries. New synthetic stress checks compare full, tiled and retained output across both scales, all details, palettes, clocks and cache transitions without changing the aquarium.
- No extra renderer buffers, visual downgrade, fish-limit change, gameplay/economy change, or save-schema change.

### Previously in 2.2.5: consistent typography in both display scales

- Normal's fractional font sizes use exact pixel coverage instead of unevenly repeated rows/columns. Native 5x7 and integer-size text remain pixel-exact, retaining Enlarged's crisp small labels.
- Shared menus, cards, setup, games, stories, notices, weather and clock captions use the corrected renderer. Jumbo text/Flip digits share it; segmented, analog and binary clock styles remain distinct.
- Case-sensitive Wi-Fi text uses the same rasterizer and matching smallest-size metrics. Common UI punctuation is rendered instead of disappearing into blank cells.
- Tests verify exact coverage and conserved ink area for every UI glyph, guarded row/region output, both resolutions, ASCII/UI agreement and retained clocks.
- No new buffers, font downloads, save fields, gameplay, text-layout or touch-target changes.

### Previously in 2.2.4: scenery, floating ornaments, visitors and care details

- Quieter sand grain, sparse two-tone pebbles, depth shading and low distant ridges. Kelp, coral, ruins and ice silhouettes have restrained layered tones.
- Lily pad, duck, floating log, tethered buoy and jelly lantern gain material-specific shading, veins, rings and quieter highlights.
- All five rare visitors gain dimensional details, including turtle shell scutes, manta wing folds, translucent ghost-fish edges and softly shaded jelly/squid bodies.
- Lit food pellets, fine translucent bubble rims and small mint wipe/warm trim marks using existing successful active-stroke state. No new particles or timers.
- Same food/sand/tool feedback in Simple, retaining quiet water and hidden ambient bubbles/helpers. Shared previews, original bounds and full/tile/cache/damage parity are tested at both sizes, day/night and all details.
- Helpers, castle, living coral and reef cluster are unchanged. No new graphics buffers, save fields, ownership, visit, price, care or progression changes.

### Previously in 2.2.3: dimensional fish and wearable accessories

- All ten fish varieties gain restrained back highlights, body midtones and underside shading. Fins, tails, ray wings and jellyfish bells share the lighting; head-on turns retain rounded volume.
- All six wearable accessories gain lit faces and shaded folds within their original outlines, retaining both chosen colors and Natural.
- Tank, wardrobe, setup and portraits share the same art. Existing silhouettes, movement, inherited markings/traits, growth, sleep cues and wearable attachment are retained.
- Five pre-tinted tones are selected during the existing polygon scanline pass: no per-pixel lighting, texture packs, new buffers or save changes. Daily care, progression and prices are unchanged.
- Both sizes, animated poses, wearables/colors, bounds, night lighting and full/tile/cache output are tested.

### Previously in 2.2.2: crafted aquarium ornaments and foliage

- Twenty actual tank ornaments/fixed scenery pieces gain material-specific shading: chest wood/brass, crystal facets, stone bevels, pearl sheen, sponge pores and barrel hoop rivets.
- Six fixed sand pieces get softer forms and quieter highlights: sea glass, brain coral, tiki, bottle, anemone and teapot.
- Jade seagrass and light/shadow faces on broad sword leaves preserve existing growth, sway and trimming. The sea fan now connects to its base.
- The tank and shop previews share the same artwork, including locked previews. Both sizes, all three scene details, day/night and guarded full/tile/cache output are tested.
- Existing bounds, touch targets, ownership, placement, prices, care and schema-17 saves are unchanged. No new graphics buffers, asset downloads or animations.

### Previously in 2.2.1: refined shared interface artwork

- Redraw fifteen shared menu/goal stamps with distinct silhouettes, outlines, body shading, upper-left highlights and warm details. Existing accent/locked states remain.
- Coordinate all eighteen game thumbnails with aqua/gold/rose tones, underwater backplates and shaded tiles, cards and chests.
- Faceted bronze/silver/gold medals with shaded ribbons; unearned medals retain a hollow face.
- Preserve icon bounds, labels and touch targets at both sizes. Validate distinctness, four-plus tones, clipping and guarded one/17-row tile equality, plus actual browser navigation and persistence.
- No new framebuffer, decoded art cache, PNG asset pack, PSRAM or aquarium animation cost. This first pass does not replace fish/scenery/story artwork. Gameplay, daily care, quotas and 6032-byte/schema-17 saves are unchanged.

### Previously in 2.2.0: care-first daily progression

- Goals opens TODAY: an eaten player feeding (25 dollars/12 XP), Say Hello or 60 seconds holding near a calm fish (25/13), and cleaning a colony, trimming a 10 cm counter step or five minutes watching a fed, calm, lit tank (50/25). Ready rewards are claimed once per profile per day; MILESTONES keeps the original objectives.
- Daily care totals 100 dollars and 50 XP. Repeat gestures and seahorse snacks no longer farm care money/XP. Growth, births and full-trust firsts retain their one-time dollars.
- All eighteen games, Challenge and Relaxed combined, share a five-dollar daily allowance: one dollar per qualifying round, no Journal XP. Further play and Challenge records/medals remain available; actual payouts/allowances/records save atomically.
- New milestone XP rewards are halved, totaling 1410 XP/830 dollars. Existing XP, levels, money, fish and unlocks are unchanged. Postcard rewards remain one-time.
- UTC days refresh allowances; no usable clock falls back to 24 hours of running care time. Clock synchronization does not double-pay, backward clocks do not renew allowances, missed days have no streak penalties, debt or back-pay.
- The deterministic budget targets roughly 6-8 weeks for the shop/maximum Keeper level and 10-12 weeks including all aquarium expansions with short daily visits and milestone claims, without requiring games. Optional wardrobe/fry spending extends this; this is an income model, not a human playtest guarantee.
- Saves append 24 daily-care bytes: 6032 bytes, JSON schema 17, imports schemas 1-16. Frozen old fields, shared profile quotas, transactional rollback, migrations, no-clock/rollback handling and 84 daily visits are tested. Back up before upgrading; do not downgrade upgraded saves.

### Previously in 2.1.3: clean settings, readable weather and CYD naming

- One Clock/Weather hub with Face, Colors, Text / Effects, Time / Zone, Weather and one Show Clock preview. BACK returns one level; nested Style/Text tabs and repeated preview buttons are removed.
- Visible Small/Medium/Large weather text sizing in Weather and Text / Effects; the saved size also controls clock weather/event captions. Adaptive layout keeps the face, supporting text and icons separate.
- Simple sun, partly cloudy, cloud, fog, rain, snow and storm icons from actual cached weather codes. Unknown readings do not invent conditions; stale/offline text remains.
- One Auto Feed toggle, full-width SD actions, simpler city search and confirmed city removal.
- Browser feeding keeps Place food > tap a column and removes the extra Left/Middle/Right buttons.
- Browser title/headings and this version's documentation consistently say Pocket Tank CYD. Original Pocket Tank attribution and compatibility identifiers remain.
- Existing aquarium/save bytes are reused; no schema change. Shared renderer/navigation tested at both resolutions, including actual browser input and cache/tile equality.

### Previously in 2.1.2: browser fish-card tool controls

- Wire the displayed padlock, sponge, scissors and DONE controls into browser/simulator input. The padlock toggles and saves the tank-wide tool safety lock without closing the fish card.
- Explicit tools own tank interaction until DONE; tool/lock control presses do not accidentally start cleaning/trimming strokes.
- Tested through actual browser clicks at Normal and Enlarged scales. No hardware touchscreen or save-format changes.

### Previously in 2.1.1: fullscreen browser game and precise feeding

- Fullscreen/Exit fullscreen retains game controls. Optional Touch hints show taps/drags.
- Place food allows tap-to-place feeding at a chosen column or Left/Middle/Right buttons. Food falls from the surface; existing pellet limits remain. Letterbox margins do not activate the tank.
- Public Pages builds check out the latest stable release tag. Browser version/source commit and USB installer must match before deployment; the game displays the release tag and commit. Browser WebAssembly uses the same source, not the ESP32 binary.
- No hardware gameplay or save-format changes.

### Previously in 2.1.0: scene detail, themed Jumbo and linked low-light dimming

- Play the actual game on the website with mouse/touch controls, complete browser-local saves, export/import and confirmed reset. Saves survive reload/reopen; browser storage failures are explicit. This is separate from your CYD and does not emulate Wi-Fi, live weather, SD or device hardware.
- Independent Normal/Enlarged size and Balanced/Simple/Enhanced detail; Normal + Balanced are the defaults, with saved choices preserved.
- Simple retains real stationary seagrass, all placed decorations and fixed scenery, hiding helpers/bubbles/ambient effects without removing benefits or altering care, money or progression.
- Balanced reduces grass and secondary animation at either size; Enhanced retains smooth curved vegetation and full bubble/shrimp density. Both sizes use bounded compressed backdrop caching and correct scaled tiled/region flushing.
- Warmed populated-CYD measurements: Simple about 20 FPS, Balanced about 10 FPS, Enhanced about 8-9 FPS at either size. Goals and samples are not guaranteed minimums or artificial frame caps.
- Five clock sizes include Tiny and Jumbo, preserving all seven theme designs: Minimal text, Digital segments, Analog hands, Neon glow, Flip tiles, Binary dots and Orbit rings. Primary/secondary colors, seconds/date, weather/activity, UTC/DST and per-tank persistence remain.
- Clock > Style > Text independently sizes date/AM-PM/seconds and weather/activity as Small/Medium/Large. Wrapping and automatic fitting keep long captions and every theme on screen without overlap at both resolutions.
- Room-light DIM/DIM+NIGHT uses one aggressive quadratic gain for screen and automatic rear LED: about 27% at 50% room light, 8% at 25%, 2% in darkness, with a small screen floor and existing shadow filtering/night hysteresis.
- Arrow-only pagination buttons preserve existing hit areas and navigation. Website, illustrated guide, firmware notes and installer copy updated together.
- Binary saves remain 6008 bytes/schema 16. Tiny and supporting-text sizes use spare clock metadata bits. Back up before upgrading; do not downgrade an upgraded save to firmware that cannot read these settings.

### Previously in 2.0.0: illustrated interface, modes and shared collection
- Flatter menus: independent Goals/Album, Themes under Tank, Identity under Aquariums, a two-page Fish menu and Stories/RPG Adventures hub.
- Six additional games (18 total): Lighthouse, Shell Slide, Sonar Sweep, Tide 2048, Harbor Stack and Echo Reef, with records, medals and balanced Normal/Hard coin/XP rewards.
- First-run Normal / Creative / Hard choice, fixed until a confirmed full wipe. Existing saves migrate to Normal. The aquarium selector switches layouts, not modes; money, progress, unlocks and records follow every tank.
- One global 20-slot Fish Bank: store a fish in one aquarium and return or swap it into another, preserving appearance, genes, family and bonds. Older local Banks merge safely on first visit; tanks keep their own residents and decorations.
- Creative has free fish and appearance/shop choices; Hard adds running-time starvation deaths without offline or old-age deaths. Creative progression rewards cannot leak into Normal/Hard.
- Extended verified storage avoids growing the full NVS blob; append-only migrations preserve existing saves and fixed-mode restore checks protect progression.
- Shared pixel-art chapter scenes, actual fish guides, choice icons and saved-result illustrations for The Lost Current; no changes to campaign rules or rewards.
- Three illustrated adventure destinations with route cues and traveler portraits, colored keepsakes or unfound silhouettes, and illustrated discovery hints.
- Portrait-led fish information, family/genes and milestone cues; actual visitor/decor previews in logs and object stories.
- Illustrated Home navigation, distinct thumbnails for eighteen games, visual rules/records, and clearer Goals, Album and theme previews.
- Compact static assets and existing renderers: no required PSRAM, second framebuffer, SD artwork pack or background aquarium work. Art adds no save fields.
- Focused maintenance: separate game module, named companion pages, shared hardware/simulator Home initialization, clearer transactional code and updated persistence/mode comments. Frozen save offsets, migration fixtures, licenses and upstream credits remain.
- Obsolete upstream board imagery, battery/touch logs, model-era notes, duplicate partition data and rejected naming prototypes removed; active CYD art and compatibility saves retained.
- Current binary saves are 6008 bytes; JSON schema 16 imports schemas 1-15. The profile uses existing internal SPIFFS storage, not an enlarged NVS blob. Back up before upgrading and do not downgrade an upgraded save.
- Hardware migration fix: recognize the exact empty legacy 64 KiB SPIFFS template inside the existing storage partition without permitting files, corruption or unknown nonblank content to be formatted. Blocked loads report an error and do not mark a pending OTA image valid.

### New in 1.4.0: story, games and menu polish
- The Lost Current: an optional six-chapter, 18-encounter fish-story RPG with courage, curiosity, kindness, three endings, family-linked chapter guides, and saved choices. Open Home > Adventures > The Lost Current. No real fish leave or get hurt; no care-stat changes, deadlines, or extra aquarium AI.
- Twelve games across two pages, adding Reef Defender, Current Runner, Pearl Breaker, Tide Tactics, Reef Architect and Deep Expedition, with help, pause, permanent records/medals and transactional rewards.
- Consistent BACK buttons; dedicated Fish Life, Family/Genes, Visitor Log, tank records and Display Rewards; no duplicate Fish Life in Say Hello or Home in Names & Looks.
- Clearer Comfort Assist and bond/trick explanations; stable Frame-selector lettering; bright clock sparkles and event captions under Settings > Clock/Weather > Effects.
- Wrapped clock/weather captions, useful-only fish events, visible tank emblems, cleaner summaries and a redesigned Night Manta.
- CYD Room Light/Back LED touch fixes, conservative light filtering, and a Wi-Fi screen that stays dark during startup and returns to Settings.
- Append-only 5912-byte saves and schema-14 JSON, importing schemas 1-13 and older binary saves. Back up before upgrading; older firmware cannot restore schema-14 backups.
- Refreshed illustrated documentation, actual-renderer screenshots, and Pocket Tank CYD branding across the installer and guides. The website now introduces the features and links to a separate USB flasher and local save viewer.

### New in 1.3.0
- Genetics: every fish carries a hidden second marking and body color plus four recessive traits (Shimmer, Painted Fins, Pale Belly, Gold Eye). Bred fry inherit one allele from each parent, hidden looks can resurface generations later, and rare mutations add a new marking or trait. Existing fish receive hidden genes on upgrade.
- Five new markings: Saddle, Tiger, Speckle, Fade and Mask (nine in total), also available in CUSTOMIZE.
- Family Tree (Home > Fish > Family & Genes): generation, parents, grandparents, children, siblings, shown and carried genes; departed ancestors' names are remembered.
- Visitor Log (Home > Collections > Visitor Log): five rare, harmless visitors - Drifter, Old Turtle, Ghost Fish, Night Manta and Lantern Squid.
- Seasonal sand scenery from the clock (spring sprigs, summer shells, autumn leaves, winter snow, holiday lights); north, south or off.
- Room-light sensor (Settings > Display > ROOM LIGHT): dims the backlight in a dark room, optionally switching the tank to night.
- Back RGB LED (Settings > Display > BACK LED): a soft glow that follows the water color, with optional hunger and visitor alerts.
- Daily automatic SD backups (`auto-NNNN.json`, newest seven kept) and PHOTO TO SD (BMP screenshots) in Settings > SD Backups.
- Web save viewer and a new logo on the installer site: open a JSON backup locally to see fish, family, genes and visitors.
- Append-only 5848-byte saves and schema-12 JSON, importing all prior saves and backups without reset. Older builds cannot restore schema-12 backups.

### New in 1.2.0
- Stable per-fish identities, biographies, life firsts and remembered swim buddies across reboot, Bank moves and adventures.
- Expressive Care text, eight recent real observations, helper reports and gentle discovery hints; no streaks or extra care meters.
- Tank subtitles/emblems, known founding dates, object provenance and per-fish adventure keepsake descriptions.
- Very rare harmless unknown drifter: a 24-second localized encounter after hours of lit, uncovered aquarium simulation, with a persistent sighting record.
- Six cached fixed sand decorations: Sea Glass, Brain Coral, Tiki Statue, Bottle Note, Anemone and Old Teapot; movable placement limits stay unchanged.
- Last verified SD-backup status and tank-name/version restore previews.
- Append-only 5416-byte binary saves and schema-11 JSON, importing all prior schemas without resetting tanks. Keep a pre-upgrade backup; older builds do not retain the new tail.

### New in 1.1.0
- Fish get acquainted during calm nearby swimming, form short spaced paired swims and favor stable decoration spots. Care names their buddy and favorite spot; session familiarity reforms after restart.
- Tap every helper creature for food/activity/progress, actual versus drawn shrimp counts and a direct free color-editor shortcut.
- Reversible decoration previews: DONE commits, CANCEL restores position/depth/stack/color, save failures stay open for retry, and autosaves/backups exclude drafts.
- Aquarium summaries show residents, Bank/traveler, helpers, stat upgrades, theme and clock-aware last visit. Backup scope is explicit.
- Sparse Deep Glow silhouettes, Moonlit shooting stars and treasure/crystal glints use localized 4 Hz dirty regions and preserve clock occlusion.
- Save-preserving browser USB updater: verified CYD identity/layout, app checksum/marker and detected active-slot writes, with separate acknowledged factory installation.
- Updated no-vignette CYD screenshots. Existing binary/JSON saves remain compatible; no reset or SD artwork download is needed.

### Features

- Normal (320x240, formerly Compact) is the default for fresh tanks; Enlarged is 240x180.
- Existing saved scale choices are preserved; no save reset is required.
- Opaque clock/weather panels, scene occlusion, cached artwork and 2D damage tracking.
- Tested warmed Normal aquarium around 10 FPS with and without clock; not a minimum guarantee.
- Gentler grass sway, representative shrimp schools and fewer ambient bubbles.
- Per-fish hats/ties/bowties, wearable colors and four companion-creature color palettes.
- Reorganized Home navigation and real adventures that safely store the traveling fish.
- Corrected display scaling/colors, touch calibration and readable menus.
- Flash-embedded menu artwork, verified JSON backups and offline firmware updates.
- Fish customization, Aquarium Journal and a free 20-slot Fish Bank.
- Eighteen Keeper mini-games across three pages, including Bubble Pop, Pearl Match, Fish Slalom, Treasure Cast, Feed Frenzy and Reef Puzzle.
- Real aquarium fish portraits, paused care and atomic sand-dollar plus Journal XP rewards.
- Persistent challenge-mode game records/medals and untimed/slower relaxed play.
- Home hub with fish care, bonds/tricks, collections and fish adventures.
- Permanent collectible rewards and optional aquarium trophy display.
- Optional city-configured Open-Meteo weather and activity captions alongside the idle clock.
- More distinctive Kelp Forest, Coral Reef and Deep Glow Journal backdrops.
- Eight paginated Journal backdrops, adding Sunken Ruins, Sunset Lagoon, Ice Cavern and Moonlit Reef.
- Keeper-level fish-variety unlocks for customization and shop fry; existing owned/seen looks stay available.
- Up to eight purchasable, named aquariums with a global sand-dollar wallet.
- Expanded decorations, three surface floats, upkeep and fish-care upgrades.
- Price-sorted shop, consistent buttons and ownership-colored icons.
- Seven idle clocks: Minimal, seven-segment Digital, Analog, Neon, Flip, Binary and Orbit.
- Independent primary/secondary clock colors; hide seconds, panel/border and date.
- Clear Orbit caption spacing and an outside-card arrow to cycle selected fish.
- Per-tank OFF/manual/15-second-to-10-minute activation, five sizes (including Tiny/Jumbo), separately sized supporting text and five positions.
- Selectable 1-hour/30-minute/15-minute UTC adjustment and manual DST +1-hour toggle.
- On-screen Wi-Fi setup, saved-network management and boot/on-demand NTP time.
- Full single-page QWERTY password keyboard with clear keycaps and separate numbers/symbols.
  Clocks start OFF; open Settings > CLOCK/WEATHER. Unsynchronized time says SYNC NEEDED.
- Upstream daytime fish-rest recovery fix.

### Downloads
- **pocket-tank-unified-app.bin**: the same app for both boards; migrate existing tanks with save-preserving USB.
- **pocket-tank-unified-cyd-factory.bin** / **pocket-tank-unified-elegoo-factory.bin**: new boards/reset only, same app with different blank NVS profiles.
- **unified-update.ptu** / **unified-ota.json**: subsequent updates after USB migration. Rename the SD package to `pocket-tank/update.ptu`.
- **pocket-tank-unified-{version}.zip** / **unified-SHA256SUMS.txt**: shared-image bundle and checksums.
- The following original CYD assets and their `elegoo-` counterparts remain for **legacy board-specific installations**:
- **pocket-tank-cyd-{version}.zip**: complete bundle with installation instructions.
- **update.ptu**: existing installations; copy to `pocket-tank/update.ptu` on SD.
- **pocket-tank-cyd-app.bin**: advanced, active-slot USB upgrades only.
- **pocket-tank-cyd-factory.bin**: new boards only, flashed at `0x0`.
- **SHA256SUMS.txt**: download checksums.

**Back up your tank before upgrading. Factory flashing overwrites NVS and
loses tanks, credentials, calibration and collection identity.** Do not use
the factory image for a normal update. SD packages are verified but unsigned.
Read INSTALL.txt before flashing. Do not downgrade to upstream firmware.

[Illustrated guide](https://github.com/jeromedontdev/pocket-tank-cyd/blob/v{version}/docs/CYD-FEATURES.md)
"""
    if board == "unified":
        notes = (f"## Pocket Tank CYD {version}: unified firmware\n\n"
                 "Use the website to select the actual CYD or ELEGOO profile. "
                 "The application bytes are identical; factory NVS profiles differ. "
                 "Existing aquariums must use save-preserving USB migration, not factory flashing.\n\n"
                 "After migration use unified-update.ptu (rename to update.ptu on SD) "
                 "or unified-ota.json. Read unified-INSTALL.txt. Legacy board-specific "
                 "downloads remain available; family checks stay enabled.\n")
    if board == "elegoo":
        notes = notes.replace("ESP32-2432S028 CYD only", profile.name + " only")
        notes = notes.replace("pocket-tank-cyd-app.bin", f"{prefix}-app.bin")
        notes = notes.replace("pocket-tank-cyd-factory.bin", f"{prefix}-factory.bin")
        notes = notes.replace(f"pocket-tank-cyd-{version}.zip", f"{prefix}-{version}.zip")
        notes = notes.replace("**update.ptu**", "**elegoo-update.ptu**")
        notes = notes.replace("**SHA256SUMS.txt**", "**elegoo-SHA256SUMS.txt**")
        notes = notes.replace("Read INSTALL.txt", "Read elegoo-INSTALL.txt")
    (output / f"{asset_prefix}RELEASE-NOTES.md").write_text(notes, encoding="utf-8")
    archive = output / f"{prefix}-{version}.zip"
    members = sorted(path for path in output.iterdir() if path.is_file())
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as bundle:
        for path in members:
            bundle.write(path, path.name)
    checksums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
                        for path in sorted(output.iterdir()) if path.is_file())
    (output / f"{asset_prefix}SHA256SUMS.txt").write_text(checksums, encoding="ascii")
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--board", choices=BOARDS, default="cyd")
    args = parser.parse_args()
    print(create_bundle(args.build, args.out, args.version, args.board))


if __name__ == "__main__":
    main()
