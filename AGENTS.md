# Pocket Tank CYD: AI maintenance directives

This is the project entry point for coding assistants. Read it before changes,
then read the relevant implementation and linked guide. It is shared project
guidance, not a secret, a runtime configuration, or a promise that every fact
stays current. Check Git, source constants, and GitHub before claiming a release
or deployment is published. Update this file when its contracts change.

## Start or resume here

1. Inspect `git status`, recent commits, tags, and the current user request.
   Preserve unrelated work. Never assume an earlier conversation's local files,
   compiler paths, credentials, or test results are available.
2. Read [README.md](README.md), [the illustrated feature guide](docs/CYD-FEATURES.md),
   and [hardware/build notes](firmware/CYD.md) for the relevant behavior.
3. Identify the smallest complete change across simulation, renderer, input,
   persistence, hardware, tests, documentation, and web/release surfaces.
4. Validate the exact behavior and error/rollback paths. Record what actually
   passed, what was not tested, and any remaining work in the final response.

### Handoff baseline

- Source release: **2.2.3**, shading all ten fish bodies/fins and six wearables.
  `fill_fish_material` selects five pre-tinted tones during existing polygon
  scan conversion; preserve exact coverage, markings/traits, color choices,
  poses, attachment and full/tile parity. Head-on shading stays within the
  existing thickness ellipse. No new textures, buffers or save fields.
  `tools/test_page_art.py --gallery <directory>` captures actual fish/wearable
  lineups at both sizes without device staging.
  2.2.2 refined twenty tank ornaments/fixed scenery,
  jade vegetation and folded sword-leaf shading. Material art stays in the
  existing `ornament_pixel`/`scenery_pixel` paths, shared with previews and
  static caches; preserve footprints, openings, night/tile parity and all
  three detail levels. No new buffers, animation, ownership or save fields.
  2.2.1 polished fifteen shared menu/goal stamps, eighteen
  game thumbnails and medals. Stamps use static 12x12 indexed art expanded
  within 24x24 page bounds; thumbnails remain 40x32 and medals 20x24.
  Preserve those bounds, low-contrast locked states, full/tile equality and
  both sizes. No live-aquarium work, new buffers or save fields were added.
  2.2.0 added shared daily care (100 dollars/50 XP),
  five shared game-bonus dollars/day with no game XP, halved future milestone
  XP and append-only 6032-byte/schema-17 saves. Existing XP/wallet/unlocks stay.
  Clockless renewal uses 24 running care hours; valid time uses UTC days.
  Backward clocks/reboots/tank changes must not renew allowances. No streak
  penalties or missed-day debt/back-pay. 2.1.3 simplified Settings/Clock/Weather, exposing weather
  text sizing and cached-condition icons, and consistently naming this fork
  Pocket Tank CYD. 2.1.2 fixed browser fish-card lock/tool/DONE dispatch
  after 2.1.1 added browser fullscreen/food-placement/touch hints
  and release-tag-pinned Pages builds after 2.1.0's three scene details, themed Jumbo clocks,
  linked screen/LED low-light dimming and arrow-only pagination to permanent modes, eighteen games, global Bank,
  illustrated UI and focused code cleanup. This began as a hardware port but
  is now a distinct aquarium game; retain upstream simulation/license credit.
  Verify publication through Actions before claiming deployed downloads.
- Historical **1.4.0** introduced The Lost Current RPG, twelve games, consistent
  BACK navigation, reorganized care/collections, visible clock effects,
  room-light/LED controls, and a feature-focused website.
- Release 2.0 includes a visual overhaul: compact shared static story
  sprites/scenery, guide/destination portraits, illustrated keepsakes/info,
  game and hub/goal cues. This is not in the historical 1.4.0 downloads.
  Story artwork lives in `common/companion_ui.c`; hub/game art lives in
  `common/render.c`. Keep the artwork static, bounded and shared, and measure
  app size rather than adding large full-screen images or new asset buffers.
- Post-1.4 source also flattens menus: Goals/Album are separate Collections
  entries; Themes lives under Tank; Identity under Aquariums; Fish has two
  pages and independent Life/Family/Genes/Firsts/Keepsakes; Adventures is a
  Stories/RPG hub. Keep simulator and hardware Home action dispatch in sync.
- Source has eighteen games with records/medals and transactional Normal/Hard
  rewards, including Lighthouse, Shell Slide, Sonar Sweep, Tide 2048, Harbor
  Stack and Echo Reef. Preserve the original twelve records on migration.
- **Game mode is chosen on first run only**, for the whole profile: Normal,
  Creative or Hard. It is NOT switchable in Settings and NOT a special tank.
  Only a confirmed full wipe permits another choice. Existing saves migrate
  safely to Normal; JSON cannot bypass a chosen mode.
- Aquariums are decorative/resident expansions of one profile, sharing money,
  XP, objectives, discoveries, unlocks, campaign, game records and a GLOBAL
  20-slot Fish Bank. Store in one tank and withdraw/swap into another.
  Residents/layouts stay local; identities, family, genes and bonds follow fish.
  Profile reserved bit 3 (`PROFILE_SHARED_BANK`) marks migrated snapshots.
  Never import a stale migrated SD Bank or identity counter over the active
  profile. Old local Banks merge once, blocking safely if capacity is exceeded;
  all identity references are remapped together to avoid aliases. Multi-tank
  JSON restores allocate safe new identities without resetting the high-water
  counter. Keep the frozen 6008-byte/schema-16 prefix; no additional NVS/PSRAM is needed.
  Creative unlocks choices without fabricated XP/wallet, permits free fish
  additions (five including travelers) and confirmed removals (keep two).
  Hard permits online starvation deaths only: never offline or old-age deaths.
  Surface risks, death/save errors and recovery explicitly; keep death and
  recovery transactional and do not reward removals.
- A full wipe resets money, progression, records, aquarium collection identity,
  fish and mode choice; disconnected old SD snapshots must not resurrect
  wiped progress. Preserve unrelated SD files and explicit user backups.
- Director `stash`/`restore` and destructive hardware demos are disabled: their
  NVS-only backup cannot protect expanded internal saves. Use simulator staging
  and SD JSON snapshots. Never re-enable them by copying a stale NVS blob.
  The blocked demo bodies and filming-only grid/paged name keyboards have
  been removed. Keep explicit legacy command refusals; fish naming uses the
  letter wheel, while Wi-Fi has a separate QWERTY keyboard. Do not remove
  save migrations or rename the frozen save fields as part of dead-code cleanup.
- Obsolete upstream board imagery, battery/touch logs and model-era design/
  benchmark notes are removed. Current behavior lives in README, the CYD feature
  guide and firmware/CYD.md. `firmware/partitions.cyd.csv` is the single active
  partition source. Preserve `assets/icons`, `docs/media/cyd-features` and
  `sim/testdata/saves`: they are source artwork, current screenshots and
  compatibility fixtures, not disposable legacy files. Leave build/cache/
  dependency directories alone unless cleanup of those is explicitly requested.
- Prior implementation commits: `551e771` (RPG/games/UI/docs) and `44cf7f0`
  (feature home, separate flasher, GCC signedness fix). Treat these as landmarks,
  not a substitute for current HEAD. Release publication is verified in Actions.
- The Lost Current is optional: six chapters, 18 encounters, three endings.
  It must not hurt/remove real fish, alter care stats, impose deadlines or
  required purchases, or add background aquarium AI. Chapter choices save
  transactionally; safe detours still advance.
- The real device was last tested on **COM4**. Confirm current port/device
  ownership before connecting. Never manipulate a real aquarium just to stage
  screenshots or advance the user's story.
- No known unfinished feature is implied by this handoff. New requests and
  current CI/GitHub state determine the next task.
- Release 2.1 adds Display > SCENE Balanced/Simple/Enhanced, independently of scale.
  This is a device-only `tank/simple` NVS preference, not a game mode or save
  field. Normal size + Balanced detail are defaults; preserve saved choices.
  Simple hides helpers/bubbles but retains real static grass and all decorations;
  all simulation, benefits, ownership and care rules remain intact. Keep
  food/algae feedback visible and hidden helper hit targets disabled.
  Placement editors always show the complete scene. Enhanced keeps smooth
  animation; Balanced reduces secondary animation independently of size;
  simulator preview is session-only. Test both scales and transitions, and
  never infer FPS gains without actual-unit measurements.

## Product constraints

- Supported board only: **ESP32-2432S028 CYD**, ESP32, 4 MB flash, no PSRAM,
  320x240 SPI LCD, XPT2046 resistive touch. Do not restore generic-board,
  model inference/training, audio, battery/PMIC, or IMU dependencies.
- This is a quiet, local, rules-based aquarium. Wi-Fi is optional for normal
  care; time/weather may use it. Keep new games/stories optional.
- Preserve fish identities, genetics, family, Bank, aquariums, settings, and
  backups across upgrades. Never use reset/factory flash to mask a bug.
- Keep tap targets, labels, and return destinations consistent. Use BACK.
  Changes must work in both Normal and Enlarged display modes.
- Surface save/device errors explicitly and roll back failed transactions.
  Do not claim success, award twice, or silently replace invalid data.

## Architecture map

| Surface | Main files and responsibility |
| --- | --- |
| Tank simulation | `common/tank.c`, `common/tank.h`: creatures, runtime fields, initialization, tick behavior |
| Progression and saves | `common/progression.c`, `.h`: locked binary layout, JSON migrations, bank/tank switching, save policy |
| Companion systems | `common/companion.c`, `.h`: life, bonds, family, visitors, adventures, RPG and records |
| Companion UI | `common/companion_ui.c`: Home and its subpages, shared page coordinates and navigation |
| Home routing | `common/home_navigation.h`: common page initialization; platform ports retain overlay/BACK state |
| Rendering/input | `common/render.c`, `.h`: aquarium, clock, menus, hit testing, retained damage |
| Games | `common/games.c`: eighteen games, pages, help/pause/results and reward dispatch; `render_internal.h` is the narrow private page-drawing bridge |
| Settings/services | `common/setup.c`, `update.c`, `weather.c`, `notice.c`, `advisor.c` and their headers |
| Firmware loop | `firmware/main/main.c`: boot, frame lifecycle, sleep/wake, display/service coordination |
| CYD hardware | `firmware/main/board_pins.h`, `display_port_cyd.c`, `touch_port_xpt2046.c`, `ambient.c`, `brightness.c`, `memory_plan.h` |
| Platform ports | `firmware/components/tankcore/` and `firmware/main/*port*`: NVS, time, network, weather, SD |
| Device diagnostics | `firmware/main/director.c`: serial staging/debug commands, including read-only `rpg` |
| Simulator | `sim/main.c`, `sim/run.py`, `sim/*port*`: actual core/renderer, staged saves, native ABI |
| Playable web game | `web/play.html`, `play.mjs`, `play.css`, `sim/browser_port.c`, `tools/build_browser.py`, `test_browser.cjs`: real WASM game, atomic local archives, exclusive-tab locking and recovery |
| Regressions | `tools/test_*.c`, `tools/test_*.py`, `sim/test_*.py`, `sim/testdata/saves/` |
| Screenshots | `tools/capture_cyd_gallery.py` -> `docs/media/cyd-features/`: actual renderer, separate demonstration tank |
| Web | `web/index.html`: features; `flash.html`: USB flashing; `viewer.html`: local backup viewer; `usb-update.mjs`: safety-critical updater |
| Release | `common/version.h`, `tools/package_cyd_release.py`, `make_sd_update.py`, `prepare_web_installer.py`, `.github/workflows/` |

Follow existing helpers and naming. Companion page IDs have named constants
in `companion.h`; preserve their selector values. Keep page-family membership
explicit rather than relying on numeric ranges. Do not add generic frameworks
or further split modules without a concrete maintenance benefit.

## Persistence and memory: hard constraints

- Current binary save **6032 bytes**, JSON **schema 17**; imports schemas 1-16.
  The 24-byte `care_state_t` appends at offset 6008; quotas and care progress
  follow shared progression, never stale parked snapshots. Validate JSON and
  binary tails; retain zero-tail migration without changing existing XP.
  Previous tails: 5848 before game records, 5888 before RPG, 5912 before Creative.
  Creative is an 8-byte append-only marker/version/enabled/reserved tail; every
  frozen prior save offset is unchanged. Append 40 bytes `games_extra` plus
  48 bytes `profile` (fixed mode, starvation timers, global unlock mask and
  LOCAL paid/refundable mask). Never share refund eligibility across tanks.
- The practical NVS save ceiling is **5920 bytes**. Worst-case space needs
  three copies plus 64 reserved entries: 628 of 630 entries at this ceiling.
  Expanded saves MUST NOT be written there. They use the existing **128 KiB
  SPIFFS `storage` partition**, mounted at `/profiles`, through aquarium_store's
  CRC/readback dual copies. NVS `tank`/`save` is read-only migration input.
  No repartition, no PSRAM, no SD dependency for the active profile.
- Only format a byte-for-byte erased partition or the verified exact empty
  legacy 64 KiB SPIFFS template inside the 128 KiB partition (16 metadata-only
  blocks, zero erase counts, all lookup/data bytes and remainder erased).
  Real hardware revealed that older empty template; any payload/damage must
  block formatting. `tools/test_profile_storage.py` pins the guard.
  Mount/copy corruption blocks
  saving and stale NVS fallback, never creates a success-shaped fresh tank.
  A durable wipe tombstone overrides old copies/NVS before cleanup; a failed
  post-wipe fresh write reports pending retry, not a false failed wipe.
  A blocked load is visibly reported and must not acknowledge a pending OTA
  image as valid. Never silently present it as a successfully loaded empty tank.
- Existing binary fields and offsets are frozen. Append-only tails, explicit
  migration, validators, frozen old-save fixtures, and `_Static_assert` locks
  are required. Never reorder fields or rename NVS namespace `tank`, key `save`.
- `tank_init` and `companion_init` initialize fields individually: explicitly
  initialize new state in both relevant paths; do not assume a whole-struct clear.
- RPG is a 24-byte `tank_rpg_t`; new-game records are a 40-byte `game_records_t`.
  RPG participant is uint32; chapter contributors are uint16 lineage IDs.
  IDs above 65535 must become unknown, never alias an unrelated ancestor.
- Older-schema JSON fixtures must remove later fields: `<11` polish,
  `<12` story, `<13` games, `<14` rpg, `<15` creative, `<16` profile/games_extra,
  `<17` care.
- Update every relevant import/export, active/Bank/away/tank-switch path,
  rollback snapshot, simulator fixture, viewer, and documentation together.
- Active-save writes are event-driven/coalesced (30 seconds; 10-minute heartbeat).
  Do not add per-frame flash writes. Failed loads must not overwrite saves.
- Rendering uses internal RAM, cached/tiled backgrounds and DMA stripe buffers.
  Check `memory_plan.h`, heap and application-slot headroom after changes.
  Each OTA app slot is `0x1e0000` bytes; do not casually grow large assets.

### FPS, internal RAM, PSRAM, and storage budgets

- **There is no PSRAM on the supported board.** Required buffers, renderer
  scratch, masks, task stacks and DMA transfers must fit internal RAM.
  Do not "fix" an allocation failure by requiring PSRAM, selecting a PSRAM board,
  or enabling PSRAM-only code. SD storage cannot replace working RAM.
- The loop has a **40 ms minimum frame interval (25 FPS ceiling)**, not a
  promise of 25 FPS in a moving aquarium. The practical populated-tank goal is
  **10 FPS**, not the earlier 15 FPS aspiration.
- Historical hardware observations in [firmware/CYD.md](firmware/CYD.md) and
  [the feature guide's performance notes](docs/CYD-FEATURES.md) include
  roughly **10.4-11.5 FPS** in Normal with/without Medium Flip clock/weather,
  and **10.6-11.8 FPS** in a warmed-up 1.2.0 sample with one resident.
  Earlier populated Ice Cavern samples were **7.5-8.4 FPS**. These are
  scene-specific observations, not current-release guarantees. Report new
  measurements with release/build, scene, population, resolution and effects.
- A stationary retained Home menu can report about 25 FPS while sending no
  pixels. That is **not** aquarium FPS. Avoid benchmarks based only on menus,
  simulator speed, the frame cap, or an empty tank.
- Measure on the real device after warm-up across several 10-second `display`
  log windows, including busy tanks and clock/weather on/off at both scales.
  Record total FPS plus poll, tick, prefetch wait, render, flush, tail and sleep;
  renderer stage timing and SPI fill/wire/send timing identify the actual cost.
  Compare like-for-like staged scenes; do not change the user's tank to make a
  result look faster. Repeat after adding creatures, decorations or effects.
- Enlarged uses a **240x180 RGB565 framebuffer = 86,400 bytes**, allocated as
  internal DMA-capable 8-bit RAM. Normal reuses that allocation as **three
  320x80 tiles**, not a full 320x240 framebuffer. Do not add a second framebuffer
  or a native full-screen allocation (**153,600 bytes**) to simplify drawing.
- The LCD stripe allocation is also internal DMA RAM. Its size follows
  `320 * CYD_STRIPE_ROWS * 2`; preserve the alternating stripe/pipelining
  lifecycle. Changing stripe rows, tile size, alignment or task order requires
  memory and transfer checks, not just a successful compile.
- Normal reuses the framebuffer tail for a compressed backdrop/static-scene
  cache. Optional artwork/clock caches are bounded and fall back to live
  drawing if unavailable. Some scene/card/dirty caches are deliberately
  disabled on CYD. Never enable them wholesale or assume all caches can
  coexist. Preserve explicit diagnostics and tested fallback correctness.
- At boot, `tank task up`, and periodic `TAG`/`display` lines, inspect **free
  internal heap and largest internal block**, cache usage, and rows rendered/
  flushed. A healthy total heap does not prove a contiguous DMA allocation
  will fit. Check network startup, SD operations and both display modes too.
- Prefer preallocated scratch, bounded static/cached scenery and localized
  dirty regions. Avoid per-frame allocation, full-screen animated effects,
  unconditional full redraws and new continuous simulation. Keep old/new
  damage bounds and cached/live pixels equivalent.
- Flash has **two 1,966,080-byte OTA app slots**, not one 4 MB app budget;
  the SPIFFS partition is only **128 KiB**. Firmware image headroom, internal
  RAM, NVS save entries and SD capacity are separate limits. Report app bytes
  and free slot space from each release build. A smaller file or more SD
  space does not establish runtime heap or NVS safety.
- Local 2.0.0 refactor build measured **1,760,944 app bytes**, leaving
  **205,136 bytes (10.43%)** per slot; static DRAM **71,331 bytes**.
  Hardware checks retained four original residents/wallet, exercised all
  eighteen game entries at both scales and verified SD/Bank/save paths.
  Populated Normal samples were 9.4-9.6 FPS and Enlarged 7.1-7.2 FPS;
  these are scene-specific, not a controlled speedup. See firmware/CYD.md.
  Rebuild and measure after changes; verify release publication separately.
- Relevant read-only diagnostics include `state`, `clock`, `sd status`, `ota`
  and the periodic logs (`help` lists current commands). For serial watching,
  use `python tools\director.py -p COM4 --watch` after confirming the port.
  Commands such as `seahorse on|off` mutate the scene: use only an authorized
  staged tank for A/B tests, never casually on the live aquarium.

## UI and hardware traps

- UI page coordinates and framebuffer coordinates differ. Inspect the relevant
  scale/transform before changing touch bounds. Raw XPT2046 coordinates also
  depend on affine calibration, orientation and finger bias.
- Settings subpages may need device-option forwarding in the HAL touch path,
  not just core rendering. Verify both visible labels and actual device actions.
- Keep CYD LCD orientation/color fixes, active-low rear LED wiring, and touch
  transforms intact. Do not infer board pin values; read `board_pins.h`.
- Retained rendering must clear both old and new bounds for moving effects and
  captions. Test retained/full pixel equivalence and clip effects outside text.
- Page text wraps differently at each resolution. Test actual glyph bounds,
  longest prompts, button collisions, and help/return paths; do not rely on
  ellipses or a screenshot from only one scale.
- `sim_frame(0)` can normalize tank state. Warm staged fixtures before asserting
  browsing does not mutate saves. Change resolution through the real UI rather
  than editing framebuffer dimensions in a serialized fixture.
- Display SPI access is coordinated by the firmware loop. New display tasks
  can cause contention/deadlocks; reuse existing paths.

## Build and validation

Run commands from the repository root (PowerShell on Windows). Use an available
compiler; do not hard-code a previous assistant's session-local Zig path.
The native runners accept `--cc gcc`, `--cc clang`, or a path to Zig.
ESP-IDF setup and managed cJSON are needed by the corresponding builds.

```powershell
python -m unittest discover -s tools -p "test_*.py"
python tools\test_tank_backup.py --cc gcc
python tools\test_tank_backup.py --cc gcc --flat-edges
python tools\test_rpg.py --cc gcc
python tools\test_page_art.py --cc gcc
python tools\test_creative.py --cc gcc
python tools\test_profiles.py --cc gcc
python tools\test_cyd_display.py --cc gcc
python tools\test_cyd_settings.py --cc gcc
python tools\test_ambient.py --cc gcc
python tools\test_weather.py --cc gcc
python sim\run.py --cc gcc --smoke
.\tools\build_cyd.ps1
git diff --check
```

- Choose the smallest relevant subset for a focused change; releases run the
  full matrix in CI. GCC treats warnings as errors; Clang-only success is not
  sufficient evidence of GCC compatibility (watch signed/unsigned comparisons).
- Core tests cover migrations, transactional saves, layout guards, retained
  rendering, games and optional RPG isolation. Keep these protections.
- Profile tests cover immutable first-run mode selection, wipe boundaries,
  global progression across local aquariums, Hard starvation/recovery and
  extended-save migration/failure safety. Never replace these with UI-only tests.
- `test_page_art.py` also checks distinct game thumbnails, goal/header label
  fit, and visitor/decor preview determinism, reserved bounds and row/tile
  equivalence at both resolutions. Keep it in checks and release workflows.
- Screenshot command:
  `python tools\capture_cyd_gallery.py --cc gcc --out <separate-output-directory>`.
  Never capture against the user's live save. Refresh relevant named images,
  ensure Markdown links resolve, and stage no generated binaries/scratch saves.
- Web changes: Python staging tests plus desktop/mobile real-browser checks,
  decoded images, no horizontal overflow, local links, correct release version.
  Verify flasher acknowledgments start unchecked, update disabled, factory
  hidden; no serial operation should run merely from visiting a page.

### Physical-device safety

- Build/inspect first. Flash only when requested; the current publication task
  does not imply permission to reset or advance the user's aquarium.
- Prefer `.\tools\build_cyd.ps1 -Flash -Port COM4` for an existing installation.
  It queries the actual running slot. Never assume `ota_0` (`0x20000`) versus
  `ota_1` (`0x200000`); never rewrite NVS, partition table or OTA selection during
  a normal upgrade. Keep a backup and stable power.
- Factory flashing is destructive and only for a new board or an explicitly
  approved reset. An update refusal must never fall back to factory flashing.
- Close serial monitors before flashing; do not kill unrelated processes.
  Preserve DTR/RTS behavior when using serial diagnostics.
- Prefer read-only diagnostics and separate simulator staging. If visual proof
  from a webcam is unavailable, say so; do not claim a renderer image is hardware.

## Documentation and release checklist

For feature work, update the relevant entries in:

1. [README.md](README.md): release status, overview, menu paths, build/install.
2. [docs/CYD-FEATURES.md](docs/CYD-FEATURES.md): user-facing behavior and screenshots.
3. [firmware/CYD.md](firmware/CYD.md) and [docs/OTA.md](docs/OTA.md): hardware,
   persistence, backup/update constraints when changed.
4. `docs/media/cyd-features/`: actual-renderer screenshots, no invented UI.
5. `web/index.html`, `flash.html`, `viewer.html`, `play.html` and staging tests when relevant.
   Screenshot selection is `FEATURE_IMAGES` in `tools/prepare_web_installer.py`;
   images come from docs during Pages staging, not duplicated web assets.
6. `tools/package_cyd_release.py`: accurate new-build notes and install safety.
   Reuse `web/logo.svg` in website, docs and release notes. Use an absolute raw
   GitHub logo URL in release notes. Do not rewrite older release claims.
7. This file when architecture, budgets, commands or maintenance contracts change.

### Publish only when requested

- Source push, Pages deployment, GitHub firmware release, and physical-device
  flash are separate operations. Clarify when ambiguous; never report one as
  evidence of another.
- Inspect `common/version.h`, Git tags, remote main, and latest GitHub release.
  For a new release update numeric/string version together, docs, website
  release copy, and release notes. Do not show source-only work as already in
  older downloadable binaries.
- Before every push/publication, update relevant website and documentation
  alongside behavior changes. Remove roadmap/coming-soon claims for implemented
  features, refresh release/version requirements and screenshots where needed,
  and verify deployed content after Pages completes.
- Tiny uses layout bit 5 (low size bits zero); detail size uses bits
  6..7, caption size uses delay bits 3..4. Use the shared packing/validation
  helpers; preserve unrelated fields and the frozen 6008-byte/schema-16 prefix.
  Test Small/Medium/Large detail and caption preferences at both resolutions,
  with all five sizes/positions, long captions, sync/no-sync, exact bounds,
  time/detail non-overlap and live/tiled-cache equality. Auto-fit is intentional.
- Jumbo is a size, not a replacement numeric theme: preserve all seven clock
  designs, colors and detail flags. Test identical-color theme distinctness,
  both scales, weather captions, cache/tile parity and save round trips.
- Include real browser input and reload persistence whenever the playable
  website simulator changes. Browser storage errors must be visible; do not
  claim that a save is durable before its storage write succeeds. Keep browser
  saves separate from the device and document unsupported hardware features.
  Build with Emscripten 4.0.18/cJSON 1.7.19; run the pinned Playwright 1.56.1
  Chromium suite with Node 22.16.0 before deployment. Reject corrupt native
  archives/preferences before import replaces localStorage; preserve exact
  stored data on failed startup. Never expose credential/update flows in web play.
  Public Pages must check out latest stable released source, never main; verify
  its tag/commit and browser version against the staged installer. Display the
  tag/commit. Test normal/fullscreen/letterboxed and both resolutions, exact
  pellet columns, mobile touch and ignored margins; preserve the pellet cap.
  Browser feeding is Place food then tap a column; no position-preset buttons.
  Simulator fish cards must dispatch lock, sponge/scissors and DONE like the
  hardware; control presses must not start tank strokes. Test both scales.
  Clock/Weather has one hub/preview; Face, Colors, Text / Effects, Time / Zone,
  and Weather each return one level. Weather Text and Weather Size share the
  existing caption-size bits (also events); reserve icon rows and include the
  code in clock-cache keys. No invented icons for unavailable readings.
  Name this fork Pocket Tank CYD in user-facing titles/docs; retain original
  Pocket Tank attribution and frozen compatibility identifiers.
- Run relevant tests, check diff/staging for secrets and generated artifacts,
  commit with the required coauthor trailer, and push main without force.
- Tag the verified release commit `vMAJOR.MINOR.PATCH` and push that tag.
  `.github/workflows/release.yml` builds/tests/packages and publishes.
  Never move an existing tag or replace historical firmware assets.
- Verify the release action, eight download assets (ZIP, app, factory, PTU,
  checksums, notes, install instructions, license), checksums, board/version
  markers and stable/latest status. Check actual package contents, not only
  the presence of a release page.
- Pages uses `tools/prepare_web_installer.py` to fetch the latest stable release,
  verify checksums/board/layout/app match, then stage the site and both firmware
  images. Release workflow explicitly dispatches Pages after publication.
- Wait for Pages success; verify public home/flasher/viewer/images, manifest
  version, app checksum and latest download. Keep update/factory warnings.
- Use available GitHub tools or authenticated REST without exposing tokens.
  Never save or print credentials. Preserve historical release bodies/assets
  when applying branding. Report commit/tag/release URLs and verification status.
- End with a clean worktree or explicitly describe remaining changes/failures.
  Do not label a running/failed workflow a successful publication.
