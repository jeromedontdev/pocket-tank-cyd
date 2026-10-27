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

- Release 2.3.13 content expansion: exactly six new wearables (IDs7–12) and twenty
  static placeable ornaments (shop IDs43–62); existing IDs0–42 never move.
  Schema19 / 6336 bytes appends `decor_content_t` at6168, retaining schema18's
  four alignment bytes explicitly. Legacy inventory stays16 slots/100 bytes;
  live decoration ranks/ordering have36 slots; the new20 stored/paid counts,
  ownership bits and geometry live in the appended tail. Shared storage still
  caps at64, extras at8. Profile unlock/refund masks remain uint64 (highest ID62);
  never shift legacy uint32 ownership by a new item ID. Renderer vegetation
  piece masks are uint64 for35 pieces. No ambient animation added. Companion
  v7 keeps the12-byte bond layout: ownership bits0–7 in wearables, bits8–11
  in tricks bits3–6; learned tricks remain bits0–2. Use companion_wearables,
  mask trick-presence checks with7, preserve bank/traveler copies and colors.
  Old schemas1–18 and6168-byte saves migrate with an empty content tail.
  Actual IDF SPIFFS geometry with6336-byte saves uses86,016/131,072 bytes
  for eight archives, active redundancy and an in-flight replacement (<75%).
- Harbor/garden art uses detailed static 40x40 procedural materials: structural
  shading, distinctive fittings and material-specific details, shared by tank
  and shop previews. Preserve transparency (especially bridge/gate/moon holes),
  exact indexed-cache RGB, night dimming and full/tiled equality. The native
  page-art test checks all20 palettes, defining detail pixels and cache parity;
  `tools/capture_decoration_art.py` produces individual PNGs and a labeled sheet.
  Do not add animation, bitmap banks or persistent state for this art.
- Wardrobe fit is shared by purchase/equip and render/clearance geometry.
  Jellyfish support ties, bowties and bandanas as front accessories; only the
  wraparound scarf is unavailable. Preserve legacy ownership/equipment/colors
  without drawing incompatible gear. Run `tools/test_wardrobe.py --cc gcc`.
- 2.3.8 integrates decoration copy/storage controls into each normal shop entry,
  removing separate Inventory/Mussel navigation. Additional-copy purchases require
  a prior first purchase (including Creative). Pearl Mussel is stable shop ID42,
  price120 per aquarium/free Creative; no care-task unlock or collection screen.
  Passive healthy live income auto-credits transactionally, retains legacy pearls
  on failed saves, and keeps the shared15/day/no XP/no offline rules. Mussel v2
  uses owned as an eight-tank mask in the same8-byte layout; v1 migrates into
  the saved active aquarium. Never treat v2 owned as a boolean for drawing.
  Inventory/mussel native regressions now run in the default backup test runner,
  not just its previously separate --inventory-only executable selector.
  User physically confirmed v2.3.7 touch and trophies fixed; ELEGOO dimness was
  identified as reflective screens/poor viewing angles, not backlight duty.
- 2.3.7 starts new aquariums with companion trophy decor disabled in all modes.
  Preserve existing saved decor preferences; Life > Rewards can enable trophies.
  ELEGOO fresh affine defaults reverse X/preserve Y; saved calibration is untouched.
  Brightness diagnostics on the local ELEGOO already request duty255 without
  night/room dimming, matching vendor PWM/panel setup; no brightness fix is claimed.
- 2.3.6 development adds `PT_BOARD_VARIANT=cyd|elegoo|unified`, separate build
  directories and `PT_BOARD_ELEGOO=1` propagated from tankcore to HAL.
  Original CYD `cyd2432s028` keeps its custom LCD sequence; ELEGOO `elegoo28`
  uses ELEGOO's ILI9341V power/gamma sequence/BGR with the same LCD/touch/SD pins,
  red LED22 and audio-enable4 high. GPIO34 is battery sensing: never initialize
  its ADC as an LDR; report room-light controls unavailable, retain manual
  brightness. An ESP32-32E module label alone does not identify a display.
  Exact official source and app-only conversion instructions are in CYD.md.
  Preserve all partitions/saves. Normal USB/SD/Wi-Fi updates refuse board
  mismatches; deliberate local recovery requires a full dump, positively
  detected slot and `-AllowBoardMigration -AppSlot ...`. Do not make a general
  cross-board bypass. Separate ELEGOO assets use `pocket-tank-elegoo-*`,
  `elegoo-update.ptu`, `elegoo-ota.json` and `elegoo-SHA256SUMS.txt`;
  SD discovery still uses `update.ptu` so rename when copying. Website has an
  explicit selector; freeze selection during USB operations and bind fetched
  manifest, banner and app marker to that identity. Compile/runtime tests
  cannot establish physical LCD colors/touch: require a view of the actual
  board or user confirmation. The user moved COM8 into the authorized webcam
  view during ELEGOO testing; COM8 backlight off/on was verified on camera.
  Do not assume it still views COM4. Port numbers are local,
  not a public hardware identifier.
  Current validation status: both local boards have run the byte-identical
  unified app, with saved runtime profiles and full app readback/reboot/save
  checks. COM4 retains Bolt/Pip/Kelp/Odie; COM8 retains Mira/Bolt and its saved
  touch calibration. The camera views COM8, not COM4. White/striped LCD root
  cause identified: failed SD mounting followed by spi_bus_free(SPI3_HOST)
  disrupted the live SPI2 LCD DMA on this ESP32/IDF stack. Keeping SPI3
  allocated for the boot lifetime restores the full UI, not just GRAM reads.
  Preserve that intentional failure-path allocation; do not "clean it up"
  without testing actual LCD updates after a missing/failed SD card. Native
  test_cyd_sd_init.py covers bus failure, mount failure and mounted SD.
  Isolated no-save tests using both raw polling SPI and ESP LCD DMA showed
  correct RGB bars; the factory program also worked. A full power cycle did
  not fix the failure. No evidence supports presuming defective hardware.
  Controller IDs 04/D3/DA/DB/DC are zero despite valid status/RGB GRAM reads:
  never use those zero IDs for automatic profile detection.
  The recovered 1 MHz diagnostic UI was mirrored/upside-down and slow.
  Restored 40 MHz and automatic CS; ELEGOO mirror(true, false) now gives
  upright/readable text on the webcam. Measured 16.5-16.6 fps, 60-61 ms/frame,
  17.5-18.1 ms flush on the full-resolution saved welcome screen.
  User reported backwards touch and then fixed it using the calibration tool.
  ELEGOO's uncalibrated affine default flips logical X: {{-1,0,239},{0,1,0}},
  matching the negative-X/positive-Y axes of the previously working calibration.
  This corrects the earlier Y-only default, which reversed the wrong axis.
  Keep the raw XPT coordinate basis unchanged so existing saved affine_v1
  calibration still overrides defaults without invalidation. A temporary raw
  Y-mirror build was replaced with this compatible fix; COM8's boot confirms
  its saved calibration loads. test_cyd_touch_defaults.py tests both boards,
  both UI sizes and a saved affine override. Fresh uncalibrated physical touch
  still needs checking; do not erase the user's working calibration to test it.
  Do not leave
  diagnostic bars, startup delays or alternate-controller tests enabled.
  Physical display and the user's calibrated touch passed; fresh-default touch
  is covered by native coordinate tests, not a claimed physical fresh-board test.
  Check actual release/tag/Pages status before claiming publication.
- User's requested direction after COM8 is fixed: prefer one binary for
  compatible ESP32/4MiB CYDs, with saved runtime hardware profiles for panel
  initialization/order/inversion, touch and peripheral capabilities. Keep an
  installer/serial/BOOT-button recovery selection because a wrong profile
  can hide the screen. Use detection only when trustworthy, never all-zero
  IDs; don't guess pins or probe active outputs indiscriminately. Preserve
  old board identities safely during migration, rollback and each profile's
  calibration. Do not promise one binary for different ESP32 chip families,
  parallel/RGB buses, larger framebuffer budgets or untested boards. Current
  runtime implementation is in common/board_hardware.h and
  firmware/main/board_hardware_esp.c. Unified family identity is
  `cydesp32profiles`; NVS namespace/key is `pt_hardware` / `profile`
  (1 CYD, 2 ELEGOO). Missing/unknown IDs wait for explicit `hardware cyd`
  or `hardware elegoo` over USB before hardware init; NVS read failures
  stop startup with an error. Running `hardware` queries the profile;
  a change saves the aquarium first, commits the profile and reboots.
  Network setup/update mode allows queries only. Saved affine_v1 remains
  untouched. Runtime selection covers both LCD sequences, RGB order/mirror,
  touch defaults, LED/audio pins, LDR access and room-light UI. New pet
  projects (Pocket Terrarium, etc.) should reuse this hardware/persistence
  boundary rather than put species/game logic in board profiles. Keep
  capabilities explicit and test native/browser fallback builds.
  Production migration is tools/flash_unified.py via build_cyd.ps1
  -Board unified -HardwareProfile cyd|elegoo -Port ACTUAL_PORT -Flash
  -Backup NEW_FILE. It requires a new full 4 MiB backup, live/fresh identity
  and matching OTA slots, validates partition/OTA metadata and family marker,
  refuses downgrade, reads back the full app, verifies profile persistence and
  restores the backed-up app if verification fails. Do not accept guessed
  slots or a generic migration bypass here.
  Website hardware choices use one shared app. Their update manifests bind
  `board=cydesp32profiles` plus explicit `hardware=cyd2432s028|elegoo28`.
  Legacy migration accepts only that hardware's old marker/banner. Existing
  unified images must report the selected saved profile; wrong profiles are
  refused, not silently changed. Only missing profiles are provisioned after
  verified app-only flashing. Factory images differ only in blank NVS, generated
  with Espressif's nvs generator; never flash them onto existing aquariums.
  Unified SD/Wi-Fi channels use unified-update.ptu / unified-ota.json;
  legacy channels retain their board-specific assets and require USB to migrate.
  Release packaging retains all three families. Installer browser tests exercise
  both actual legacy/unified handlers with simulated serial/bootloader devices;
  hardware readback/profile/reboot tests passed on both local boards. Public
  status still requires actual GitHub/Pages verification, not these local facts.
- 2.3.4 moves all eight aquarium archives to internal SPIFFS; SD is only for
  legacy import, backups, screenshots and offline updates. Firmware owns this
  policy in `persist_port_esp.c`; `sd_port_cyd.c` exposes read-only legacy import.
  Boot visits inactive labels to import missing archives with the original SD
  inserted. Never delete or refresh SD originals during import, and never
  resurrect stale SD data when any local archive exists but is corrupt.
  Active saves remain dual-generation; inactive archives keep one verified
  generation after alternate-write/readback and retiring the previous file.
  Eight archives + two active copies + markers + one replacement use 80 KiB
  under actual IDF SPIFFS geometry (<75% of 128 KiB). No partition/schema change.
  Native port tests cover SD-free reads/writes, interruption, migration, corrupt
  local rejection and wipe cleanup; rerun capacity checks as save tails grow.
  Whole-collection backup is NOT implemented: JSON covers the active tank and
  shared state, not slot identity/all archives. Old SD copies stop updating.
- 2.3.2/2.3.3 add `ota.json` at the latest stable GitHub release, pin the app URL
  to its immutable tag and require format/fork/board/version/HTTPS/size/SHA-256.
  Read back the entire downloaded asset before `esp_https_ota_finish` selects
  the inactive slot. Standard images are unsigned: trust is certificate-checked
  HTTPS/release account plus integrity checks, not secure boot/signatures.
  ESP-IDF descriptor.version is PT_RELEASE; PT_BUILD_ID separately displays Git.
  Update mode must save successfully then reboot before simulation/SD starts;
  defer fish/decor scratch and manifest buffer until needed for TLS headroom.
  Real GitHub asset certificate verification failed with PSA -141 (RAM) before
  this fix; never weaken TLS checks to work around it. SD and Wi-Fi buttons
  remain separate. 2.3.5 menu cleanup renames SD Updates to Firmware
  Updates, keeps Screenshot to SD only in Device and weather text size only
  on Weather. Versions 2.3.4 and older retain the previous labels/duplicate shortcuts.
  The measured menu audit covers 434 states (217 per actual framebuffer size),
  including long network names, full Bank pages and maximum game records.

- Release 2.3.0 inventory/income: schema 18 / 6168-byte binary, frozen
  previous schema-17 prefix unchanged. Shared storage holds 64 physical
  pieces; eight additional simple ornaments per aquarium have independent
  position/depth. Complex plants/castles/coral/clusters/pumpkins stay one of
  each placed type per tank. Store/switch/place preserves copies; paid/free
  provenance, failed saves and stale snapshot transfers are tested.
  Care now pays 30/30/65 coins (125 total), unchanged 50 XP; games pay 1/round,
  up to 5/game/day (90 across 18), practice/challenge and tanks share each
  game's quota. Legacy same-day earnings conservatively reduce each allowance;
  day/fallback rollover resets all. No game XP or snapshot quota rewinds.
  Non-fry pending birth welcomes retire on load/poll without changing fish.
  Welcome and birth completion save transactionally; failure keeps UI open
  with a retry banner instead of silently leaving a persistent old flag.
  In 2.3.0-2.3.7, claiming all three tasks unlocked a profile-wide pearl mussel;
  2.3.8 makes it a shop resident with automatic payout: one coin
  per 600 healthy live seconds, max 15/day with pending coins retained on failure,
  no offline income or XP. It rests for 344 seconds then crawls for 16,
  with bounded old/new damage; no new art buffer. Shared state follows the
  active tank; backups cannot rewind an already-owned mussel/quota but can
  recover it onto a never-unlocked profile. Creative has no income and keeps
  free-copy restore behavior.
- Performance baseline from **2.2.7**: full-capacity LCD DMA halves at unchanged 40 MHz,
  off-tile polygon palette culling, identical-input pitch memo, row-shaded
  urchins, conservative seahorse coronet culling/exact 96-byte tail-width memo,
  and clipped clock-span replay (retain alpha-255 blend semantics).
  Both scales/all details/gameplay modes benefit; LCD transfers are CYD-only.
  RAM-only serial `benchmark` pauses real care, never runs progression/saves,
  uses two seeded fixtures and one evolving current-tank copy with clock.
  2.3.0 added a fourth maximum-extra-ornament/mussel fixture.
  Local Normal/Balanced five-fish copy: 10.79 FPS, lowest 120-frame window
  10.22 with 5 ms/frame service reserve; not full gameplay/network timing or
  a universal minimum. Ordinary four-fish windows included sub-10 results.
  Current-renderer 5-minute soak passed 283,024 frame sets, 12 hashes equal
  2.2.6. See hardware guide for scope; do not claim public deployment without
  checking release/Pages. No new image buffers, save fields or gameplay.
  2.2.6 introduced one shared Back on Wardrobe Colors, unchanged
  Creature Colors Next Type, draft/cancel/save behavior and both-scale headers.
  Lossless adjacent backdrop-row reuse keeps the same 12 KB budget; Simple
  batches consecutive live fallback rows. Fish scan edges avoid modulo.
  Perched-seahorse contact shadows include the preceding tile/region row.
  `test_renderer_stress.py` profiles synthetic five-fish scenes and checks
  full/tile/retained parity, cache/detail/palette/clock transitions and no tank
  mutation. Positive soak durations include a shadow regression that fails
  the 2.2.5 renderer; collect old timing baselines with `--seconds 0`.
  Native timings are not hardware FPS. No new buffers, save fields or gameplay.
  2.2.5 introduced consistent font coverage at fractional sizes,
  sharp native 5x7/integer sizes and common punctuation. UI and Wi-Fi share
  rasterization and metrics; Wi-Fi remains case-sensitive. Jumbo text/Flip
  digits share coverage; retain distinct clock-face styles, run recording,
  dirty bounds and row/region clipping. Tests independently integrate 35
  subcells per output pixel and assert conserved ink area. No new buffers,
  font downloads, save fields, layout/touch-target or gameplay changes.
  Optional page-art captures include typography at both resolutions.
  2.2.4 refines sand/depth/pebbles, layered distant
  scenery, five floating ornaments and all five rare visitors. Food/bubbles
  share bounded procedural materials; wipe/trim marks use existing successful
  active-stroke flags, with old/new damage restoration. Preserve quiet Simple
  water/hidden ambient creatures, same food/sand/care art, night/tile/cache
  equality and unchanged helpers/castle/living coral/reef cluster. No new
  buffers, simulation state, saves, visit rules or progression. The optional
  page-art gallery also captures visitor/care lineups at both sizes.
  2.2.3 shades all ten fish bodies/fins and six wearables.
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
- User testing workflow: after verified firmware changes, build and flash the
  original CYD on **COM4** by default, then verify app readback, boot and saves.
  Preserve a fresh full-device backup and use the app-only unified updater;
  never touch COM5. Commit/publication remain separate from this authorization.
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

- Supported boards: **ESP32-2432S028 CYD** (existing revision) and
  **ELEGOO ESP32-32E 2.8-inch ILI9341V**, ESP32, 4 MB flash, no PSRAM,
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

- Current source binary save **6336 bytes**, JSON **schema 19**; imports schemas 1-18.
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
  No repartition, no PSRAM, no SD dependency for any aquarium after legacy import.
  Inactive archives use `/profiles/tanks` (SPIFFS virtual path, no mkdir);
  keep filenames below CONFIG_SPIFFS_OBJ_NAME_LEN. Wipe's durable tombstone
  precedes archive cleanup; retry cleanup before creating a fresh active save.
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
- The framebuffer allocation remains **86,400 bytes** of internal DMA-capable
  8-bit RAM. Live rendering uses two alternating **40-row tiles** in that same
  allocation at both scales; Normal retains the previous backdrop-cache tail.
  A core-1 LCD worker owns each submitted source until completion while core 0
  renders into the other half. Capture viewport bounds at submission; never
  read renderer globals in the worker. Drain before simulation/resolution,
  sleep, or synchronous screens. Do not add a second framebuffer
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
- Fish stay full-rate; scenery clocks are independent (Balanced effects5Hz/
  grass2Hz, Enhanced10Hz/4Hz, staggered grass beds). Damage planning compares
  merged rectangle costs, not the old fixed40% promotion threshold. Serial
  `benchmark` compares synchronous/full and pipeline full/partial/adaptive
  modes with identical40-row tiles. Frame pacing logs include2-ms upper-bound
  p50/p95/p99, exact maxima, slow counts, and main-stage maxima; worker timings
  overlap rendering and must not be added again to the main-task frame budget.
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
- `test_page_art.py` compiles test-only menu observers: measured glyph bounds,
  text collisions and button label fit across menu/submenu fixtures at both
  CYD scales. `--gallery <session-directory>` exports the audited screens.
  Assert the actual framebuffer width for each pass: `progression_boot` can
  load a different display scale, so restore the intended fixture resolution.
  Update touch routing and browser/simulator tests when moving a control.
  Keep screenshot status visible on Device, destructive actions separated,
  and both update sources discoverable behind the neutral Firmware Updates label.
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
python tools\test_renderer_stress.py --cc gcc --frames 1200 --seconds 30
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
  New ornament/wardrobe art review:
  `python tools\capture_decoration_art.py --cc gcc --out <separate-output-directory>`.
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
  Public Pages must build game/updater code from latest stable released source,
  never main. The deployment overlays only current web/index.html,
  web/flash.html, web/board-profiles.mjs display names and their installer UI test
  for copy-only fixes. Keep internal hardware keys/paths and update logic
  compatible with the release; do not use this exception for behavioral changes.
  Public labels are CYD / ELEGOO CYD, not model numbers. Verify
  the release tag/commit and browser version against the staged installer. Display the
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
- Verify the release action, nine download assets per board (ZIP, app, factory,
  PTU, OTA JSON, checksums, notes, install instructions, license), checksums, board/version
  markers and stable/latest status. Check actual package contents, not only
  the presence of a release page.
- Pages uses `tools/prepare_web_installer.py` to fetch the latest stable release,
  verify checksums/board/layout/app match, then stage the site and both boards'
  app/factory images and manifests. Release workflow explicitly dispatches Pages after publication.
- Wait for Pages success; verify public home/flasher/viewer/images, manifest
  version, app checksum and latest download. Keep update/factory warnings.
- Use available GitHub tools or authenticated REST without exposing tokens.
  Never save or print credentials. Preserve historical release bodies/assets
  when applying branding. Report commit/tag/release URLs and verification status.
- End with a clean worktree or explicitly describe remaining changes/failures.
  Do not label a running/failed workflow a successful publication.

## Reuse for Pocket Terrarium and other virtual pets

- This repository remains Pocket Tank CYD. Do not add terrarium creatures,
  alternate pet modes or another product's assets to its tight OTA slots unless
  explicitly requested. The earlier Terrarium prototype was canceled; do not
  resurrect it or alter a sibling checkout as incidental cleanup.
- Start a separately requested product in its own repository/build/release
  channel. Preserve upstream license/attribution. Reuse proven platform ports,
  tiled renderer/input architecture, transactional storage, tests and packaging;
  replace aquarium-specific simulation, care economy, scenery and terminology.
  Avoid building a generic pet framework before a concrete second product needs it.
- Trace `common/` simulation/progression and portable renderer, platform ports
  in `firmware/components/tankcore` and `firmware/main`, and simulator/browser
  backends before copying. Keep hardware services separate from species rules.
  Recheck framebuffer/RAM budgets, 40 MHz LCD DMA and both UI scales.
- New products require distinct firmware identity, fork/channel checks, image
  marker and package identity as appropriate; a shared physical board ID alone
  must not allow cross-product OTA/SD installs. Preserve board compatibility
  checks and reject the wrong product before selecting a boot slot.
- Never reuse Pocket Tank save namespaces/files/collection IDs on a real keeper's
  board without explicit migration/reset consent. Design a versioned save contract,
  validators and fixtures for the new pet; do not reinterpret aquarium bytes.
  Retain missing-versus-corrupt distinctions, transactional switching, durable
  wipe, no stale-source resurrection and verified archive replacement.
- Flash capacity, filesystem working space and heap are separate budgets.
  Re-run `test_internal_tanks.py --cc <compiler> --idf <esp-idf>` for actual
  filesystem packing, plus profile/storage, renderer, browser and OTA tests.
  Test synthetic pets in the simulator, never by overwriting the real aquarium.
  Validate hardware with backups and app-only flashing; preserve A/B rollback.
