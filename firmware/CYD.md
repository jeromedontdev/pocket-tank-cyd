# ESP32-2432S028 CYD

This fork targets only the 2.8-inch Cheap Yellow Display with an ESP32, SPI LCD,
and XPT2046 resistive touch. It uses a 320x240 landscape LCD, an internal-RAM
framebuffer, SD-backed icons, and the rules-based fish advisor; the 4 MB flash
layout has no model partition. Other board support, model/training code, audio,
battery/PMIC and IMU drivers have been removed from the source tree.

From an ESP-IDF PowerShell terminal in `firmware`:

```powershell
..\tools\build_cyd.ps1
..\tools\build_cyd.ps1 -Flash -Port COM4
```

The upgrade helper detects and writes only the active application slot
(`0x20000` or `0x200000`), preserving the existing
partition table, saved tank, credentials and calibration. For a brand-new board,
see the initial-install instructions in [README](../README.md).
`sdkconfig.defaults` is canonical; `sdkconfig.cyd` remains an identical alias
for existing configured build directories.

## Downloadable releases

[GitHub Releases](https://github.com/jeromedontdev/pocket-tank-cyd/releases)
provides tagged CYD alpha builds. Download the complete ZIP and read its
`INSTALL.txt`. For an existing CYD installation, back up first and use
`update.ptu` at `pocket-tank/update.ptu` on SD. Copy the included
`icons-<crc>.bin` to the card root. These packages are verified but unsigned;
install only trusted releases.

**The factory image is for new boards or deliberate resets only.** Flashing
it at `0x0` overwrites NVS, tank saves, Wi-Fi credentials, calibration and OTA
slot selection. It is not a save-preserving upgrade. The app-only image must
be written to the detected active OTA slot, not blindly to a fixed offset.
Every release includes SHA-256 checksums and the matching SD artwork.

Maintainers publish by pushing a tag such as `v0.3.8-cyd.2`. The CYD release
workflow builds with ESP-IDF v6.1, runs tool/native/emulator tests, packages
the application, factory image, SD updater and icon bank, and creates a
GitHub prerelease. The version label without `v` must fit 15 characters.
Normal pushes run checks but do not publish releases. Locally, after building:

```powershell
python ..\tools\package_cyd_release.py --build build_cyd --out ..\dist --version v0.3.8-cyd.2
```

Run this in the ESP-IDF Python environment (esptool v5 is required).
The output directory must not already exist; release files are never silently
overwritten. GitHub tags/releases provide the CYD distribution revision;
the firmware's base version remains 0.3.8 with its CYD-fork build identity.

The LCD uses SPI2; the XPT2046 uses a separate, software-driven SPI bus.
Pins and touch ADC calibration limits are in [main/board_pins.h](main/board_pins.h).
Touch readings discard the first conversion after a channel change, use a
three-sample median, and power down the ADC between polls to restore PENIRQ.
Pressure thresholds are 150 for a new press and 100 to maintain a press,
so light touches register without chattering near the threshold.
The default finger offset is zero on CYD. The raw ADC limits are defaults,
not a per-device calibration; physical accuracy still needs to be checked.
For per-device calibration, open Settings and tap **TOUCH CALIBRATION** at the bottom right,
or send `touch calibrate` through the director console. Hold the stylus on
each of the five crosses briefly, then lift it. The first three points fit
an affine coordinate map; the remaining two must agree within six framebuffer
pixels before the map is saved to NVS. This corrects offsets, scale, swapped
or mirrored axes, and skew without changing LCD settings. A bad fit restarts
the targets; a 90-second inactivity timeout or an orientation change cancels
without replacing the previous calibration.
If a CYD revision reports touch coordinates inverted or clipped, adjust those
CYD touch constants and rebuild.

The tank uses a 240x180 internal-RAM framebuffer, scaled to the 320x240 LCD.
Menus retain their full 448x368 layout and are scaled into that framebuffer,
rather than cropped. Menu touch coordinates use the inverse scaling.
The display sends RGB565 pixels most-significant byte first. For panel color
checks, `CYD_DIAGNOSTIC_PATTERN` in [main/display_port_cyd.c](main/display_port_cyd.c)
temporarily replaces the image with red, green, blue, white, black, yellow,
cyan, and magenta ramps; leave it at `0` for normal operation.
The current panel initialization uses the ST7789-style timing/gamma sequence
selected for the connected CYD revision, through the compatible SPI panel
transport. The exact controller has not been confirmed by register reads.
Orientation is handled in the pixel mapping; per-frame orientation updates do
not rewrite panel scan-direction registers. This keeps orientation changes
separate from color/timing settings. Other CYD revisions may need different
initialization; correct primary-color bands alone are not sufficient to
validate midtone reproduction.
The CYD has no onboard IMU, so its orientation is fixed and Settings omits
the rotation-lock control and its touch target. Rotation fields remain in
the save format for compatibility but cannot flip the CYD display.

The shared decoration renderer requires a contiguous 60,024-byte scratch
buffer. CYD reserves it in internal RAM immediately after the framebuffer,
before peripheral initialization fragments the heap. Deferring this allocation
until a decoration is first drawn can fail even with enough total free RAM,
causing a saved pumpkin to crash repeatedly at boot.

The director console uses UART0 through the board's USB-to-serial adapter
at 115200 baud (for example, `python ..\tools\director.py -p COM4 milestones`).

## Feature limitations

- Fish goals use the rules advisor, not the upstream local LLM. This build
  has no model partition and the connected board has no PSRAM.
- Wi-Fi scanning has been verified on the board. Scan records and manifests
  use internal RAM. There is no default firmware update server: an unconfigured
  fork reports **NO UPDATE CHANNEL** after connecting. Manifests must identify
  `pocket-tank-cyd`; stock upstream manifests are rejected. Network connection
  and complete signed OTA installation still require separate validation.
  See [OTA](../docs/OTA.md) before enabling a fork release channel.
- SD adds asset storage, not RAM; it does not transparently replace model PSRAM.
- Audio output, sound-bank/mixer sources, the codec driver, PMIC driver,
  battery history/logging and IMU implementation have been deleted.
  Volume, battery overlays and rotation controls are absent; USB power is assumed.

## SD assets

Insert an existing FAT-formatted card before boot. The SD slot uses its own
SPI3 bus: SCLK 18, MOSI 23, MISO 19, CS 5, at 10 MHz. The firmware never formats
the card. It mounts at `/sdcard` and reads a content-versioned icon bank from
the card root. It verifies the full bank size and CRC32 before using it.
Mounting and the icon cache are deferred until after update/provisioning mode
and boot-time network synchronization, preserving Wi-Fi calibration headroom
on the no-PSRAM board. The SD console is available in normal tank mode.

The build packs all 47 icons (RGB565 plus alpha, 120,384 bytes at this revision)
from the canonical generated [icons.c](../common/icons.c). Their pixel arrays
are not linked into firmware. The desktop preview reads the same packed bank.
Only one icon is cached in RAM at a time (12 KiB maximum). Procedural fish,
decorations, text, active tank saves, and touch calibration stay on-device.
Optional JSON tank snapshots can also be stored on SD, as described below.

After building, copy `build_cyd\cyd_assets\icons.bin` to the card root using the
filename specified in `build_cyd\cyd_assets\icons_sd_bank.h`. Alternatively,
with the card inserted in the CYD and the serial console closed:

```powershell
python ..\tools\upload_cyd_icons.py build_cyd\cyd_assets\icons.bin --port COM4
```

The uploader requires pyserial. It checks that the bank matches the flashed
firmware, uploads to a new `.upload` file, and verifies both the transfer CRC
and a complete SD readback before renaming it. A matching installed bank is
left unchanged; existing destination files are never overwritten.
`sd status` reports mount/bank status. `sd cancel` removes only the named
partial upload for this build if an upload was interrupted.

Without a card or a valid bank, the tank and text menus remain usable, but
missing icons display magenta diagonal placeholders and errors are logged.
There is no embedded copy of the icon art. Card removal while running is not
supported: power off before inserting or removing the card.

### Other assets

The icon bank was the worthwhile image payload to offload. Fish and tank
decorations are mostly drawn procedurally, not read from large sprite files.
The remaining font bitmap, palettes and roster are
small; keeping them in flash preserves usable text without a card. Audio and
model weights are already absent from the CYD image, so copying them to SD
would not save more flash or supply the RAM needed by the LLM. Future large
backgrounds, sprite packs, or animations would be good SD candidates.

## Treasure chest

The shop offers **TREASURE CHEST** for **120 sand dollars**. It is
a decorative chest of gold, drawn procedurally without changing the SD icon
bank. Purchase opens the usual placement page: drag horizontally, choose
BEHIND or IN FRONT, change stacking order, and tap DONE. Its position, depth,
ownership, and rank survive reboot. MOVE and SELL work as for other
decorations; selling returns 24 dollars. Console equivalents include
`buy chest`, `place chest 180 front`, `order chest forward`, and `sell chest`.

The binary save keeps every previous field at its original offset. The
chest's original 1920-byte layout is intact; the new decoration/palette tail
extends it to 1952 bytes; the customization expansion appends another 48 bytes
to reach 2000; the Journal appends 40 bytes to reach 2040; the 20-slot fish
bank extends this to 3968; a 32-byte tail for aquarium collection identity,
active slot, tank name and care-upgrade flags extends the live save to 4000,
within the 4000-byte NVS budget. The four care flags reuse one reserved byte;
they do not shift older fields.
Reconcile extensions explicitly when changing saves.

## More decorations and color upgrades

| Item | Sand dollars | Effect |
| --- | ---: | --- |
| Scallop shell | 60 | Pearly shell on the sand |
| Starfish | 60 | Coral-colored, five-armed ornament (not a feeding resident) |
| Sunken anchor | 80 | Weathered metal anchor |
| Tethered buoy | 80 | Bobs and sways in mid-water; rope ends at a fixed floor weight |
| Sand palette | 300 | Original, gold, pearl, volcanic, rainbow, hot pink, electric cyan and neon lime |
| Mood lights | 300 | Original, blue, teal, violet, amber, rainbow, hot pink and neon lime water |
| Shipwreck | 200 | Broken hull, mast and torn sail |
| Treasure urn | 60 | Gold-filled terracotta vessel |
| Crystals | 100 | Glowing-colored crystal cluster |
| Sea fan | 80 | Tall pink branching ornament |
| Sunken ruins | 160 | Mossy stone arch |
| Jelly lantern | 120 | Bobbing jellyfish-shaped ornament tethered to a floor weight |

Use the shop's page arrows to find them. The placeable decorations listed above have the same
drag/depth/order/MOVE/SELL controls as the chest. Selling follows the existing
20% refund rule, rounded down. The buoy animates in either depth layer,
including with the optional scene cache, without reserving another large
sprite or scratch buffer. These small procedural pieces require no SD-bank update.
The jelly lantern uses the same lightweight animation approach. Console aliases
for these six decorations are `wreck`, `urn`, `crystal`, `fan`, `ruins`, `jelly`.

## Fish appearance

Open **Keeper**, tap a fish's portrait, then **CUSTOMIZE**. Tap each row to
cycle **VARIETY**, **PATTERN**, **BODY COLOR**, or **MARKING COLOR**. The preview
shows the adult look (young fry reveal markings as they grow). **DONE** saves
the new appearance; **CANCEL** leaves the fish unchanged. Drafts are isolated
from automatic saves, and a failed save keeps the page open to retry.
Body and marking colors each have 24 choices: the original eight plus vivid
pink, red, orange, yellow, lime, green, cyan, blue, violet, purple, magenta,
pastel pink/mint/lavender, white/pearl and dark slate. Existing custom colors
are preserved until you cycle their row. First-run swatches remain unchanged.

Choose Classic, Angel, Guppy, Tetra, Puffer, Shark, Jellyfish, Stingray,
Clownfish or Betta silhouettes, and Stripes, Spots,
Band or Plain markings. These are visual varieties, not separate care/species
simulations; personalities, feeding and the five-fish population cap are
unchanged. Customizing is free, including for your existing fish. Shop fry
offerings show their variety; offspring inherit variety with their body-color
parent and pattern with their marking-color parent. Console: `fish customize 0`.
For serial UI checks, `setup tap <x> <y>` activates the current setup page's
normal touch hit target using 240x180 framebuffer coordinates.
Sharks have a pointed snout, tall dorsal fin and gills; jellyfish have a
pulsing bell and waving tentacles; stingrays have flapping wings and a whip
tail; clownfish have broader stripe markings; bettas have flowing fins.
Every look supports the same 24 body colors, 24 marking colors and four
patterns. Sharks do not eat their neighbors. Jellyfish use the existing
feeding, personality and growth rules and occupy a normal fish slot; they
are not the tethered Jelly Lantern decoration. No new SD icon bank is needed.
Existing fish keep their saved look until you change it. Backups with the
five newest IDs require this firmware or newer, even though the schema and
binary save size have not changed.

## Starting over

Tap the red **WIPE** button at the top left of Settings. The separate
**WIPE TANK?** prompt explains what is erased. Tap **CANCEL**, or wait 20 seconds,
to keep the tank; only a new tap on **WIPE** in the prompt starts over.

Wiping erases the active tank and any console-parked copy: fish, bank, purchases
(including care/upkeep upgrades), milestones, Journal, palette selections and
gameplay settings. **With multiple tanks, the shared wallet is preserved;
with only the original tank, its sand dollars reset.** Purchased aquarium
slots, their names and the other tanks remain. Wipe creates and
saves a fresh pair of fry and opens first-time setup, just like a new tank.
It does not erase touch calibration, Wi-Fi credentials, SD assets, SD JSON
backups or firmware packages. Brightness is retained. Back up to SD first if
you might want your old tank back; restoring an SD backup can bring it back.

## Sand and water palettes

The two palette packs are permanent one-time unlocks, not consumable paint.
After purchase, tap the separate full-width **SAND: ...** or **WATER: ...** rows in Settings
to cycle colors. Changes are saved immediately; choosing ORIGINAL is free.
Lights change the water gradient and water-tinted scenery, not fish genetics,
brightness, feeding or the lights-out schedule. Night dimming still applies.
Other item prices are unchanged. Existing ownership and balances are preserved.
Rainbow is a static, saturated red-to-violet gradient from surface to floor,
not a flashing effect. The new colors are included in the same Mood Lights
unlock. `style water 5`, `6`, and `7` select rainbow, hot pink, and neon lime.
Older firmware will not understand the three new color IDs; use ORIGINAL
before downgrading or restoring a new-color backup onto older firmware.
Rainbow sand runs left-to-right with the original stable grain texture.
`style sand 4`, `5`, `6`, and `7` select rainbow, hot pink, electric cyan,
and neon lime. All four are included in the existing Sand Palette purchase.
Settings keeps each palette on its own line with full-size text. **CLOSE**
is at the top right; **TOUCH CALIBRATION** is at the bottom right and
**UPDATES** is at the bottom left. The Lights Out control remains hidden;
existing lighting behavior and the serial `ota check` command remain available.
**UPDATES** checks `pocket-tank/update.ptu` on SD, shows a release offer, then
requires a second tap before installing into the inactive application slot.
No Wi-Fi is needed. See [offline SD updates](../docs/OTA.md) for packaging,
USB copying, validation, safety backups and the trusted-local-file policy.

Console examples: `buy shell`, `buy starfish`, `buy anchor`, `buy buoy`,
`place buoy 180 behind`, `buy sand`, `buy lights`, `style sand 2`,
`style water 3`. These commands spend real tank currency like the shop.
`style sand 0` / `style water 0` restore the original colors.

## SD JSON tank backup and restore

Open **Settings** and tap **BACKUP TO SD**. Each successful backup creates a
new file in the card's `pocket-tank` directory: `backup-0001.json`,
`backup-0002.json`, and so on. Existing backups are not overwritten.
New exports use JSON schema 5, including item ownership, placement,
stacking, palette selections, fish appearance, Aquarium Journal progress
and all stored fish. Schema 1, 2, 3 and 4 backups
still restore, defaulting absent additions to unowned decorations and
Classic/Stripes fish (schema 1 also defaults the palettes to original).
Older backups start with an empty Journal; existing care totals and current
fish appearances qualify as you play. Open **Keeper > JOURNAL** to claim
objectives, view the appearance album and select level-unlocked backdrops.
See the README's Aquarium Journal section for XP rates and unlock levels.
Pre-bank snapshots restore with an empty fish bank. Open **Keeper > FISH BANK**
to deposit, return or swap fish; the bank pauses care and growth and keeps
20 fish independently of the five active slots. Two fish must remain active;
a full tank can still swap with the bank. Wipe clears both the tank and bank.
**Keeper > UPGRADES > TANKS** offers up to eight independent aquariums.
Each additional tank costs 500 sand dollars from the global wallet and begins
with two fry. Switching is free and requires SD. Inactive tanks are fully
paused in alternating CRC-checked binary snapshots in `pocket-tank/tanks`.
Only one tank is in RAM/NVS. Purchase/switch commits select the tank in
the same NVS blob as the save, so failed writes do not charge or switch.
Sand dollars and lifetime earnings are global; switching cannot restore a
stale balance. Each tank has its own Journal, decorations, fish and Fish Bank.
NAME TANK edits the active tank's persistent name (15 characters, `<` / `>` editor).
The shop has 36 stable item IDs, presented in ascending price order (ties keep
their item order); the optional nursery row stays first. The scenery/upkeep
extension uses ownership bits through bit 31 without changing the 4000-byte save
or schema 5. IDs 0..31 use the full 32-bit ownership mask; IDs 32..35 use the
four care flags in the reserved aquarium-metadata byte. Ownership queries
handle both stores; there are no shifts past bit 31. Do not downgrade after
buying these items: older firmware masks
off ownership bits it does not recognize.

New fixed scenery: Zen Pebbles (40), Duck Float (60), Pearl Clam
(70), Lily Pads (80), Floating Log (90), Tube Sponges (100), Lost Barrel (110)
and Dive Helmet (140), all in sand dollars. The log, lily pads and duck occupy
separate bobbing surface positions and can coexist. Fixed scenery is behind
fish, with floor scenery behind movable pieces. There is no MOVE control;
SELL requires two taps and refunds the usual 20 percent. Failed saves roll
back both purchases and sales of the new scenery.
Slow Garden (160) halves grass growth and Bio Filter (180) halves new algae
growth in both awake and offline simulation. Neither clears existing growth,
generates chore rewards or changes other care rates; both are per tank.
New purchases roll back their balance/ownership if the internal save fails.
Menu actions share the dark fill and pale teal outline; selected tabs and
letters use a white outline. Aquarium and bank pagination use `<` / `>`.
All original and added shop icons are muted gray while unowned and full color
after purchase, both in rows and detail dialogs. This indicates ownership,
not affordability; the price and UNLOCK button show whether you can buy.
JSON backup/restore and Wipe affect the active tank only; purchased slots
and inactive tanks remain. Multi-tank Wipe preserves the shared wallet; JSON
restore replaces it with the backup's balance. Names remain tied to slots.
Archive individual JSON exports and the tanks directory; there is no automatic
collection recovery after erasing NVS. Do not downgrade to older firmware.
The console `tanks` opens the page, `tanks tap <x> <y>` uses framebuffer
coordinates, and `tanks demo` parks the real tank before granting temporary
test sand dollars. Use `restore` after staging; never Wipe while a real tank is parked.
Schema 5 files require this firmware or newer; old firmware will refuse them.
The serial `sd verify` diagnostic exercises SD write/readback and restore
validation (including safety-backup memory usage), removes its temporary
verification snapshots, and leaves the tank and normal backups unchanged.
Keep your pre-upgrade backup if you plan to downgrade.
The status line reports success or failure. New files are flushed, synced,
fully read back and checked using CRC32, validated, and only then renamed from
their temporary name to `.json`.

Tap **RESTORE FROM SD**, then **CONFIRM RESTORE** within 10 seconds to restore
the highest-numbered backup. Restore replaces the active tank and restarts
the board. Before committing NVS, it creates a separate
`before-restore-0001.json` snapshot of the current tank; if that safety backup
fails, restore is refused. A failed or malformed restore is reported on
screen and in serial logs, without intentionally replacing the active tank.

Console commands (normal tank mode):

```text
sd backup
sd backups
sd export
sd restore
sd restore confirm
sd restore 2 confirm
sd undo 1 confirm
```

`sd backups` reports the newest normal and safety snapshot paths.
`sd export` prints the latest normal backup between `JSON BEGIN` / `JSON END`
markers. `restore 2` chooses `backup-0002.json`; omitting the number chooses
the latest. `undo 1` chooses `before-restore-0001.json`; omitting its number
chooses the latest safety snapshot. Without `confirm`, restore/undo only
validates the file and prompts; it does not replace the tank.

To archive on a computer or supply a JSON file, power off before moving the
card. Copy snapshots out of `pocket-tank`, or copy a compatible snapshot into
that directory with an unused `backup-NNNN.json` name (0001 through 9999).
Use the numbered serial command to select an older file. Incomplete `.tmp`
files are never offered for restore; an interrupted write is not silently
overwritten. Move its matching `.tmp` file aside while powered off before
retrying that backup slot.

Schema 1 is readable JSON with named tank fields and a fish array, not a
base64 NVS blob. It includes names, colors, age, drives/personality, family
links, milestones and seen flags, sand dollars and earning ledger, owned
items and placement, vegetation, algae, and saved gameplay settings. Algae
cells are a compact hexadecimal string; numeric arrays use the frozen save
order (vegetation is bed-major). An empty saved name means to use that fish's
preset name rather than a custom name. Restore validates required fields, types,
array lengths, finite numeric ranges, schema, board, and framebuffer size.
The size limit is 32 KiB and nesting is bounded. JSON compatibility is
currently this CYD profile (240x180), not automatic cross-board migration.

This is the same durable state as a normal tank save, not a frame-exact
memory dump: fish positions and transient animations are rebuilt at boot.
Restore resumes the snapshot without simulating the time spent in the
archive. Wi-Fi credentials, device brightness, and touch calibration are
not exported or overwritten. Normal saves still use NVS and do not require
SD. Backups contain no embedded icons or firmware.

Fish-care shop upgrades: Calm Current (160) gives 25% faster resting/sleep
energy recovery; Cozy Shelter (180) gives 25% faster base stress decay awake
and asleep; Play Kit (200) gives 25% faster exploring curiosity gains;
Training Ring (240) adds 25% of the base calm-hold trust rate, for 225% total
with a hitched seahorse. Food rewards and negative drives are unchanged.
They are permanent per-tank purchases, clear on active-tank wipe, and persist
in NVS, SD tank snapshots and portable JSON. Older backups default to none.
The 4000-byte binary save and JSON schema 5 are retained; JSON adds optional
`tank.stat_upgrades` (integer 0..15), binary saves use a reserved metadata byte.
Console purchase keys: `current`, `shelter`, `play`, `training`.

### Focused tests

With an existing host C compiler and firmware dependencies configured:

```powershell
python ..\tools\test_tank_backup.py --cc clang
python -m unittest discover -s ..\tools -p "test_*.py"
python ..\sim\run.py --cc clang --smoke
```

`--cc` can also name a GCC-compatible compiler or the full path to `zig`
(the runner adds `cc` for Zig). The native suite covers purchase/sale,
placement and persistence, JSON round-trip, invalid inputs without writes,
failed NVS writes, frozen legacy save fixtures, cached/uncached rendering,
and Settings restore confirmation/timeout/fixed orientation. The desktop preview
uses the actual 240x180 renderer and rules advisor via a temporary native library,
displayed in Python's Tk window. It requires no SDL or LVGL and keeps a separate
local save. The headless smoke check renders distinct Tank/Keeper/Settings/Shop
pages and validates JSON restore and saving.