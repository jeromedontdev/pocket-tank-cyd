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


def release_version(value):
    value = value.removeprefix("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+", value) or len(value) > 15:
        raise ValueError("Use vMAJOR.MINOR.PATCH, at most 15 characters without v")
    return value


def create_bundle(build, output, version):
    version = release_version(version)
    build, output = Path(build), Path(output)
    image = (build / "pocket_tank.bin").read_bytes()
    update = package(image, version)
    args = json.loads((build / "flasher_args.json").read_text())
    expected = {0x1000, 0x8000, 0xf000, 0x20000}
    if ({int(offset, 0) for offset in args["flash_files"]} != expected or
            args["extra_esptool_args"]["chip"] != "esp32"):
        raise ValueError("Unexpected CYD initial-install flash layout")
    if output.exists():
        raise FileExistsError(f"Release output already exists: {output}")
    output.mkdir(parents=True)
    factory = output / "pocket-tank-cyd-factory.bin"
    command = [sys.executable, "-m", "esptool", "--chip", "esp32", "merge-bin",
               "-o", str(factory.resolve()), *args["write_flash_args"]]
    for offset, name in args["flash_files"].items():
        command.extend((offset, str((build / name).resolve())))
    subprocess.run(command, check=True)
    # These assets are shared by the standalone downloads and the all-in-one ZIP.
    (output / "pocket-tank-cyd-app.bin").write_bytes(image)
    (output / "update.ptu").write_bytes(update)
    source = Path(__file__).resolve().parent.parent
    shutil.copyfile(source / "LICENSE", output / "LICENSE")
    instructions = f"""Pocket Tank CYD {version}
ESP32-2432S028 only. Not for other ESP32 displays.

EXISTING CYD INSTALLATION (preserve your tanks)
1. Use Settings > SD BACKUPS > BACKUP TO SD before updating.
2. Power off before moving the SD card.
3. Menu artwork is embedded in firmware; no SD icons are required.
4. Copy update.ptu to pocket-tank/update.ptu on the SD card.
5. Reinsert the card, power on, and use Settings > Device > SD Updates to install.
The SD updater verifies board, size and SHA-256. Packages are not signed:
only install files from a source you trust.

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
    (output / "INSTALL.txt").write_text(instructions, encoding="utf-8")
    notes = f"""<p align="center"><a href="https://jeromedontdev.github.io/pocket-tank-cyd/"><img src="https://raw.githubusercontent.com/jeromedontdev/pocket-tank-cyd/main/web/logo.svg" width="320" alt="Pocket Tank CYD aquarium logo"></a></p>

## Pocket Tank CYD {version}

Release for **ESP32-2432S028 CYD only**.

Pocket Tank CYD began as a board port of upstream Pocket Tank v0.3.8. It is
now **a distinct aquarium game**, retaining and crediting that simulation
while adding its own modes, shared progression, genetics, stories and games.

### New in 2.1.1: fullscreen browser game and precise feeding

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
    (output / "RELEASE-NOTES.md").write_text(notes, encoding="utf-8")
    archive = output / f"pocket-tank-cyd-{version}.zip"
    members = sorted(path for path in output.iterdir() if path.is_file())
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as bundle:
        for path in members:
            bundle.write(path, path.name)
    checksums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
                        for path in sorted(output.iterdir()) if path.is_file())
    (output / "SHA256SUMS.txt").write_text(checksums, encoding="ascii")
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    print(create_bundle(args.build, args.out, args.version))


if __name__ == "__main__":
    main()
