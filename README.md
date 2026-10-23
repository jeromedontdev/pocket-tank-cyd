# Pocket Tank CYD 1.0

A CYD-only fork of [mediacutlet/pocket-tank](https://github.com/mediacutlet/pocket-tank),
now versioned independently as **1.0.0**. Originally based on upstream v0.3.8.
Targets the **ESP32-2432S028 Cheap Yellow Display**:
4 MB flash, no PSRAM, 320x240 SPI LCD and XPT2046 resistive touch.
Other boards, model inference/training, audio, battery/PMIC and IMU drivers
are not included. See [LICENSE](LICENSE) for the original project license.

**Take the illustrated tour:** [CYD features and screenshots](docs/CYD-FEATURES.md)
covers named tanks, the bank, creature customization, Journal, mini-games,
decorations, fish-care upgrades, desk-companion progression, optional weather,
themed clocks, Wi-Fi setup, releases and SD workflows with captures from the
actual desktop renderer.

## What remains

- Personality-aware **rules-based** fish goals, reflexes, feeding, growth,
  trust, milestones, breeding, tank maintenance and the shop.
- Touch-only navigation, persistent five-point touch calibration, fixed
  display orientation, brightness and gameplay settings.
- Flash-resident icon art and versioned SD JSON tank backups with safety snapshots
  before restores.
- Offline SD firmware packages through **Settings > Device > SD Updates**, with
  checksum/board validation, explicit confirmation and a pre-update tank backup.
- Treasure chest: 120 sand dollars, placement/layering, move/sell and persistence.
- Scallop shell (60), starfish ornament (60), sunken anchor (80), and a
  bobbing, bottom-tethered buoy (80). All support placement, depth and selling.
- One-time sand palette (300) and mood lights (300) upgrades. Tap **SAND** or
  **WATER** in **Home > Tank > Sand & Water** to cycle colors for free, including the original look.
  Mood Lights includes Rainbow, Hot Pink and Neon Lime alongside the classic
  blue, teal, violet and amber gradients; no flashing or strobing.
  Sand Palette also includes Rainbow, Hot Pink, Electric Cyan and Neon Lime.
- Shipwreck (200), treasure urn (60), crystals (100), sea fan (80),
  sunken ruins (160), and a bobbing tethered jelly lantern (120).
- Ten creature looks: Classic, Angel, Guppy, Tetra, Puffer, Shark, Jellyfish,
  Stingray, Clownfish and Betta; four marking
  patterns: Stripes, Spots, Band and Plain. Open **Keeper > fish portrait >
  CUSTOMIZE** to change an existing fish's unlocked variety, pattern, body and marking
  colors for free. Varieties unlock through [Keeper levels](#fish-variety-unlocks);
  existing owned/seen looks stay available. DONE saves; CANCEL discards the preview. Fry offers show
  their variety, and bred fry inherit their parents' appearance traits.
  Sharks have a pointed snout, dorsal fin and gills; jellyfish pulse and wave
  their tentacles; rays flap their wings; bettas have flowing fins. These are
  peaceful visual varieties using the existing fish care system, not predators
  or separate species simulations. Jellyfish appearances occupy a normal fish
  slot and are separate from the decorative Jelly Lantern shop item.
- Fish customization offers 24 body colors and 24 marking colors, including
  hot pink, vivid red/orange/yellow, neon lime/green, cyan, electric blue,
  purple, magenta, pastels, white and dark slate.
- **Settings > Device > WIPE AQUARIUM** opens a separate **WIPE TANK?** confirmation.
  Confirming starts over with two fry and first-time setup; CANCEL or waiting
  20 seconds keeps the tank. SD backups and touch calibration are preserved.
- Optional **Minimal, Digital, Analog, Neon, Flip, Binary and Orbit idle clocks**
  over the aquarium.
  Open **Settings > CLOCK/WEATHER** for large/medium/small sizes, center or four-corner
  placement, 12/24-hour time, a DST toggle and UTC offsets in selectable
  1-hour, 30-minute or 15-minute steps. The default
  layout is large and centered. Clocks are **OFF by default** and saved per tank.
  Choose **15 sec, 30 sec, 1, 2, 5 or 10 min** of inactivity, or **Manual Only**;
  **SHOW CLOCK** opens it immediately. The first touch only dismisses the clock.
  **STYLE** offers separate primary and secondary colors: theme defaults or
  white, amber, mint, cyan, rose, lavender and red, plus independent seconds,
  panel/border and date toggles.
  Visible panels are opaque. Normal skips hidden aquarium drawing and reduces
  ambient bubbles, grass sway updates and visible shrimp representatives
  (10 real shrimp display as 5); population and care/progression rules stay intact.
- A selected fish's card has a **>** button just outside its top-right edge
  to cycle through fish, wrapping to the first. Tapping the card still opens Keeper.
- **Settings > WI-FI / SYNC** scans 2.4 GHz networks and
  provides a full single-page QWERTY password keyboard with separate
  numbers/symbols, connection errors, change-network and confirmed
  forget options. The tank is saved before a brief restart into setup; Wi-Fi
  credentials are device-wide and never included in aquarium backups.
  NTP sets the time when returning to the tank and on subsequent boots.
  After a failed sync, the overlay says **SYNC NEEDED**, not an invented time.
  No battery-backed RTC is fitted: Wi-Fi is needed again after power loss.
  A synchronized clock keeps running offline while powered or in deep sleep.
  Set your standard-time UTC offset and enable **DST: +1 HOUR** during daylight
  saving. DST is manual, not an automatic regional schedule.
- Legacy Wi-Fi firmware downloads are
  **disabled by default** until a CYD fork update channel is configured.
- A desktop preview of the actual CYD gameplay/renderer and native regressions.

The original learned advisor selected goals and urgency from fish state;
this fork instead uses explicit personality-aware rules. There is no LLM,
generated conversation or learned-confidence hesitation. This is the same
advisor the CYD port already used before the cleanup. SD storage does not
replace the working RAM needed by the original model.

## Build and upgrade

### Install from your browser

**[Open the USB web installer](https://jeromedontdev.github.io/pocket-tank-cyd/)**
in desktop Chrome or Edge. Connect an ESP32-2432S028 CYD with a USB data cable;
no ESP-IDF installation is needed.

**Factory installation only: this overwrites saved tanks, Wi-Fi, calibration
and collection identity.** Back up first. Existing users should use the SD
update instead to preserve their aquarium. The page requires an explicit
acknowledgment before connecting. All artwork is embedded; no SD icons are needed.

The site uses ESP Web Tools and the latest stable release's factory image,
verified against its published SHA-256 checksums before deployment. The chip
family is checked, but users must confirm the correct CYD board model.

Prebuilt CYD firmware, SD update packages, matching artwork and installation
instructions are available under
[Latest built release](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest).

| Download | Use |
|---|---|
| [SD update: update.ptu](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/update.ptu) | Existing CYD installations; place at `pocket-tank/update.ptu` on SD |
| [Initial-flash binary](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/pocket-tank-cyd-factory.bin) | New boards only; flash at `0x0` |
| [Install instructions](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/INSTALL.txt) | SD placement, USB flashing and warnings |
| [Checksums](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/SHA256SUMS.txt) | Verify downloaded files |

**All artwork is embedded in the firmware. No icons need to be copied to SD.**
No source build is needed for these downloads. SD is used for extra aquariums,
backups and offline updates, not artwork.
Use the SD update for an existing installation. **Factory images overwrite
saved tanks and settings** and are only for new boards or deliberate resets.
See [release instructions](firmware/CYD.md#downloadable-releases).

Install ESP-IDF (verified with v6.1), then open its PowerShell terminal:

```powershell
.\tools\build_cyd.ps1
.\tools\build_cyd.ps1 -Flash -Port COM4
```

The helper builds in `firmware\build_cyd` and upgrades **only the active app**,
detected through the USB console (`ota_0` at `0x20000`, `ota_1` at `0x200000`).
For deliberate recovery without a working console, specify `-AppSlot ota_0`
or `-AppSlot ota_1`; this does not change the boot slot.
It assumes this fork's CYD partition table is already installed;
it does not erase NVS, flash a partition table or format SD.
For an unflashed board, from `firmware`:

```powershell
idf.py -B build_cyd -D SDKCONFIG=build_cyd\sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build_cyd -p COM4 flash
```

The only supported target is ESP32/CYD. `sdkconfig.defaults` is the canonical
configuration; `sdkconfig.cyd` is retained as an identical compatibility alias
for existing build directories. The app partition layout and NVS save offsets
are unchanged. Never erase NVS to perform a normal upgrade.

See [firmware/CYD.md](firmware/CYD.md) for SD setup, calibration, console
commands, JSON backup/restore and hardware limitations.

## Desktop preview and tests

Configure/build the firmware first to provide its managed cJSON dependency.
With Python 3.9+ and a GCC/Clang-compatible C compiler:

```powershell
python sim\run.py --cc clang
python sim\run.py --cc clang --smoke
python tools\test_tank_backup.py --cc clang
python -m unittest discover -s tools -p "test_*.py"
```

`--cc` also accepts the path to Zig (the runner adds `cc`). The preview uses
Python's Tk window, so SDL/LVGL are no longer required. Mouse gestures act as
touches; toolbar buttons open Keeper, Settings and Shop. It has its own
`sim\tank.sav`; it never touches the physical device. Wi-Fi is simulated,
not hardware-emulated. Export/restore buttons use the same CYD JSON schema.

The native suite checks all shop items, fish styles/patterns, customization
save/cancel/failure and breeding, frozen legacy saves, JSON
round-trip/rejection/write failures, decoration rendering with scratch guards,
Settings confirmation/timeout/fixed orientation, Wi-Fi flow, the unconfigured
update-channel message and rejection of upstream update manifests.

## Shop expansion

**Keeper > UPGRADES** lists unlocks from cheapest to most expensive, with
stable ordering for equal prices. The nursery's FRY row, when available, stays
separate at the top. Existing item IDs, prices, placements and ownership are
unchanged.

Ten more tank-local unlocks fit the existing 4000-byte save prefix:

| Unlock | Sand dollars | Effect |
|---|---:|---|
| Zen Pebbles | 40 | Fixed stone stack on the sand |
| Duck Float | 60 | A yellow duck bobbing at the surface |
| Pearl Clam | 70 | Fixed open clam and pearl |
| Lily Pads | 80 | Floating pads with a pink water lily |
| Floating Log | 90 | A gently bobbing surface log |
| Tube Sponges | 100 | Fixed gold sponge garden |
| Lost Barrel | 110 | Fixed oak barrel |
| Dive Helmet | 140 | Fixed brass diving helmet |
| Slow Garden | 160 | Grass grows at half speed, awake and asleep |
| Bio Filter | 180 | New algae grows at half speed, awake and asleep |

The three surface ornaments can be displayed together. These eight new scenic
unlocks have fixed positions; unlike movable decorations, they do not offer
MOVE. They can be sold back for the usual 20 percent (tap SELL twice).
The shop descriptions identify their positions.
The upkeep upgrades are permanent, affect only their own aquarium, do not
erase existing algae or grass, and never award chore credit. Other care,
fish aging and coral growth remain unchanged. Sleeping SD tanks still pause.

![Surface decorations](docs/media/cyd-features/tank-surface-floats.png)

## Fish-care upgrades

Every shop item uses a muted gray icon until purchased and its full-color icon
afterward, in both the shop list and item details. This indicates ownership,
not affordability.

Four permanent, per-aquarium purchases join the price-sorted shop:

| Upgrade | Sand dollars | Effect |
|---|---:|---|
| Calm Current | 160 | 25% faster energy recovery while resting or asleep |
| Cozy Shelter | 180 | 25% faster natural stress decay, including sleep |
| Play Kit | 200 | 25% faster curiosity gain from exploring |
| Training Ring | 240 | Adds 25% of the normal hold-to-trust rate |

Bonuses do not stack with duplicate purchases. Training Ring adds to, rather
than multiplies, the seahorse's bonus (225% total while hitched). These do not
change food rewards, energy drain, curiosity spending, maximum stats, size,
aging, hunger or breeding requirements. Sleeping tanks in the SD collection
stay paused. Purchases travel with their own tank, survive reboot and JSON
backups, and clear when that tank is wiped. Older backups start without them.
The binary save's 4000-byte prefix is unchanged; a formerly reserved byte holds the flags.

The fork also includes upstream's daytime breather fix: exhausted fish duck
into nearby seagrass (or their resting corner) to recover without changing
their advisor goal. Calm Current also boosts this resting recovery. The CYD
fork retains its separate update channel and existing release-number format.

## Display readability

CYD menu text is pixel-aligned in both Enlarged and Normal modes, with a minimum
5x7 glyph (including subtext), brighter secondary labels, and wrapped status
messages. Journal cards, the collection album, fry offers, Settings and SD
Updates use full-size captions rather than downscaled microtext. Detailed
build information is on Updates; Settings > Device keeps a readable version footer.

## Aquarium Journal

Open **Keeper > JOURNAL** for Goals, Album and Themes. Earn permanent XP
from eaten meals (8), cleaned algae colonies (2), each 10 cm trimmed (2),
and successful calm-hold approaches (10). Mini-game claims also grant three XP
per sand dollar earned. Spending sand dollars never lowers your
Keeper level. There are 15 levels (level 15 starts at 4200 XP), with no daily
streaks or absence penalties.

Three of the 15 objectives are active at once. Tap a ready objective to
claim its one-time XP and sand-dollar reward; the next objective replaces it.
Existing care totals qualify, but old care does not grant bulk retroactive
XP. The album permanently records seen and adult appearances, including
customized looks, even after selling a fish; it is not an acquisition-only
species checklist. Backdrops unlock at levels 3 (Kelp Forest), 6 (Coral Reef)
and 10 (Deep Glow). Original remains available, and themes do not replace
your purchased water/sand palettes or lock existing customization.
Kelp Forest now has a rich green water tint, leafy stalks and light shafts;
Coral Reef adds blue water and large pink/peach branching coral; Deep Glow
has midnight-purple water, luminous jellyfish and bioluminescent specks.
Original is unchanged.
Four more backdrops are available on the Themes tab's second page:

| Backdrop | Keeper level | Look |
|---|---|---|
| Sunken Ruins | 5 | Weathered stone columns and broken arches in teal water |
| Sunset Lagoon | 8 | Peach/purple water, a golden sun and reflections |
| Ice Cavern | 12 | Blue water, hanging ice and faceted crystals |
| Moonlit Reef | 15 | Midnight-blue water, crescent moon and silver reflections |

Use the `<` / `>` buttons above the theme rows to browse both pages.
The existing theme IDs and unlock levels are unchanged. New IDs 4..7 fit the
existing save byte; there is no save-size increase. Older firmware will not
recognize the new backdrops, so avoid downgrading.

### Fish-variety unlocks

New aquariums start with **Classic** fish only. Keeper XP from care, objectives
and mini-games gradually unlocks more appearances for **CUSTOMIZE** and
future shop fry offers:

| Keeper level | Variety |
|---|---|
| 1 | Classic |
| 2 | Guppy |
| 3 | Tetra |
| 4 | Clownfish |
| 6 | Angel |
| 8 | Betta |
| 10 | Puffer |
| 12 | Shark |
| 14 | Stingray |
| 15 | Jellyfish |

The Journal's Album shows required levels for locked varieties. Customization
cycles only available varieties; all patterns and colors remain available.
Unlocks are **per aquarium**, like its Journal XP. Existing active/banked fish
and appearances already recorded in the Album are grandfathered in: they
remain usable even below their new level requirement. Seen varieties stay
available after fish are sold, stored or customized. Breeding still inherits
existing parents' looks; it cannot randomly bypass variety unlocks.
These are visual appearances, not different fish-care rules or predator behavior.

XP, claims, album records and the selected theme survive saves and SD
backup/restore. Wipe clears them along with the tank. Claims and theme
changes report save failures instead of applying unsaved rewards.

## Mini-games

Open **Keeper > GAMES**. Tap **PLAY AS** to cycle through your active fish;
Slalom uses that fish's real appearance, Feed Frenzy visits the tank's fish,
and Treasure Cast shows existing fish among the hazards. These are portraits,
not fish transfers: games cannot injure, feed, train or change the actual fish.

| Game | How to play | Sand dollars |
|---|---|---|
| Bubble Pop | Pop rising bubbles for 30 seconds; gold bubbles score 3. | Score / 3, rounded down, up to 10 |
| Pearl Match | Reveal cards and remember six pearl pairs; untimed. | 8 at 18 moves or fewer, otherwise 5 |
| Fish Slalom | Use LEFT/RIGHT to pass 12 open gates and collect their snacks. | 1 per successful gate, up to 10 |
| Treasure Cast | Time five casts over the chest, avoiding fish/debris. | 2 per accurate cast, up to 10 |
| Feed Frenzy | Choose each visiting fish's requested snack for 30 seconds. Correct +2; wrong -1, never below zero. | Score / 2, rounded down, up to 10 |
| Reef Puzzle | Rotate pipe tiles to connect the left inlet to the right outlet; untimed, always solvable. | 6 |

Each sand dollar also earns **3 permanent Journal XP**, contributing to Keeper
levels and backdrop unlocks without falsely completing care objectives.
**CLAIM** saves rewards and game records together; a failed save changes neither and
leaves the claim available to retry. A round can only be claimed once.
BACK or PLAY AGAIN requires a second tap before discarding an unclaimed reward.

Aquarium care, growth and care progression pause throughout the games menu,
round and results. Leaving resumes normal care; manually sleeping/rebooting
still uses the normal offline-care rules. Games, in-progress scores and
unclaimed rewards are temporary. Claimed challenge-mode personal bests, round
counts and bronze/silver/gold medals are permanent per aquarium.
There are no entry fees, wagers or penalties to your actual aquarium.

Use **CHALLENGE / RELAXED** on the games menu to switch modes. Relaxed Bubble
Pop and Feed Frenzy have no countdown: tap **FINISH** whenever you like.
Relaxed Slalom and Treasure Cast move at half speed; Match and Reef remain
untimed. Relaxed play still offers the usual capped coins/XP but does not
set records or medals. **RECORDS** shows personal bests and permanent gold
challenge targets: 24 Bubble points, Match in 16 moves, 24 Slalom points,
10 Cast points, 40 Frenzy points, and Reef in 18 moves. Meeting a move target
means that many moves or fewer. The suggested game advances after you claim
its challenge-mode reward; it has no calendar deadline or streak.

## Desk companion, discoveries and safe adventures

Tap a fish and its **MENU** button for **Home**: **Fish**, **Tank**,
**Adventures**, **Collections**, **Games**, and **Settings**. The existing
Keeper page also has a **HOME** shortcut. Fish holds care, Bank, wardrobe and
names/looks; Tank holds the shop, aquariums, palettes and creature colors.
Collections holds the Journal, discoveries/frames and progress. Adventures
is its own destination, not a container for unrelated features. Back returns
to the category you used. There are no daily streaks or missed-visit penalties.

**Settings** is grouped into **Display**, **Clock/Weather**, **Wi-Fi/Sync**,
**SD Backups**, **Feeding**, and **Device** (updates/calibration/wipe).
**Display > SCALE** toggles **Normal 320x240** (formerly Compact, now the default
for fresh installations, resets and new aquariums) / **Enlarged 240x180**.
Existing saves retain their selected scale. Normal renders
the full tank natively, making scene objects and clock graphics about 25%
smaller in each dimension, with no crop or inset. Page controls remain
readable. The setting and remapped positions persist per aquarium; failed
saves roll back. Three horizontal tiles reuse the existing framebuffer to
preserve weather's RAM budget. Normal can have a lower animation rate.

- **Cozy Care** keeps unattended care comfortable without pretending you fed,
  cleaned or trained your fish. Classic keeps the original care behaviour.
  New aquariums start Cozy; existing aquariums stay Classic until you change
  the option. Bonds never decay in either mode.
- **SAY HELLO** adds 5 bond, up to 100, once per 30 powered-on seconds.
  It doesn't manufacture trust, feeding counters or Journal care milestones.
  DART unlocks at bond 10, BUBBLES at 25, FOLLOW at 50. Tap an unlocked trick
  to request a playful dart, a visit to the bubble column, or following a
  tankmate; close the menu to watch. CARE's **BOND / TRICK HELP** explains
  this alongside **trust**, the separate 0–10 confidence built through food
  and calm care. Bond is permanent friendship, not another trust multiplier
  or a source of care XP. Bonds/tricks travel with banked and adventuring fish.
- **CAPTIONS** adds aquarium activity beside the idle clock. **CALM / LIVELY**
  chooses whether extra drifting clock-mode specks are shown.
- **Collections > Discoveries** automatically records bubble watching, exploration, seagrass naps,
  swimming together, playful darts, reef visits, greetings, learned tricks,
  growing up and adventure postcards. Behaviour discoveries require at least
  two seconds of the activity with the aquarium visible; you don't have to
  tap the event while it happens.
- Complete **Nature** (six activity discoveries), **Friendship** (hello and a
  trick), or **Story** (all three postcards) and claim its permanent cosmetic
  frame. Frames colour the companion page and add small clock-corner ornaments;
  **FRAME >** cycles owned frames and displays the selected name; it explicitly
  shows **LOCKED** until you complete and claim a collection. DEFAULT removes
  the ornament without changing your clock colours.
  **TROPHIES** shows/hides earned shells and gold game-challenge markers in
  the aquarium. Collection claims are one-time, not a repeatable currency source.

### Story quests

Choose a fish, destination and postcard route, then tap **SEND FISH** and
**CONFIRM SEND**. ARCH/BUBBLES choose the postcard's scenery, not different
risks, durations or rewards.

| Destination | Time away | Reward | Postcard find |
|---|---|---|---|
| Lantern Lagoon | 2 hours | 10 sand dollars + 30 Journal XP | Lantern shell |
| Pearl Library | 6 hours | 20 sand dollars + 60 Journal XP | Pearl bookmark |
| Starlight Cove | 12 hours | 30 sand dollars + 90 Journal XP | Star stone |

The selected fish **actually leaves the aquarium** and cannot be fed, sold
or banked while away. Its care, age, appearance, trust and bond are preserved
in the save. Its tank slot is reserved so it **automatically returns** when
the timer finishes, without waiting for you to claim rewards. At least one
fish stays home. One adventure runs at a time.
Each destination can be completed and claimed once; postcards and
route choices remain in the Album. There are no entry fees, failures,
deadlines or expiring rewards.

Progress is saved and resumes after restart. With a synced clock, elapsed time
includes power-off/sleep; without valid time, powered-on seconds advance it.
No time is guessed while the clock is unknown. Completed rewards wait for you.
CLAIM saves coins, XP and claim state together, with rollback/retry if saving
fails. Schema 9 includes the safely stored traveler, per-fish wardrobe/colors
and creature colors;
schema 6/7 companion saves
still import, including already-running short portrait stories. Schemas 1–5
still import with Classic settings and empty companion progress.
Do not downgrade after using the new progression.

### Optional weather with the clock

Open **Settings > CLOCK/WEATHER > WEATHER**, search for a city, select the correct
result, choose F/C and enable weather. No location is inferred automatically.
Weather is off by default and needs a saved Wi-Fi connection; city search and
selected coordinates are sent to [Open-Meteo](https://open-meteo.com/) over
HTTPS. Location and units are device-wide settings, separate from aquarium
backups.

The clock displays current temperature/conditions, or an explicit
unavailable/offline/stale status. Weather occupies a bordered, larger-text area
attached to the clock, with the city on its own bounded line. Activity captions
share the reserved clock area without covering its face. Conditions refresh
about every **15 minutes**; readings **30 minutes** old are marked stale.
Disabled weather makes no requests.
Weather data: Open-Meteo; city search: GeoNames via Open-Meteo.

## Fish bank

Open **Keeper > FISH BANK** to keep up to **20 fish** outside the five-fish
active tank limit. **In Tank > STORE** deposits a fish; **Stored > CHOOSE**
shows a banked fish's age, trust and appearance. Use **RETURN** if there is
room, or **SWAP** to choose the active fish that trades places with it.
Every move asks for confirmation and saves immediately; a failed save
leaves both fish where they were. Storage and swaps are free.

Keep at least two fish active. Swaps work even at this minimum and when the
tank or bank is full. Finish naming a new fry before moving fish. Stored
fish do not age, grow, eat, earn care rewards or require attention. Names,
looks, stage, age, traits, trust, care counts and milestone reward ledgers
travel with each fish. Positions and current swimming goals restart on
return. Slot-based parent links to a fish leaving the active tank are
cleared rather than reassigned to its replacement.

The bank is included in autosaves and SD backups. Restoring a snapshot
restores both the tank and bank; restoring an older pre-bank snapshot
starts with an empty bank. **Wipe clears the bank too**, but not SD files.

## Additional aquariums

Open **Keeper > UPGRADES > TANKS**. The original tank is free; each additional
tank costs **500 sand dollars**, paid from your shared wallet.
There are **eight tanks total**. Buying enters a new aquarium with two fry
and first-time setup; it does not copy your fish. Both purchases and free
switches require confirmation.

**Sand dollars are global:** earning or spending in any tank changes the same
balance, and switching never restores an old wallet. Each aquarium has its
**own decorations, layout, palettes, Journal,
fish and 20-slot Fish Bank**. Ownership of the purchased tank slots is shared,
but their gameplay state is independent. Finish naming a fry before leaving.
Use **NAME TANK** to name the active aquarium (up to 15 characters). Select
a character, use `<` / `>` to change it, then SAVE; BACK discards edits. Names remain
with their slots across switches, restarts, Wipe and individual JSON restores.
Inactive tanks are fully paused: no aging, hunger, growth, rewards or offline
simulation while another tank is active. The active tank retains the existing
power-off/sleep behavior.

An SD card is required to buy or switch. Sleeping aquarium snapshots are
stored under `pocket-tank/tanks`, with alternating checksummed, readback-verified
copies. The outgoing tank must save successfully before switching; the final
internal-save commit selects the new aquarium and records the purchase.
Failures do not charge you or replace the live aquarium. Keep the card with
the device: losing it loses access to inactive aquariums. The selected aquarium
continues from internal NVS without the card.

**Backup/Restore and Wipe apply to the active aquarium only**, not the whole
collection. Restoring JSON preserves your current purchased-slot identity.
Wipe clears the active aquarium and its bank/Journal but keeps purchased
slots and other aquariums; when multiple tanks are owned it also keeps the
shared wallet. Restoring JSON replaces the shared wallet with the backup's
balance. To archive tank contents, export each aquarium and also copy the
card's `pocket-tank/tanks` directory. There is no one-click whole-collection
restore: copying SD files alone cannot recreate ownership after erasing NVS.
Do not downgrade firmware
after buying aquariums: older builds do not preserve collection metadata.

## Fork maintenance

This is intentionally no longer a multi-board tree. Upstream gameplay changes
can still be ported manually, but merges and release compatibility are not
automatic. Preserve existing binary offsets and append new fields; the save
is now 4520 bytes, with the first 4512 bytes and the
original 1920-byte chest, 1952-byte palette, 2000-byte customization and
2040-byte Journal and 3968-byte Fish Bank layouts intact. The new 32-byte
aquarium metadata identifies the purchased collection, active slot, its name and
four care-upgrade flags in a formerly reserved byte. The 36-item shop uses
the original full 32-bit ownership field plus those four flags.
The appended companion section holds bonds, discoveries, quests, wearables and game records.
Wardrobe/color data uses reserved relationship/portrait bytes; four per-creature color
pairs append eight bytes (including reserved alignment bytes).
JSON exports use schema 10 for an individual aquarium, including saved Enlarged/Normal scale; schema
1 through 9 backups remain importable. Older firmware cannot restore schema 10 backups; keep a matching
pre-upgrade backup before downgrading. The historical progression documents under `docs`
describe the original game design; this README and the CYD guide are the
authoritative build/hardware instructions.

Do not use upstream installers or release binaries on this fork. See
[docs/OTA.md](docs/OTA.md) for offline SD packages, the opt-in fork channel
and signing limitations.
