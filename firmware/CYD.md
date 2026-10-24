# ESP32-2432S028 CYD

<p align="center"><img src="../web/logo.svg" width="200" alt="Pocket Tank CYD aquarium logo"></p>

This fork targets only the 2.8-inch Cheap Yellow Display with an ESP32, SPI LCD,
and XPT2046 resistive touch. It uses a 320x240 landscape LCD, an internal-RAM
framebuffer, flash-resident icons, and the rules-based fish advisor; the 4 MB flash
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

For a new board or deliberate reset, the
[USB web installer](https://jeromedontdev.github.io/pocket-tank-cyd/flash.html) works in
desktop Chrome/Edge without ESP-IDF. **Save-preserving USB update** validates a running CYD
firmware's boot identity and `ota` response, detects its active slot and writes
only the application, preserving NVS and the partition table. Back up first.
The **factory installer** writes the factory image at `0x0` and overwrites saved
tanks/settings; it is only for new boards or deliberate resets.
Close serial monitors before connecting. The `CYD web installer` workflow checks
the latest stable release's application/factory checksums and board markers before deploying
the static site through GitHub Pages (Actions source). New releases dispatch
this workflow automatically; maintainers can also run it manually.

[Latest built release](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest)
provides independently versioned CYD builds, starting with 1.0.0. Download the complete ZIP and read its
`INSTALL.txt`. For an existing CYD installation, back up first and use
`update.ptu` at `pocket-tank/update.ptu` on SD. Menu artwork is embedded;
no SD icon download is required. These packages are verified but unsigned;
install only trusted releases.

**The factory image is for new boards or deliberate resets only.** Flashing
it at `0x0` overwrites NVS, tank saves, Wi-Fi credentials, calibration and OTA
slot selection. It is not a save-preserving upgrade. The app-only image must
be written to the detected active OTA slot, not blindly to a fixed offset.
Every release includes SHA-256 checksums. Artwork is included in the firmware,
not distributed as a separate SD icon bank.

### Install a downloaded build (no ESP-IDF required)

**Existing CYD installation, keeping your tanks:**

1. Make an SD tank backup in Settings and keep a separate copy of the card.
2. Download [update.ptu](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/update.ptu)
   from the latest release; no artwork download is required.
3. Power off before moving the card. Put
   the update at `pocket-tank/update.ptu` (create `pocket-tank` if needed).
4. Reinsert the card, power on, open **Settings > Device > SD Updates > Check SD Card**,
   then **Install and Restart**. Keep power connected until it finishes.

**Brand-new board or deliberate factory reset:**

1. Download [pocket-tank-cyd-factory.bin](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/pocket-tank-cyd-factory.bin)
   and verify it against the release's `SHA256SUMS.txt`.
2. Close any serial monitor. In a terminal in the download directory:

   ```powershell
   python -m pip install esptool==5.6.0
   python -m esptool --chip esp32 --port COM4 --baud 460800 write-flash 0x0 pocket-tank-cyd-factory.bin
   ```

   Replace COM4 with the board's actual port. **This overwrites tank saves
   and settings. Do not run it as a normal upgrade.**
3. Power on and complete first-time setup/calibration. Menu artwork is embedded;
   an SD card is optional for the first aquarium, but required for extra aquarium
   snapshots, SD backups and SD updates.

The factory binary already contains the bootloader, partition table, initial
OTA data and application at the correct offsets. It is flashed once at `0x0`;
do not flash the application-only binary there.
[INSTALL.txt](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/INSTALL.txt)
also accompanies every release. Stable releases use plain semantic versions
such as `v1.0.0`, independent of the original upstream version.

### Publish another release

Maintainers publish by pushing a tag such as `v1.0.0`. The CYD release
workflow builds with ESP-IDF v6.1, runs tool/native/emulator tests, packages
the application, factory image and SD updater, and creates a
normal GitHub release marked Latest. The version label without `v` must fit
15 characters. Stable releases are available through `/releases/latest`.
Branch pushes run checks but do not publish releases. Locally, after building:

```powershell
python ..\tools\package_cyd_release.py --build build_cyd --out ..\dist --version v1.0.0
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
For per-device calibration, open **Settings > Device > TOUCH CALIBRATION**,
or send `touch calibrate` through the director console. Hold the stylus on
each of the five crosses briefly, then lift it. The first three points fit
an affine coordinate map; the remaining two must agree within six framebuffer
pixels before the map is saved to NVS. This corrects offsets, scale, swapped
or mirrored axes, and skew without changing LCD settings. A bad fit restarts
the targets; a 90-second inactivity timeout or an orientation change cancels
without replacing the previous calibration.
If a CYD revision reports touch coordinates inverted or clipped, adjust those
CYD touch constants and rebuild.

Enlarged uses a 240x180 internal-RAM framebuffer, scaled to the 320x240 LCD.
Normal (formerly Compact) is the default for fresh installations, active-tank
resets and newly purchased aquariums. Existing saves preserve their scale.
**Settings > Display > SCALE** switches to native **320x240 Normal** mode,
with scene objects and clock graphics about 25% smaller on screen. It draws
three 320x80 horizontal tiles in the same allocation, then sends matching
rotated LCD regions. Nothing is cropped. It does not allocate a full second
framebuffer or take RAM away from weather; animation may be slower.
Scale is saved per aquarium; positions and algae are remapped on a toggle,
and a failed save restores the previous mode. Menu controls stay readable.
Menus retain their full 448x368 layout and are scaled into that framebuffer,
rather than cropped. Menu touch coordinates use the inverse scaling.
The display sends RGB565 pixels most-significant byte first. For panel color
checks, `CYD_DIAGNOSTIC_PATTERN` in [main/display_port_cyd.c](main/display_port_cyd.c)
temporarily replaces the image with red, green, blue, white, black, yellow,
cyan, and magenta ramps; leave it at `0` for normal operation.
The current panel initialization uses the ST7789-style timing/gamma sequence
selected for the connected CYD revision, through the compatible SPI panel
transport. The exact controller has not been confirmed by register reads.
The LCD SPI clock is 40 MHz, validated on the connected board with live
aquarium, clock/weather and menu rendering. Other panel revisions may need
a lower clock. Tile flushes precompute rotated row offsets instead of
repeating coordinate divisions for every pixel.
The existing DMA stripe allocation is split into alternating buffers:
preparing the next stripe overlaps the preceding transfer, and buffers
are not reused until completion. CRC32 fingerprints identify unchanged
physical LCD blocks; their transfers are skipped. Resolution/tile-layout
changes, inversion and display wake invalidate those fingerprints.
Normal CPU damage tracking unions previous/current dynamic rectangles into
32x40 blocks, merges adjacent blocks within the 80-row buffer limit, and clips
both drawing and rotated LCD transfers horizontally and vertically. Algae
changes invalidate their own cells. Background changes, covering pages and
modal overlays force complete repainting. Dense damage (over 40% of blocks)
uses large canonical tiles instead of many small rectangles, preserving
changed-block LCD reuse rather than repeatedly invalidating it. Partial rectangles
bypass/invalidate canonical LCD fingerprints so different geometries cannot
alias. The populated Ice Cavern tank can still dirty the entire screen.
CYD omits the purely cosmetic vignette, including inline edge shading and
the full-screen post-process; other targets retain it. Actual Normal samples
measured about 8.2–8.4 FPS before the idle clock appeared and 7.5–7.7 FPS with
the large clock/weather visible and caches warmed. The next Normal pass
measured 10.4-11.1 FPS with the saved Medium Flip clock/weather and
10.6-11.5 FPS with clock OFF after warm-up in the tested populated aquarium.
This is not a guaranteed minimum or a 15 FPS result.
Release 1.2.0 sampled 10.6-11.8 FPS after warm-up with the existing
one-resident-fish tank and Neon clock/weather, versus a recent 1.1 sample of
about 10.3 FPS. These are observational samples, not a controlled benchmark;
new scenery was tested separately with the native renderer and demo gallery.

Clock face and caption panels are always opaque when shown. Normal uses one
scene traversal with occlusion-aware spans/pixels and whole-object rejection,
including caption-only occlusion when the face panel is hidden. Sparse hidden
blocks are omitted from damage; dense canonical tiles still skip hidden scene
work. Backdrop cache construction deliberately disables occlusion so future
clock movement/dismissal cannot expose an incomplete cache.
Fish damage/culling uses cached projected anatomical extents, including fins,
strands and accessories. Normal halves rendered bubbles and represents each
two real shrimp with one visible shrimp (rounded up), with a tighter school;
population, card totals and progression rules are retained. Grass uses a
smaller, rigid lean at 2 Hz and invalidates damage only for changed pose,
height or placement. Seahorse secondary graphics use a 5 Hz clock while blade
attachment shares the grass shape. Enlarged animation and fish simulation remain
full-rate.

Optional work caches retain 16 quantized urchin spine poses (reset on chewing
changes), eight floor-snail shell-heave tones, and clock/weather paint runs.
The snail's live stalk/foot motion, creature palette/tint/night compositing and
clock compositing are retained. Clock runs preserve ordered RGB565 stores
and exact alpha blends, coalescing adjacent horizontal/vertical runs; 256-run
allocation pages avoid requiring a large contiguous block. Time/text/layout
changes rebuild the clock cache. Allocation/capacity failure is reported and
uses the live renderer. All work-cache memory is released before a predicted
weather deadline or foreground Wi-Fi setup, leaving network/TLS headroom.
The serial profile reports full-width-equivalent CPU rows and allocated cache bytes.
SD backup/restore JSON validation and the Bank JSON diagnostic also release
optional work caches before allocating their export/parser memory.
Seagrass clips its scanline calculations before interpolating/drawing a tile,
and fish outside a tile's silhouette bounds skip procedural artwork generation.
Native tests compare full/tiled output across every variety, growth stage and
wearable. Additional simulation tests verify Enlarged/Normal side reach, the
matching 12-pixel grass-root edge insets, bounded playful-dart speed and turn
settling under crowding. Urgent turns now settle for at least 650 ms, and
side-glass impacts retain the normal 1.3-second settle period.

`python tools/test_cyd_display.py --cc <compiler>` from the repository root
tests the actual flush loop against a simulated LCD with delayed DMA,
including buffer ownership, arbitrary damage slices and exact Enlarged/Normal
pixel mappings. Native renderer tests compare a retained display updated only
through CPU damage slices against full-frame reference images, and exercise
cached/live clocks across the layout matrix with nonuniform backgrounds.
Orientation is handled in the pixel mapping; per-frame orientation updates do
not rewrite panel scan-direction registers. This keeps orientation changes
separate from color/timing settings. Other CYD revisions may need different
initialization; correct primary-color bands alone are not sufficient to
validate midtone reproduction.
The CYD has no onboard IMU, so its orientation is fixed and Settings omits
the rotation-lock control and its touch target. Rotation fields remain in
the save format for compatibility but cannot flip the CYD display.

The shared decoration renderer reserves 35,384 bytes of byte-addressable
scratch immediately after the framebuffer, before peripheral initialization
fragments the heap. Another 49,280 bytes of integer masks and castle palette
live in 32-bit-capable internal RAM (including instruction RAM). Palette
entries and clearing operations deliberately use whole words: ESP32 instruction
RAM cannot serve byte/halfword accesses or floating-point storage. Native tests
exercise a guarded supplied allocation with both live and cached scenery.
Deferring required scratch allocation until a decoration is first drawn can
otherwise cause a saved pumpkin to crash repeatedly at boot.

CYD does not allocate the scene cache or its unused dirty/card caches.
This leaves byte-addressable RAM available for background Wi-Fi and TLS without
blanking or shrinking the aquarium. TLS uses dynamic buffers while retaining
the full 16 KiB incoming-record capacity and certificate-bundle verification.
Network diagnostics report **8-bit internal** free/largest blocks, not just
total internal heap (which also counts instruction RAM).

The director console uses UART0 through the board's USB-to-serial adapter
at 115200 baud (for example, `python ..\tools\director.py -p COM4 milestones`).

## Room light sensor, back LED, photos and daily backups

The light-dependent resistor beside the display and the rear RGB LED are
driven by `firmware/main/ambient.c`. **Settings > Display** selects
ROOM LIGHT OFF / DIM / DIM+NIGHT (backlight scaled to 35-100% of the chosen
brightness; DIM+NIGHT forces night after 30 s of sustained darkness) and BACK LED
OFF / GLOW / ALERTS (water-colored breathing glow; ALERTS adds hunger and
visitor signals). Choices persist in NVS; the learned lit-room reading is
saved at most every 30 minutes. **Settings > SD Backups** offers DAILY
automatic backups (`auto-NNNN.json`, verified, newest seven kept, retried
hourly on failure; requires a valid clock) and PHOTO TO SD
(`pocket-tank/photos/photo-NNNN.bmp`).

Serial console helpers: `ldr` (log), `ldr <mode>`, `ldr <dark> <lit>` (manual
calibration), `led <mode>`, `led <r> <g> <b> [ms]`, `photo`,
`autobackup [0|1]`, `visit [0-4]` (a visitor on the next tick),
`season <0-4, +8 lights | -1>` and `genes` (every fish's shown and hidden alleles).

## Feature limitations

- Fish goals use the rules advisor, not the upstream local LLM. This build
  has no model partition and the connected board has no PSRAM.
- Wi-Fi scanning, saved-network reconnect and certificate-verified weather
  requests have been verified on the board. Scan records and manifests
  use internal RAM. There is no default firmware update server: an unconfigured
  fork reports **NO UPDATE CHANNEL** after connecting. Manifests must identify
  `pocket-tank-cyd`; stock upstream manifests are rejected. Complete signed
  OTA installation still requires separate validation.
  See [OTA](../docs/OTA.md) before enabling a fork release channel.
- SD adds asset storage, not RAM; it does not transparently replace model PSRAM.
- Audio output, sound-bank/mixer sources, the codec driver, PMIC driver,
  battery history/logging and IMU implementation have been deleted.
  Volume, battery overlays and rotation controls are absent; USB power is assumed.

## Embedded artwork and SD storage

Insert an existing FAT-formatted card before boot. The SD slot uses its own
SPI3 bus: SCLK 18, MOSI 23, MISO 19, CS 5, at 10 MHz. The firmware never formats
the card. It mounts at `/sdcard`. Mounting is deferred until after update/provisioning mode
and boot-time network synchronization, preserving Wi-Fi calibration headroom
on the no-PSRAM board. The SD console is available in normal tank mode.

All 47 icons (RGB565 plus alpha, 120,384 bytes at this revision) are linked from
the canonical generated [icons.c](../common/icons.c) as flash-mapped constants.
This removes the previous 12 KiB SD icon cache and menu-time card reads, at
the cost of about 120 KiB app flash. Procedural fish and scenery were never
streamed from SD, so offloading icons did not save their rendering RAM.
SD remains in use for inactive aquarium snapshots, JSON backups and firmware
updates. Keep the card inserted when using multiple aquariums.

The build still generates the legacy bank for desktop tools and legacy diagnostics,
but release packages do not include it.
It is not needed for on-device artwork. To support older firmware, copy
`build_cyd\cyd_assets\icons.bin` to the card root using the
filename specified in `build_cyd\cyd_assets\icons_sd_bank.h`. Alternatively,
with the card inserted in the CYD and the serial console closed:

```powershell
python ..\tools\upload_cyd_icons.py build_cyd\cyd_assets\icons.bin --port COM4
```

The uploader requires pyserial. It checks that the bank matches the flashed
firmware, uploads to a new `.upload` file, and verifies both the transfer CRC
and a complete SD readback before renaming it. A matching installed bank is
left unchanged; existing destination files are never overwritten.
`sd status` verifies/reports the legacy bank on demand; it is not opened at boot
or kept open for rendering. `sd cancel` removes only the named
partial upload for this build if an upload was interrupted.

Without a card or a valid legacy bank, all embedded menu icons remain available.
Card removal while running is not
supported: power off before inserting or removing the card.

### Other assets

Fish and tank decorations are mostly drawn procedurally, not read from large sprite files.
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
active slot, tank name and care-upgrade flags extends the prefix to 4000.
The companion/traveler/color tail extends the prefix to 4520 bytes; the
append-only 1.2 polish tail brings the save to 5416 bytes, and the 432-byte 1.3
story tail (lineage, genes, ancestor names, visitor log, seasons) to 5848. The polish tail holds stable
fish identities/life records, a bounded recap, tank identity, visitor/backup
records and six fixed-scenery ownership bits. The 24 KiB NVS partition remains
unchanged; the 5920-byte save ceiling budgets three copies and 64 settings
entries within the 630 usable entries, retaining the dedicated GC page.
The four care flags reuse one reserved byte;
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

Open **Settings > Device** and tap the red **WIPE AQUARIUM** button. The separate
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
After purchase, tap the separate full-width **SAND: ...** or **WATER: ...** rows in **Home > Tank > Sand & Water**
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
**Home > Tank > Sand & Water** keeps each palette on its own line.
**Settings > Device** contains **TOUCH CALIBRATION**, **SD UPDATES** and the
confirmed aquarium wipe. Brightness and scale are in **Settings > Display**.
The Lights Out control remains hidden;
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

Open **Settings > SD Backups** and tap **BACKUP TO SD**. Each successful backup creates a
new file in the card's `pocket-tank` directory: `backup-0001.json`,
`backup-0002.json`, and so on. Existing backups are not overwritten.
New exports use JSON schema 14, including Lost Current progress, twelve-game records, family lineage, genes, the visitor log, persistent fish stories/buddies, recaps,
tank identity, rare sightings, fixed scenery, backup status, Enlarged/Normal scale, creature/wardrobe colors, per-fish wearables, the traveling fish, companion progress, item ownership, placement,
stacking, palette selections, fish appearance, Aquarium Journal progress
and all stored fish. Schema 1 through 13 backups
still restore, defaulting absent additions to unowned decorations and
Classic/Stripes fish (schema 1 also defaults the palettes to original).
Older backups start with an empty Journal; existing care totals and current
fish appearances qualify as you play. Open **Home > Collections > Journal** to claim
objectives, view the appearance album and select level-unlocked backdrops.
See the README's Aquarium Journal section for XP rates and unlock levels.
Pre-bank snapshots restore with an empty fish bank. Open **Home > Fish > Fish Bank**
to deposit, return or swap fish; the bank pauses care and growth and keeps
20 fish independently of the five active slots. Two fish must remain active;
a full tank can still swap with the bank. Wipe clears both the tank and bank.
**Home > Tank > Aquariums** offers up to eight independent aquariums.
Each additional tank costs 500 sand dollars from the global wallet and begins
with two fry. Switching is free and requires SD. Inactive tanks are fully
paused in alternating CRC-checked binary snapshots in `pocket-tank/tanks`.
Only one tank is in RAM/NVS. Purchase/switch commits select the tank in
the same NVS blob as the save, so failed writes do not charge or switch.
Sand dollars and lifetime earnings are global; switching cannot restore a
stale balance. Each tank has its own Journal, decorations, fish and Fish Bank.
NAME TANK edits the active tank's persistent name (15 characters, `<` / `>` editor).
The shop has 42 stable item IDs, presented in ascending price order (ties keep
their item order); the optional nursery row stays first. The scenery/upkeep
extension uses ownership bits through bit 31 without changing the 4000-byte save prefix
or schema 5. IDs 0..31 use the full 32-bit ownership mask; IDs 32..35 use the
four care flags in the reserved aquarium-metadata byte. IDs 36..41 use the
six-bit scenery mask in the append-only polish tail. Ownership queries
handle all three stores; there are no shifts past bit 31. Do not downgrade after
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
currently this CYD profile (Enlarged 240x180 or Normal 320x240), not automatic cross-board migration.

This is the same durable state as a normal tank save, not a frame-exact
memory dump: fish positions and transient animations are rebuilt at boot.
Restore resumes the snapshot without simulating the time spent in the
archive. Wi-Fi credentials, device brightness, and touch calibration are
not exported or overwritten. Routine saves still use NVS and do not require
SD. Backups contain no embedded icons or firmware.

Fish-care shop upgrades: Calm Current (160) gives 25% faster resting/sleep
energy recovery; Cozy Shelter (180) gives 25% faster base stress decay awake
and asleep; Play Kit (200) gives 25% faster exploring curiosity gains;
Training Ring (240) adds 25% of the base calm-hold trust rate, for 225% total
with a hitched seahorse. Food rewards and negative drives are unchanged.
They are permanent per-tank purchases, clear on active-tank wipe, and persist
in NVS, SD tank snapshots and portable JSON. Older backups default to none.
The four upgrades preserve the 4000-byte binary prefix; JSON has optional
`tank.stat_upgrades` (integer 0..15), binary saves use a reserved metadata byte.
Schemas 6 through 8 also include the companion extension; schema 7 appends a reserved traveler.
Console purchase keys: `current`, `shelter`, `play`, `training`.

### Mini-games and stronger Journal backdrops

**Home > Games** offers Bubble Pop, Pearl Match, Fish Slalom, Treasure Cast,
Feed Frenzy and Reef Puzzle. The games menu, rounds and results pause aquarium
simulation and care progression. Slalom and feeding games reuse active fish
portraits without changing fish state. Gameplay uses a separate random stream.

Completed rounds offer at most 10 sand dollars and 3 Journal XP per dollar.
CLAIM persists coins, XP and challenge-mode records atomically, with rollback
and retry on failure.
These XP contribute to existing Keeper levels and theme unlocks, not care
objective counters. Leaving/replaying an unclaimed result requires two taps.
Game sessions and unclaimed rewards do not survive reboot. Claimed personal
bests, medals and permanent challenges are saved in the companion extension.
Relaxed mode removes Bubble/Frenzy timers (FINISH ends the round), halves
Slalom/Cast speed, and pays the same capped rewards without records/medals.
Manual sleep follows the selected care mode's offline-care rules.

Kelp Forest, Coral Reef and Deep Glow retain their existing IDs and unlock
levels but now have much stronger water tints and scenery. Original is unchanged.
The Journal now has eight backdrops across two pages. Added IDs 4..7 are
Sunken Ruins (level 5), Sunset Lagoon (8), Ice Cavern (12) and Moonlit Reef (15).
Ice Cavern's hanging icicles start at the top framebuffer edge without a water gap.
Names and unlock levels share one table across rendering, selection, save
validation and JSON restore. The existing `journal.theme` byte in the 4000-byte
prefix is unchanged; schemas 5 and 6 accept `journal_theme` 0..7. Older firmware does
not recognize the new IDs; do not downgrade with one selected.

Fish appearances also use Keeper progression: Classic at level 1; Guppy 2,
Tetra 3, Clownfish 4, Angel 6, Betta 8, Puffer 10, Shark 12, Stingray 14 and
Jellyfish 15. Customization and shop previews/purchases use the same unlock
predicate. Existing active fish, banked fish and Journal-seen varieties stay
available below their required level, without changing fish or granting XP.
Journal-seen bits preserve acquired appearances after customization or sale.
Levels/unlocks are tank-local; new tanks still begin with Classic fry.
Patterns/colors and inherited breeding appearances are unchanged.
See the [game rules and reward table](../README.md#mini-games).
The serial director's `games` and `games tap <framebuffer x> <y>` commands
open the same page and use the same hit targets without staging a demo tank.

### Optional tank clock and on-screen Wi-Fi setup

Open **Settings > CLOCK/WEATHER** to cycle **OFF / MINIMAL / DIGITAL / ANALOG / NEON /
FLIP / BINARY / ORBIT**.
This is an idle display mode rather than a permanent corner widget. The
default layout is **large and centered**; choose large/medium/small and
center/top-left/top-right/bottom-left/bottom-right placement. Digital uses
large seven-segment numerals with separate beveled bars, visible gaps and
dim unlit segments; Analog has 60 ticks, numbered hours and three
hands; Neon adds cyan/pink accents. Flip uses shaded split-flap tiles, Binary
uses decimal-digit bit columns (8/4/2/1 from top to bottom, grouped H/M/S),
and Orbit uses concentric hour/minute/second progress rings. The hour ring
completes every 12 hours, the minute ring every hour and the second ring every
minute. Themes show the local date and seconds by default.
Open **STYLE** for separate **PRIMARY** and **SECONDARY** colors, each offering
theme defaults or **white / amber / mint / cyan / rose / lavender / red**.
Primary controls numerals, the analog hour hand and the Binary/Orbit hour
indicators; secondary controls captions/date/borders, the minute/second
indicators and analog second hand. Theme defaults retain the original
multi-color accents. The Orbit caption sits at least 10 native pixels below
the circle at every size.
There are also independent **SECONDS**, **PANEL + BORDER** and
**DATE** visibility. Hiding seconds also hides the analog second hand, Binary
seconds columns and Orbit seconds ring. Hiding the panel removes both its
background and border, not Flip's individual numeral tiles. The 12/24-hour
indicator remains, and an unsynchronized clock always shows **SYNC NEEDED**
even when the date is hidden. **SHOW CLOCK** previews directly from STYLE.
Choose 12/24-hour time. **ACTIVATE** cycles 1 minute (default), 15 seconds,
30 seconds, 2/5/10 minutes, then **MANUAL ONLY**. **OFF** disables both automatic
and manual activation; **SHOW CLOCK** opens an enabled theme immediately.
The first touch is consumed until release, returning to normal play without
feeding, selecting, wiping or dragging. Menus suspend/reset the clock idle timer.
Clock mode hides normal tool chips/cards/toasts and keeps the display readable;
fish continue simulating and swimming behind the opaque panel; Normal omits
the hidden scene drawing.
The UTC adjustment buttons start at **-1H / +1H**, from UTC-12 to UTC+14.
Tap **STEP** to cycle **1H / 30M / 15M**; the smaller steps support fractional
zones such as Nepal (UTC+5:45). STEP only controls editing granularity and
does not change the time or write a save. Set the UTC offset to your region's
**standard-time** offset, then toggle **DST: OFF / DST: +1 HOUR** for daylight
saving without changing that base offset. Both the time and calendar use the
extra hour. DST defaults to OFF and is manual, not an automatic regional
schedule. Regions with a different seasonal shift can use the UTC adjustment
instead. UTC is the default base offset.
Options save immediately for the active tank, roll back on save failure,
follow SD tank switches and portable JSON restores, and clear on active-tank
wipe. Old saves/backups default to OFF, large/centered and a 1-minute delay.
Five reserved metadata bytes hold theme, format/style flags, signed UTC quarter-hours,
size/position and packed delay/secondary color, keeping the original binary
prefix at 4000 bytes (the companion/traveler/color prefix is 4520 bytes; the polish and story tails make the full save 5848 bytes).
Schema 5 adds optional `tank.clock_theme` (0..7),
`tank.clock_flags` (0..255: bit 0 is 12-hour format, bit 1 is manual DST +1 hour,
bits 2/3/4 hide seconds/panel/date; bits 5..7 select the primary color, 0 = theme colors),
`tank.clock_utc_quarters` (-48..56),
`tank.clock_layout` (size in bits 0..1, position in bits 2..4; size 3 is invalid)
`tank.clock_delay` (0..6, in the ACTIVATE order above), and
`tank.clock_secondary` (0..7, same color order as PRIMARY). Missing secondary
color defaults to theme accents. Binary saves pack secondary color into the
high three bits of the delay byte, leaving its low five bits for the delay.
Changing either setting preserves the other. Both colors save per tank,
follow SD switches and JSON restores, roll back on write failure and reset
on active-tank wipe. Do not downgrade to older firmware with a non-default
secondary color; older builds do not understand the packed delay.

### Cozy companions and story adventures

Tap a fish, then its card's **MENU**, to open **Home**. Keeper also has a
**HOME** shortcut. Home groups **Fish** (Say Hello, Bank, wardrobe, names/looks,
life, family/genes), **Tank** (shop/decor, aquariums, palettes, creature colors,
identity, tank journal), **Adventures**,
**Collections** (Journal, discoveries, display rewards, visitor log), **Games**, and
**Settings**. Back returns to the category actually used to enter a page.
Settings groups Display, Clock/Weather, Wi-Fi/Sync, SD Backups, Aquarium Care, and Device.
New tanks start in **COZY** care: hunger and exhaustion stay comfortable,
without inventing player meals, trust, or care achievements. Existing binary
saves and schema 1–5 JSON backups remain **CLASSIC**, with captions and trophy
decorations OFF. **Settings > Aquarium Care** holds Comfort Assist (the saved
Cozy/Classic option) and auto feeding.
**Settings > Clock/Weather > Effects** holds cosmetic **Sparkles** (formerly
Calm/Lively) and **Event Captions**. Sparkles are eight bright drifting stars
beside the clock, not a fish-care setting.
**Collections > Display Rewards > TROPHIES** shows/hides earned sand ornaments.
All can be changed without resetting a tank; new tanks start without sparkles.

**Home > Adventures > The Lost Current** is an optional six-chapter, 18-encounter
story RPG. It saves three shared adventure skills, chapter resolutions, and
family-linked contributors in a 24-byte append-only tail (5912-byte total save).
It adds no aquarium AI or timed background activity and does not transfer or
change real fish. Memories and an optional trophy map are its rewards; it
does not issue coins or care upgrades. BACK leaves safely; each choice saves
atomically and save failures leave the story unchanged.
The serial `rpg` command reports campaign progress without advancing it.

Say hello to the selected fish to grow its permanent bond (once per 30
seconds of powered-on tank time). At bonds 10/25/50, invite it to dart,
visit bubbles or follow a friend. Bonds, hello counts and learned tricks
never decay and follow the actual fish through bank deposits, withdrawals
and swaps, even when names are duplicated. Selling a fish clears only its
own relationship; a failed bank/sale save rolls back the transaction.
Say Hello shows each fish's favorite (e.g. `LIKES: TANKMATES (NATURE)`), derived
from its saved starting personality and roster preset, or `(SEEN)` once that
fish's matching milestone was actually observed. Clock messages show adventures,
ready postcards, visitors and care needs, not routine exploration or favorites.
Weather and event text wrap inside the caption panel without ellipses.

The scrapbook automatically records observed behavior, greetings, tricks,
growth and story postcards. Complete Nature, Friendship and Story collections
to **CLAIM** their cosmetic frames once in Display Rewards. **FRAME** selects an
unlocked menu border; **ORIGINAL** means the unadorned border.
**TROPHIES: SHOW/HIDE** shows or hides earned shells on the sand.
Gold mini-game challenges also add permanent cosmetic markers.

Choose Lantern Lagoon (2 hours), Pearl Library (6 hours) or Starlight Cove
(12 hours), select a fish and an **ARCH** or **BUBBLES** postcard route.
Routes change the postcard scenery, not the reward, duration or risk.
**SEND FISH > CONFIRM SEND** moves the actual fish out of the live tank into
a saved 96-byte fish record plus its bond. Its care and aging pause.
The return slot is reserved across fry purchases, births and bank withdrawals;
the fish cannot be sold or banked while away. At least one fish stays home.
Its automatic return saves atomically before making it visible; failed writes
retain the safely stored fish for retry. Rewards wait until claimed separately.

Adventure timers use real elapsed time when the clock is valid (including
power-off/deep sleep), falling back to powered-on time when it is unknown.
The hello cooldown still uses only powered-on time. Open menus never record
activity discoveries or change care. There is no injury, failure, expiry or deadline.
Progress saves with the tank. A completed postcard permanently records its
route and shell/bookmark/star-stone find; **CLAIM POSTCARD** awards 10/20/30
coins and 30/60/90 Journal XP once, with save-failure rollback.

Save schema 6 added a validated top-level `companion` object. Schema 7 extends
it from 392 to 512 bytes with the traveler, bond, away flag and last valid
adventure clock. Binary data remains at offset 4000; every earlier field and
fish-bank record is unchanged. Schema-6 saves keep their existing short
portrait stories and never remove a fish retroactively.
Older checksummed SD aquarium snapshots are zero-extended after validation,
and retain generation ordering when rewritten. Backups still import schemas
1–9. Schema 8 added a `portrait_wearable` field and companion version 3 validates
equipped IDs, ownership masks and reserved bytes. The relationship record's
three reserved bytes now hold equipped ID, six-bit ownership and packed color choices;
one portrait byte preserves the departure accessory. Records remain 12 bytes
and wardrobe-only saves were 4512 bytes. Schema 9 / companion version 4 adds
`portrait_colors` and four packed `creature_colors` entries. Each color byte
stores primary in bits 0–3 and detail in bits 4–7 (0 = Natural, 1–15 = palette).
Per-fish color pairs follow the relationship record; the second portrait byte
preserves departure colors. Four creature bytes plus a display-scale byte and three reserved zero bytes
extend the companion to 520 bytes and the full save to 4520 bytes, retaining
the earlier prefix. Schema 10 / companion version 5 uses `display_compact`
(0 = Enlarged, 1 = Normal); older companions default to Enlarged. Reserved bytes
must remain zero. Older companions default to Natural with no accessories.
Avoid downgrading after using companion features: old firmware cannot
retain a tail it does not understand.
Snapshot capture fills caller-provided storage instead of returning the whole
save by value. This avoids a second large temporary on the 12 KiB tank-task
stack; SD backup readback and safety-backup validation were verified
on the board without increasing its permanently reserved stack.

### Optional desk weather

Open **Settings > CLOCK/WEATHER > WEATHER**. Weather starts OFF, in Fahrenheit, with
no city. Search using the on-device QWERTY keyboard, select the correct
city/region/country result, choose F/C and enable it. **FORGET** removes the
city. No location is inferred: only the entered city search or selected
coordinates are sent to Open-Meteo over certificate-verified HTTPS. These
device-wide NVS settings are separate from aquarium saves and backups.

Weather joins the saved Wi-Fi network in the background, retries disconnected
links with backoff, and yields to foreground Wi-Fi setup/update work. Its radio
lease is held only while connecting/requesting/polling a refresh or city search;
it is released immediately after completion and during retry backoff.
Cached readings remain visible during deliberate sleep without an offline
label; actual failed refreshes still report stale/offline status.
Disabling weather or forgetting the city also releases its ownership. City searches
temporarily acquire the same connection even while the clock weather is OFF.
The existing network worker performs HTTPS; no permanent weather task or
network/file I/O runs on the render loop. Each request uses a bounded 4 KiB
response buffer and reports connection, HTTP and validation failures explicitly.
Configuration saves are asynchronous and do not acquire the network.
`weather_network_needed_after(dt)` predicts a deadline crossing before the tick
so the renderer frees optional work caches before the worker can start Wi-Fi.

Current temperature and conditions refresh about every 15 minutes. Readings
30 minutes old are marked stale; offline/unavailable states remain visible
instead of inventing data. Weather uses a bordered, larger-text area attached
to the clock, with its own city line. Weather and optional fish-activity text
have reserved space separate from the clock face. Open-Meteo
provides conditions and GeoNames city results. On-device London search,
current conditions, F/C conversion, clock captions and disable/reconnect
were verified with the aquarium still running; the test city was then forgotten.

### Cycling selected fish

With more than one fish in the active tank, selecting a fish shows a **>**
button just outside the card's top-right edge. Tap it to select the next fish,
wrapping from the last back to the first. The arrow does not feed, open Keeper,
or trigger hold/drag actions; dragging off the button cancels the tap.
The selected card and selection ring update together, and the normal
10-second card dismissal timer restarts after a successful cycle.
Tapping the card itself still opens Keeper. Single-fish tanks and special
snail/shrimp/urchin/seahorse cards do not show this arrow.

Tap **WI-FI / SYNC** on that page (or enter `wifi setup` on the console). The tank first saves to NVS,
then restarts into a memory-isolated setup so the Wi-Fi driver does not
compete with aquarium rendering for SRAM. Choose a visible **2.4 GHz** network,
enter its password using the case-sensitive keyboard, and join. All 26 letters
are on one staggered **QWERTY** page, with large, high-contrast uppercase
keycaps. Input starts lowercase; **CAPS** toggles uppercase input, and the
footer shows the current case. The password field always shows the actual
characters entered. **123** opens numbers and symbols; **#+=** switches to
the remaining symbols, and **ABC** returns to letters without losing the
selected case. **DEL**, **SPACE**, **BACK** and **JOIN** have dedicated keys.
Backing out to the network list and selecting the same network keeps the
password; joining a secured network requires at least eight characters.
Open networks
skip the keyboard. Errors offer retry, retype or another network. An existing
saved network offers **CONNECT / SYNC**, **CHANGE NETWORK** and a confirmed
**FORGET NETWORK**. Failed new connections do not replace the saved credentials;
credential-write failures are shown explicitly. Hidden SSIDs and enterprise
authentication are not supported by this picker.

After **WI-FI CONNECTED**, tap **BACK**. At boot/return the existing NTP
client tries `pool.ntp.org`, `time.google.com`, then `time.cloudflare.com`;
it switches the radio off afterward. This setup never checks or installs
firmware. Network credentials are global device settings, separate from
tank JSON and SD snapshots. No password is printed in the console.

The CYD has no battery-backed RTC. A successful NTP sync keeps ticking
offline while powered, including deep sleep, but a power loss requires a
new sync. Before a reliable sync the overlay shows `--:-- / SYNC NEEDED`.
The approximate build/save-time seed used for gameplay is never presented
as synchronized wall time. The Clock page reports whether NTP time is
available. Synchronization is boot-time/on-demand, not a continuous NTP service.

### Focused tests

With an existing host C compiler and firmware dependencies configured:

```powershell
python ..\tools\test_tank_backup.py --cc clang
python ..\tools\test_cyd_settings.py --cc clang
python ..\tools\test_ambient.py --cc clang
python -m unittest discover -s ..\tools -p "test_*.py"
python ..\sim\run.py --cc clang --smoke
```

`--cc` can also name a GCC-compatible compiler or the full path to `zig`
(the runner adds `cc` for Zig). The native suite covers purchase/sale,
placement and persistence, JSON round-trip, invalid inputs without writes,
failed NVS writes, frozen legacy save fixtures, cached/uncached rendering,
and Settings restore confirmation/timeout/fixed orientation.
The CYD settings test runs the hardware touch handler's actual action handoff,
including room light, back LED, daily backups and photos.
The desktop preview
uses the actual Enlarged/Normal renderer and rules advisor via a temporary native library,
displayed in Python's Tk window. It requires no SDL or LVGL and keeps a separate
local save. The headless smoke check renders distinct Tank/Keeper/Settings/Shop
pages and validates JSON restore and saving.