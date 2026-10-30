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

## Game direction and design (2.4.0, authoritative)

This section is the current design contract. Where an older bullet below
(handoff logs, historical revisions) disagrees, this section wins; keep the
old bullets only as history and evidence.

### Pillars

- **A serene aquarium first.** The tank is the main screen; menus are tools
  around it. Prefer calm motion, few overlays, no chores, no neglect debt and
  no random stress injected to make meters move. Decor choice stays rich, but
  every new visual must earn its frame-time (see the FPS budgets below).
- **One loop: buy, breed, sell.** Buy fish in the Fish Market, raise
  same-variety pairs, and sell offspring. Genetics (alleles, vigor, mutations,
  rare marks), stage, care and pedigree set the price. Purchases must never
  resell for an immediate profit.
- **One currency: doubloons** (gold coin icon in every purse). XP, levels,
  firsts, trophies, object stories, frames and discoveries are retired as
  active systems; keep their historical save fields frozen and unused.
- **Original work only.** Do not reintroduce assets, names or code from the
  upstream project the fork started from. New art is procedural or drawn by
  tools in `tools/` in the established crafted pixel style.

### Navigation (keep it shallow)

- Main: Fish, Tank, Market, Games, Casino, Help; Settings is the cog at the
  bottom right. Tapping a fish opens its Fish Summary directly; one Back
  returns to the aquarium. Every companion submenu has Menu (to Main) and
  Back (to its real parent).
- Fish hub: Summary, Rename (full-page QWERTY), Family, Genes, Life, Wardrobe,
  `<`/`>` and Fish Bank. Fish Summary shows five meters (Trust, Energy,
  Fullness, Calm, Vigor) and info lines; it has no action grid.
- Tank: Tank Summary (with Name & Identity), Decor & Storage, Fish Food,
  Themes, Sand & Water.
- Market hub: Fish Market, Creature Market, Collection, Contracts, Auction.
- Games is one paged list (Stories and Lost Current are entries); never
  reintroduce Arcade/Stories/RPG submenus or game-count subtitles.
- Help is a topic list on Main covering every system (including
  CONTRACTS & AUCTIONS and CREATURES); update it when a mechanic changes.

### Fish economy and genetics

- Varieties breed only with their own variety, on every path. The Fish Market
  sells the varieties you have unlocked, cheapest tier first (Tetra is tier
  one). Sale value = stage x tier x care x genes; genes include vigor
  (heterozygous loci), visible traits, mutations and rare marks. Pedigree
  certificates (generation and prized lines) add a small bonus. One variety
  per day is in demand (+`PROGRESSION_DEMAND_PCT`%).
- Favorite decor present or missing changes calm recovery and growth.
  Permanent diets (Pellets, Flakes, Bloodworms) speed growth and have distinct
  live particles.
- Collections: finding every mutation of a variety awards a gold statue that
  can be displayed in the tank (`tank_story_t` collection/statue bits).

### Contracts and auctions (`common/progression.c` trade block)

- Contracts: one order per `PROGRESSION_CONTRACT_DAYS` (3) day window, chosen
  deterministically from the window and six named clients. An order asks for a
  variety plus a need (fine grade, rare grade or any mutation, bred adult).
  Filling it pays quote x `PROGRESSION_CONTRACT_PCT`% + `PROGRESSION_CONTRACT_BONUS`.
  The filled mark is `progression.reserved` = window % 255 + 1.
- Auction: Saturdays (`care.day % 7 == PROGRESSION_AUCTION_WEEKDAY`), one
  eligible prized fish per week. Five rival bids rise from 80% of the quote to
  a hammer of 95..165%, deterministic from (week, life id), consecutive
  bidders differ; reserve is 95%. The weekly mark is `progression.pad` =
  week % 65535 + 1. Both marks are profile-wide (copied across aquarium
  switches) and serialized as optional JSON keys `contract_done`/`auction_done`.
- Trades require Normal/Hard (not Creative) and `progression.version == 1`.
  Every sale goes through `sell_fish_at`, which saves transactionally and
  shows the pedigree certificate on its confirmation page.
- UI sections: CONTRACT 37, AUCTION 38, TRADE_CONFIRM 39, AUCTION_RESULT 40.

### Creatures (one Creature Market for all of them)

- Shrimp school, snail, urchin, seahorse and pearl mussel are all bought and
  sold in Market > Creature Market (`market_creature` cycles all
  `CREATURE_COUNT` kinds). The Decor shop lists only decor and foliage;
  creature catalog IDs (1, 5, 6, 7, 42) stay reserved and still render in the
  tank.
- Creatures do not breed. One of each per aquarium (the shrimp as one
  school). Legacy pairs/nurseries were retired once with a refund.
- Colors roll at purchase: NATURAL 45%, TINTED 30% (x1.4 resale), TWO-TONE 20%
  (x2), PRIZED 5% (x3.5). Resale = price / 2 x rarity, so rerolling is a
  losing average with lottery-like upside. Selling clears that tank's colors.
- Roles: shrimp forage fallen food; the snail keeps glass clear (calm
  recovers faster); the urchin grazes at night (resting fish settle faster);
  the seahorse perches on kelp and doubles trust from holds; the pearl mussel
  earns 1 doubloon per `MUSSEL_SECONDS` while fish are healthy, up to
  `MUSSEL_DAILY_LIMIT` per day, paid automatically. Mussel ownership is the
  per-aquarium `mussel.owned` bitmask (version 2); selling clears only the
  active aquarium's bit and the paid/owned profile bits once none remain.

### Casino

- Casino games: Fish Races, Reef Reels, Pearl Hi-Lo, Shell Game, Treasure
  Dive and Tide Wheel. Bets are 5/10/25/50 doubloons, settled once per play via
  `progression_wallet_settle` (transactional; failure shows SAVE FAILED).
  Keep every game's return below 100% and betting off in Creative.

### Technical guardrails for this direction

- Layout: page space is 448 x ~362. At 240 px, scale-1 lines must stay within
  about 33 characters and Main/hub hints within about 14;
  `tools/test_page_art.c` asserts against clipped text at both scales.
- Tests to extend when touching these systems: `tools/test_trade.c`
  (`test_reef_progression.py --trade-only`), the creature part of
  `tools/test_fish_market.c`, `tools/test_page_art.c` (trade and
  creature-market captures), `tools/test_companion_feedback.c`,
  `sim/test_market.py`, `tools/test_browser.cjs` and
  `tools/capture_cyd_gallery.py`. The fish-price part of `test_fish_market.c`
  predates the rebalance and is not in the matrix.
### Handoff baseline

- Current verified COM4 2.4.0 app (Creature Market, contracts, auction, six
  casino games) is 1,795,840 bytes (0x1b6700, 9% app partition free), SHA256
  `4ac91891c508aea2b2c5b70106f767634b716a6a9dbd990dcb6323ab2c0d9d92`; app-only
  ota_0 write with full backup and exact readback. Live tank ~22.2 fps; fixtures
  1/2/3 at 22.41/16.41/19.44 fps, matching the previous image.
- Previous identity/serenity app was 1,759,520 bytes, SHA256
  `c339283c2b1e30e77703963e39a9b55b32ebed0681efab74f4cdab20b8986aed`.
  Full recovery backup, app-only ota_0 write, exact readback, all sixteen
  fresh hardware fixtures, reboot/Bank verification and two-minute soak pass.
  Normal adaptive day/night/clock-decor/dense FPS:
  22.85/22.55/16.12/19.57, within -0.37% of the preceding image at worst.
  See firmware/CYD.md for all paths, stress evidence and explicit save/console
  stalls; no commit or release publication was performed for this revision.
- Casino revision (supersedes the menu layout below): Main is Fish, Tank,
  Market, Games, Casino, Help, and Settings is the cog at the bottom right
  (400,334). Main > Fish opens the Fish hub (Summary/Rename/Family/Genes/Life/
  Wardrobe, `<`/`>` and Fish Bank); Fish Summary has no action grid, only five
  meters (Vigor added) and info lines. Casino bets go through `wallet_settle`
  once per play; keep RTP below 100% and betting off in Creative. Collection
  bits/statue live in `tank_story_t` (JSON `collection`/`statue`), with no
  schema bump. At 240 px, keep scale-1 lines within ~33 characters and avoid
  multi-line `draw_text_wrap` paragraphs (16 px pitch overlaps).
- The economy/help revision: Main menu is Fish, Tank, Market, Games, Help,
  Settings. Main > Fish opens Fish Summary; its six actions are the only fish
  hub. Fish Bank and Rename (full-page QWERTY) must return to the Summary,
  not Main or the aquarium. Games is one paged list with Stories and Lost
  Current as entries; never reintroduce Arcade/Stories/RPG submenus. Help
  replaces Care Guide. Sale value = stage x tier x care x genes, where genes
  include vigor (heterozygous loci), shown traits, mutations and rare marks;
  purchase must exceed immediate resale. Favorite decor present/missing changes
  calm recovery and growth; do not inject random stress. Tetra is market tier
  one; Harborfin keeps FISH_CLASSIC's ID. Themes cost doubloons (tap twice).
  13 subtitles / 12 emblems cover eight aquariums uniquely. Jelly tentacle
  geometry must match fish_projected_extent; shrimp schools draw half.
  Summary text must fit at 240 px (test_page_art clipping check).
- The identity/serenity revision removes the seven decorative footer pips and
  packs Tank actions contiguously (Summary, Decor, Food, Themes, Sand & Water).
  Tank Summary has one Name & Identity entry; rename returns there on Save or
  Cancel, and Menu must reach Main in both firmware and simulator.
  Fish first-run/newborn/rename and tank names share the QWERTY draw/hit helper
  in setup.c, never the historical wheel IDs. Preserve valid existing name
  characters and transactional failure behavior. Test real key coordinates.
  Season hemisphere comes from saved weather-city latitude, even with weather
  disabled; without a city show no season. Preserve legacy SOUTH data but never
  let it override the city. Only the optional accent Auto/Off control remains.
  FISH_CLASSIC keeps its frozen numeric ID but is displayed/rendered as the
  original Harborfin, including forked tail and ribbed sail; clearance, live
  art, previews, market and existing residents must agree without changing
  saved genetics/colors. Rainbow flake colors derive from stable particle
  indices, not moving coordinates; samples use the same material.
  Need gauges have illustrated icons, decimal values and ten shaded segments:
  test ten separate pixel runs at both scales, not just virtual-coordinate
  gaps. Fullness/Calm invert hunger/stress. Calm10 is normal without recent
  startle/chasing; do not inject random stress to animate a meter.
  Reef/shell/pearl/star emblems reuse detailed actual ornament materials.
  Quiet background contrast/less luminous noise must not erase owned decor.
  Sword folds/ribs/veins share one at-most-21px row occlusion mask; preserve
  exact material strokes and verify against scalar piece/arch visibility.
  Do not repeat full piece-mask work for every single-pixel vein stroke.
- The next trading-focused iteration supersedes the earlier 2.4.0 UI/art
  acceptance below. The user found the clock/live creature art insufficient,
  markets crowded, menus fragmented and retired tool controls still visible.
  Do not treat replaced icon PNGs as evidence that live procedural creatures,
  theme scenery, bubbles, sand or clocks have visibly changed. Capture and
  inspect those actual render paths at both scales.
- Use one active currency, doubloons. XP and levels are historical save data,
  not active gates, rewards or a second displayed economy. Preserve frozen
  offsets and existing wallets/stock. Fish varieties may breed only with the
  same variety on every path. Fish and creature base colors must come from
  purchased stock, inheritance or mutation, never free editors, even Creative.
  Permanent Pellets/Flakes/Bloodworms diets must have distinct live art and measurable growth
  multipliers with transactional persistence across aquariums.
- Consolidate Fish/Tank/Market/Games. Tapping fish opens the useful fish
  summary directly, not the old stats/tool/lock overlay. One Back closes a
  direct fish summary to the aquarium; Menu opens Main from every companion
  submenu. Child Back preserves its actual parent. Stats belong inside
  Fish; no sponge/scissors padlock may remain drawn or interactive. Stories
  and Lost Current belong under Games; tank identity/rename under Tank
  Summary. Retire active firsts/trophies/object stories/frames/discoveries
  rather than retaining empty navigation. No game-count tile subtitle.
  Decoration Shop must not offer fish fry. Markets need separate roomy stock,
  residents/sales and pair/nursery views with explicit confirmation.
- Every placed decoration, including sea glass, fixed scenery and extras,
  needs an accessible removal path. Placement arrows must cycle actual
  placed items/copies, not unlocked-but-absent stock. Remove the mandatory
  starter stone arch: a bare tank may breed over open sand without a hidden
  structure or phantom shelter bonus. Unanchored seahorses cruise upright in
  open water and hover briefly, rather than sinking to a sand perch.
- Bubbler, Reef Rhythm/current gestures and Say Hello are retired, including
  setup, visit goals, rewards, tips and navigation. Keep legacy binary/JSON
  fields and enum IDs intact; compatibility APIs must be no-ops without
  rewards or RNG changes. Ambient/courtship bubbles may remain visual.
- Every menu and submenu, including setup, weather and network/update pages,
  must use the established crafted materials rather than flat placeholder
  panels. Reuse shared framed controls; inspect actual captures at both scales.
  Use the gold doubloon icon in purses. Name food Pellets/Flakes/Bloodworms
  with distinct actual particles and samples, not generic tier labels.
- Foliage means visual ground cover like the old seagrass footprint, not
  a separate shop/category. Decor & Storage sells aquarium-local floor sets:
  Anubias Carpet 60, Moss Cushions 85, Pearlweed 110 and Kelp 95 doubloons.
  Owned sets can be planted/stored for free. The first three mix across
  six static patches; kelp adds two 80px stands, about half sword height.
  Existing saves retain free Anubias; new aquariums start bare. No growth,
  trimming, maintenance or phantom shelter bonus. PNG source art and the
  indexed live header come from tools/draw_ground_cover.py; previews must
  use the same art. Include planted bits in compressed/baked/damage cache
  signatures and test every mixture at both sizes, including row/column clips.
  Bake kelp's fixed lower stems into the floor cache; only the top 21px
  crowns draw live, with six-step 2Hz sway (at most 2px; fully baked in Simple).
  Damage only the moving crowns on phase changes; never invalidate static
  scene caches every animation phase or redraw/resample the whole 80px plant.
  Keep the native-size clipped fast path and shared 16-color dimmed palette.
  Seahorses can genuinely perch on kelp's fixed stems: keep grip and art aligned,
  release on Store, and preserve upright open-water swimming without perches.
  Sword art and grip width must share sword_half; its preview uses the same
  folded emerald/teal direction, not the retired thin lime silhouette.
  Keep legacy SD_ITEM_COUNT and 64-bit profile ownership frozen; foliage uses
  appended catalog IDs 63..66 and its own save tail, never legacy bit shifts.
- The working 2.4.0 overhaul supersedes the older gameplay/routing contracts
  below; 2.3.20 remains the published baseline until publication is verified.
  The current user authorized autonomous mechanic design and iteration, not
  approval prompts for each concept. Build/COM4 testing remains subject to the
  backup and app-only safety rules; publication is a separate authorization.
- Achievement retirement includes runtime detectors, meal/hold bits, sleep,
  growth and population firsts, stage/trust bonus payouts, and queued modals,
  not just menus. Preserve historical save bits/ledgers without new grants.
  Keep care/storage warnings, release updates, the light-control tip and real
  fish growth, breeding and trade income. Test actual eating, every legacy
  bit, stage changes, blocked queues, no-op retired modal rendering and reload.
- Kelp Forest theme leaves must be positioned relative to each stalk's tip,
  not a global row phase clipped at a per-stalk height. Use tapered tips and
  complete leaf silhouettes at every authored height; verify both scales.
- Seagrass, algae and cleaning tools remain retired. Beds 0..2 have no
  simulation or rendered geometry; sword bed 3 has static authored leaves.
  Snail and urchin are ornamental wanderers; shrimp never refuse food because
  of algae. Seahorse blade anchors use sword bed 3, never retired grass;
  kelp perches use their separate planted-set anchor kind.
  Preserve old save fields, catalog IDs and historic chore counters; never
  renumber or silently reuse them.
- Shelter comes only from sufficiently wide, actually placed nonsurface
  decorations, including extra copies. Swipes never create current bonuses.
  Quiet Company uses quiet viewing only, not historical current counters.
  There are no
  neglect penalties or offline environmental debt. Care gestures must not
  depend on the reserved historical tool-lock flag.
- Fish Summary replaces both the old Names & Looks hub and tapped-fish overlay.
  Keep all seven clock faces and their saved preferences. Legacy toolbox
  hitboxes and free base-color editing are retired in every mode.
- The user's follow-up makes fish commerce/breeding the core loop. Normal/Hard
  species, colors and patterns come from bought stock and inheritance, not
  free identity editing; names and optional wearables remain. Creative does
  not restore free fish/creature color editing. Existing residents and historical unlock records
  must survive. Fish type/quality affects sale value, but fresh purchases
  must not yield instant resale profit. Buying and confirmed selling must
  save transactionally, including wallet, roster, identities and family/bank
  references. Clear capacity, breeding-pair and failure feedback is required.
- (Superseded: creatures no longer breed; see "Creatures" above.) Historically, creature pairs also bred for trade. Existing single residents must not
  silently become pairs. Keep adult representatives and nursery stock bounded,
  active-time breeding paused in inactive aquariums, and pair visuals/hit targets
  consistent. Legacy creature ownership, paid refunds, shop actions and market
  stock must remain synchronized: free replacements cannot become sale income.
  Additional stock must not multiply mussel income or resurrect grazing chores.
  Creature palettes currently use one existing saved slot per species per
  aquarium: paid initial stock assigns a bounded palette, and a rare birth
  mutation changes the whole species palette, including adults. Do not claim
  individual creature color inheritance. Shrimp/mussel have paid palettes,
  not individual offspring genes. Free palette saves reject in every mode.
- Current 2.4.0 saves are schema 23 / 6476 bytes. The entire historical
  6424-byte prefix is frozen; Reef Rhythm occupies 6424..6443 and creature
  stock 6444..6463, with eight progression bytes at 6464 and four aquarium-local
  foliage bytes at 6472. Complete schema-22 prefixes migrate; partial new tails
  and invalid ownership/planted masks must be rejected.
  Initialize the appended progression state even on nonzero-filled tank memory.
  Active persistence ports must report the verified stored payload length via
  `aquarium_store_read_sized`, not the caller's larger capacity: zero-padding
  a historical snapshot is not evidence that its new schema tail exists.
  Downgraded synthetic JSON fixtures must remove newer `reef`, `creatures`,
  `progression` and `foliage` sections; never relax strict production validation.
  Retired shop IDs stay reserved. Paid filter/garden
  refunds run once, after shared-wallet copying on inactive-tank migration.
  Hard online starvation is unchanged by the environmental overhaul.
### Earlier local trading revision

- Final COM4 app is 1,748,352 bytes, 217,728 OTA-slot bytes free, SHA256
  `88f0c1a3302737c991984e2bfe1d6b167847127472f99f1aab1fb4bc20775c4c`.
  Full recovery backup and exact app-only ota_0 readback passed. The first
  image safely blocked writes on a historical-profile length mismatch;
  verified sized reads fixed it. Original SPIFFS was byte-identical before
  recovery. Residents bolt/pip/kelp/odie and fry mira, wallet and clock settings
  loaded; Bank JSON, HTTPS weather and saves passed. No erase/repartition.
- All 27 source selectors passed, with final native backup and exact bounded
  clocks. Renderer stress: 180 seconds, 16708 cases / 116956 full/tile/retained
  frame sets. Final persistence changes also passed focused profile/storage,
  complete simulator smoke and real Edge acceptance. Gallery: 192 per scale.
- All 16 device benchmark fixtures passed. Against the previous local 2.4.0
  image, default-mode FPS changed 23.90/23.16/18.72/20.58 to
  23.05/22.56/17.44/20.09 (day/night/clock-decor/dense). The richer art costs
  2.4-6.8% in that mode, not a speed gain. Final real-tank save took 190 ms.
  Keep this local identity separate from published 2.3.20.

### Historical deployment evidence (not latest-revision acceptance)

- Earlier 2.4.0 source acceptance passed the 27-selector matrix, native economy and
  both-scale Normal/Hard/Creative touch-market checks, and rebuilt real Edge
  acceptance. The 180-second embedded renderer soak covered 14655 cases /
  102585 exact full/tile/retained frame sets. The actual gallery has 205
  captures per scale, with market text/route assertions. All 42 active
  upstream icon PNGs were replaced; five retired originals were removed.
  Screenshots and reproducible evidence are in the feature/hardware guides.
- COM4's latest app-only verified local 2.4.0 build is 1746464 bytes,
  219616 bytes free in its existing OTA slot, SHA256
  `a0bfd802b79151cc70732d5f0a9f1a99a5dfd83939bd4beab014d76e96c20786`.
  Full recovery backup preceded the write; exact readback passed on ota_0.
  All 16 final before/after benchmark fixtures completed. Default mode 3
  improved 22.28/21.72/17.56/19.13 to 23.90/23.18/18.73/20.59 FPS,
  roughly 6.7-7.6%, not a dramatic multiplier. Tick work fell from about
  2.5 to 1.8 ms, but rendering/LCD work remains. Keep this local identity
  separate from published 2.3.20; the old COM4 evidence below is historical.
  Final save/reboot/Bank JSON and the original bolt/pip/kelp/odie roster
  passed. Clock preferences and wallet 7753 survived. HTTPS weather passed
  with 30856 internal bytes free / 29696 largest at request start, then the
  radio shut down. The two-minute quiet soak had a 184-ms service stall;
  JSON verification produced a separate 764-ms diagnostic stall. Do not
  claim hitch-free output or confuse these with steady fixture FPS.
- Release 2.3.20 is published at tag/source
  `7b5411019ac89cb7ad6e3a75fa223459e366c81e`. Main acceptance
  (40806623944) and all-board release/installer gates (40808164983) passed.
  All 29 public assets were downloaded; checksums, ZIP members, PTUs,
  OTA metadata and embedded board/version/DIO-80-MHz markers passed.
  Published unified app: 1,777,456 bytes / 188,624 slot bytes free, SHA256
  `d3b3a0af2b3569e62b8e48757deee1e23555aaab2222672f1d87f6f00d1a54a1`.
  Keep this CI artifact identity separate from COM4's local tested build below.
  Release-pinned Pages run 40809467894 passed. Live home, all 33 images,
  installer/viewer/play, all board manifests/app hashes and browser
  release/source identity passed after deployment.
- COM4 now runs the local 2.3.20 unified image, superseding the older
  hardware baselines below: 1,777,472 bytes / 188,608 slot bytes free,
  SHA256 `b852aa70f334f93a57c1d4bd72e564f54b8d57d6ade584e73e68231e89d14724`.
  A new full recovery dump, active-slot app-only flash and exact readback
  passed. All 16 before/after fixtures completed; mode-3 FPS improved
  21.87/21.20/17.31/18.65 to 22.28/21.72/17.55/19.14. These fixtures
  have no pumpkin: do not attribute all gains to its removal or substitute
  native pumpkin CPU overhead for device FPS. Save/Bank JSON and original
  residents passed. A post-save reboot preserved clock/ownership/care settings,
  wallet difference equalled earned income, and HTTPS weather succeeded before
  radio shutdown. Quiet live soak still had a 257-ms service stall; do not
  claim hitch-free output. Publication identity is recorded separately.
- The 2.3.20 work retires pumpkin ID 8, not the catalog index itself. Keep all
  63 stable IDs and the legacy slot/position fields reserved: 62 items remain
  offered. Never reuse that bit or collapse the inventory slot mapping.
  `tank_decor_placeable(8)` remains true only for historical save validation;
  availability, ownership, movement and inventory actions reject it.
  `retire_pumpkin` clears placed/stored copies and paid provenance on load,
  returning the normal sell value only for paid copies outside Creative.
  Inactive tanks must retire **after** `share_progress`, so shared-wallet
  copying cannot overwrite refunds. Failed persistence must retain retry
  semantics, and revisiting a saved tank must not refund twice. Keep binary
  schema 20 / 6424 bytes unchanged. Regression coverage includes failed
  aquarium switches, archive visits, paid/free storage and binary/JSON reloads.
- 2.3.20 Home routing: first tile opens Names & Looks; footer left opens the
  unchanged Fish category as Life & Care, footer right opens Symbol Guide.
  Update synthetic capture/test taps when routing changes; a nonempty frame
  is not evidence that the intended page was captured. Clock screenshots
  must use the Clock/Weather SHOW CLOCK tile, not obsolete footer coordinates.
  Preserve all seven faces, saved settings and live/tile/cache pixel parity.
  Fish-card labels must retain existing tool/next/lock hitboxes and input
  behavior. Sponge/scissors are intentionally retained. The Symbol Guide
  explains actual needs, traits and tools without adding progression rules.
- Release 2.3.19 is published at tag/source
  `2141e67fd7ddf8b5720b76e0bd15c30e30102f32`. It collects the motion,
  placement, illustrated art, save-memory and 80 MHz follow-ups below, with
  updated docs/website and 194 renderer captures. Main checks (40799910228),
  release (40801238113) and release-pinned Pages (40802367853) passed.
  All 29 public assets were downloaded: checksums, ZIP contents, PTUs, OTA
  metadata and board/version/80 MHz markers passed. Unified app is 1,785,968
  bytes / 180,112 slot bytes free, SHA256
  `4bb3758636cc48ccb36c3859e485fdf634e9782a3c7d27addb6e9f0586091aec`.
  Live Pages home/flasher/viewer/play, all 33 home images, board manifests,
  app hashes and browser release/commit passed. Recheck before future claims.
  COM4 still runs the measured pre-release image below; publication did not
  flash or reset it. ELEGOO at 80 MHz remains unbenchmarked on hardware.
- COM4's current local image supersedes the release acceptance image below:
  the post-2.3.18 edge-steering, fluid-motion/save-pacing and decoration
  edit-mode/mirror/save-speed, menu-performance, minigame-art, menu-art,
  movable-scenery, save-memory, RTC-scratch/packed-pieces, float-depth,
  internal-RAM-trim and 80 MHz flash follow-ups, unified build, app 1,785,968
  bytes, slot headroom 180,112, SHA256
  `b23e72b4d083d275705432cc218b2217133ba46801cbc470ba75f7bd5d038d05`
  (see [menu art and movable scenery](firmware/CYD.md#menu-art-and-movable-scenery)).
  Fresh full recovery backup, app readback/profile and saves passed. Residents/settings remain intact; the wallet
  changes only from normal passive income. Tracing is off. These follow-ups
  are collected in 2.3.19; the immutable 2.3.18 tag remains unchanged.
- The verified 2.3.18 source is `92a2a41d3ee9edbe662d62c3c24c63fe2852a608`.
  At release acceptance COM4 ran that clean unified build with the saved `cyd2432s028` profile:
  app1,769,568 bytes, slot headroom196,512, static DRAM79,731 bytes, SHA256
  `14a55ed66359dca5bdc016b82e85c4287ae8217171aa46f0af6268184d8cc3bb`.
  A fresh full recovery dump, app-only flash/full readback, verified HTTPS,
  all16 controlled fixtures, live soak and explicit save/reboot/reload passed;
  original residents/wallet/settings remain intact and tracing is off.
  Linux/native/browser CI passed on that source; all three board bundles and
  actual installer/migration handlers passed locally. Recheck release/tag/Pages
  status before claiming publication. Default adaptive fixtures improved only
  1.7-2.0% over the already-optimized prior hardware image; clock p95 fell
  76->72ms. Live quiet windows still had a331ms maximum: do not promise
  hitch-free animation. A natural daily backup verified successfully, but its
  104ms snapshot and199ms completion are separate remaining rare service stalls.
  Do not prioritize another daily-backup rewrite over regular rendering.
  See [final COM4 acceptance](firmware/CYD.md#final-2318-com4-acceptance).
- Release 2.3.17 contains the rendering/weather/backup follow-up. The 2.3.15/16
  attempts failed before publication on strict-C POSIX and lazy image-decode
  test gates; preserve their tags. Require the Linux native backup harness
  to pass; website tests must serve PNG/SVG with proper MIME types and wait
  for lazy images to finish loading before requiring actual decode.
  Saves remain
  schema19 / 6336 bytes and companion v7. Weather acquisition is boot + hourly,
  including failures, with bounded offline attempts. Do not restore a 15-minute
  poll or keep the radio/caches competing while idle. See the measured
  [four-part follow-up](firmware/CYD.md#com4-verification-of-the-four-part-follow-up);
  regular FPS gains are modest/mixed, and backup snapshot validation still hitches.
  Published tag commit is `bf11f3b08fe2c340306a589f238c0f96fe124859`;
  release/main/tag checks and release-pinned Pages passed. Published unified
  app is1,765,776 bytes with200,304 bytes per OTA slot. At publication, COM4 ran a clean local
  build of that tag, with full recovery backup/readback and residents/saves
  verified. Local/CI binary hashes differ with build metadata: consult
  firmware/CYD.md and recheck actual GitHub/Pages status before future claims.
- Before the 2.3.18 gameplay polish, the optimization follow-up was flashed on COM4, superseding that clean-tag
  hardware baseline: local2.3.17/44dee70-dirty, app SHA256
  `97aaf933e5b6dd806f6206ca9eac685d39f81538815c2cd5f6c36442b05707e2`.
  Full backup/app readback, verified HTTPS, all16 fixture comparisons, three
  SD worker diagnostics and original residents/wallet/settings/saves passed.
  Adaptive clock/decor fixture13.88->15.43FPS, p9584->76ms, >80ms31->0/240;
  other fixtures improve only1.0-1.9%. Live windows are not controlled A/B.
  Actual backup pause remains about105ms (108.9ms before), not the larger
  native-only validator speedup. Frame metadata is optional leased RAM; the
  first permanent-key/extra-histogram trial broke HTTPS PSA memory allocation.
  That prior app was1,767,264 bytes, slot headroom198,816, static DRAM79,731 bytes.
  See [measurements and limitations](firmware/CYD.md#com4-beforeafter-measurements).
  Those are pre-polish measurements, not the final 2.3.18 image identity.
- Release 2.3.14 content expansion: exactly six new wearables (IDs7–12) and twenty
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

## Art direction and optimization priorities

- Preserve the detailed, cohesive illustrated aquarium: readable silhouettes,
  structural shading, material-specific texture, fittings and highlights.
  New decorations must match the detailed harbor/garden set, not regress to
  simplistic flat shapes. Keep holes transparent and previews consistent with
  placed art; inspect actual-renderer captures at both scales.
- Minigames share the `ga_*` art kit in `common/games.c` (water backdrop,
  bevelled blocks/sockets, pearls, rocks, coral, crabs, sub, pipes, shells,
  lanterns, chests, `ga_hud` text plates). Game pages redraw every frame, so
  props must stay cheap: use the row-deduped `render_internal_ellipse`,
  `annulus`, `rect_blend` and `poly` bridge primitives, never per-pixel rings
  or full-screen blends beyond the backdrop. Help/pause keep the plain ink
  fill that hides the playfield. Keep hit boxes and art aligned; check
  `game-play-N` gallery captures at 240 and 320 and device games FPS
  (`render` must stay well under the ~33 ms frame). Prefer opaque fills
  pre-mixed against the known water band (`ga_wash`, the stepped shafts)
  over translucent blends; opaque page spans use `fill565_span`.
- Menu pages share the same look (`common/render.c`): `page_backdrop` (a
  water gradient with stepped light rays and a sandy foot, opaque spans only,
  tile-culled), the bevelled round-cornered `button()` key (edge rim, lit lip,
  darker foot) and, in the companion, `key_face`. Shop rows sit on raised
  `shop_row_card` slates with the preview in a sunk `SHP_WELL`; row previews
  are full colour whether owned or not (the modal still greys locked items).
  Keep new pages on these helpers, not flat ink fills or flat rectangles, and
  keep them opaque so still-page skipping and tile culling stay cheap.
- Fit every wearable to each species and life stage, including turns and dive
  poses. When a fit genuinely cannot work, exclude it consistently from purchase,
  equip and rendering rather than showing broken gear. Jellyfish exclude only
  the wraparound scarf; ties, bowties and bandanas remain supported.
- Optimize for visibly smooth fish motion and fewer long frames, not a higher
  menu FPS or smaller file alone. Preserve fish cadence, artwork, care behavior,
  saves and touch targets. Prefer exact reuse, clipping and bounded caches over
  reduced detail, new animation banks, faster SPI or additional framebuffers.
- Measure controlled populated hardware fixtures and p95/p99/max frame times;
  compare total frame cost, not one faster stage. Keep unsuccessful prototypes
  out of the hot path. Document remaining hitches and negative/mixed results.
  Detailed reference captures and exact pixel/hash tests are acceptance checks,
  not substitutes for device pacing and memory measurements.
- Keep behavior polish on the existing bold/lazy traits and steering: rest
  arrival eases toward a trait-scaled hover; laziness moderates food pursuit,
  close food dives steer more directly, and bold fish startle less from taps.
  Edge avoidance must fade in continuously over its first4 logical pixels,
  retaining the full0.62 blend beyond that band. Do not normalize a tiny edge
  penetration into full-strength steering: it causes frame-to-frame nose
  seesawing. Keep final food approaches exempt and hard clearance bounds intact.
  Do not add a new personality record, save tail or cosmetic animation bank.
- Fish motion must stay continuous. `fish_t.swim_phase`, `turn_vel` and
  `speed_vel` are runtime-only, never serialized, and zeroed/reseeded on
  spawn, facing commits and sleep/wake. Mirror reflections negate `turn_vel`.
  The tail beat advances `swim_phase` by `dt*(8 + speed*0.055)`; never derive
  it from wall-clock time, or frame hitches and speed changes jump the tail.
  Turns accelerate at 9x (urgent 24x) `turn_rate` and brake to land exactly
  on the target; speed follows an implicit critically-damped spring (omega
  5.5, urgent 10) that never overshoots or goes negative. `test_fluid_motion`
  requires turn jerk <0.7 and speed jerk <0.35; the old bang-bang/lerp logic fails.
  Measured on COM4, this is FPS-neutral; keep any change to it FPS-neutral too.
- Decoration placement: layer steps/text consider only overlapping pieces;
  mirror is rank bit 7 (`DECOR_RANK_MIRROR`, sprite items only, no save-layout
  change) and must be masked wherever ranks are compared. The placed piece is
  drawn in painter order (`pc_scene_live`); never overdraw it after the scene.
  The place page uses adaptive damage (its animated band), not forced full
  frames; page primitives must cull to the tile rows. It runs ~8.7 FPS vs
  13-16 live because no clock face occludes damage; measure before claiming more.
  The place page has no on-screen CANCEL (DONE only; serial `setup off`
  reverts); MIRROR replaces the DEPTH caption, centred above the depth bar
  (`SETUP_MIRROR_X/Y`). Text over the live tank uses `text_plate` (white on a
  dark plate); there is no copy counter, tap-to-select or drag hint.
- Every tank piece the shop sells is movable (`tank_decor_movable`):
  placeable ornaments/plants plus the 14 "fixed" pieces (scenery 36-41 and
  items 22-29). Fixed pieces stay non-placeable for inventory, copies and
  pricing (no REMOVE, never stored); they use decor slots 36-49
  (`DECOR_FIXED_SLOT0`, `tank_fixed_index`: 36..41 then 22..29) with x/z in
  `tank_t.fixed` (`x == 0` = the historical default spot). Floats 24/28/29
  (`tank_decor_surface`) ride the surface: no depth bar or order arrows, only
  drag and MIRROR; their z is stored BACK but reads FRONT, so the placement
  snapshot keeps the raw stored byte. Buying any movable piece opens its
  placement page (`main.c`/`sim/main.c`); the owned shop modal shows MOVE +
  SELL/REMOVE. `test_setup_placement` buys, drags, mirrors, saves and reloads
  all 50 movable pieces at both scales; keep it passing when adding items.
- Still pages: `main.c` skips render+flush while a static full page
  (milestones/life/bank/journal, settings, shop/aquariums) is unchanged. It
  redraws on page change, while touched and 400 ms after, and every 500 ms. If
  a page gains animation, or state that changes without touch or a serial
  command, exclude it from the skip (as games, updates and
  `render_settings_animated()` are), or it will look frozen. Page
  primitives must stay tile-culled (`page_clamp_rows`).
  `tools/test_fish_behavior.py --cc gcc` checks continuity, all ten varieties,
  tank edges, reactions and frame-step stability. Intentional behavior changes
  alter trajectories: compare old/new renderer pixels using the same updated
  simulation, not old trajectory hashes as an artwork acceptance test.
- Care feedback stays read-only and grounded in actual rules. Fish Interaction
  uses10/25/50 bond thresholds, ceil(+5 greeting) counts and the30-second cooldown;
  Creative bypasses bond locks but Follow still needs a tankmate. Life uses
  ceil-rounded growth-hours,25% growth during sleep and no elder next stage.
  Keep feedback on existing pages, not another chore/checklist system.
  `tools/test_companion_feedback.py --cc gcc` checks semantic thresholds,
  cooldowns, stages and both-scale read-only layout; retain the page-art audit.

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
| Games | `common/games.c`: eighteen games, pages, help/pause/results, reward dispatch and the shared `ga_*` minigame art kit; `render_internal.h` is the narrow private page-drawing bridge |
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

- Current source binary save **6424 bytes**, JSON **schema 20**; imports schemas 1-19.
  Schema 20 appends the 88-byte `decor_fixed_t` (`fixed`: version, x, z and
  ranks of the 14 movable fixed pieces) at 6336; 6336-byte saves load with
  default spots. JSON exports a `"fixed"` object after `"content"`.
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
  Synthetic fish fixtures must call `tank_make_fish` (or populate every required
  trait/runtime field) before overriding geometry. `tank_init` does not
  initialize `fish[]`; uninitialized lazy/turn/energy fields made sand-feeding
  checks platform-dependent when food steering began using laziness.
- RPG is a 24-byte `tank_rpg_t`; new-game records are a 40-byte `game_records_t`.
  RPG participant is uint32; chapter contributors are uint16 lineage IDs.
  IDs above 65535 must become unknown, never alias an unrelated ancestor.
- Older-schema JSON fixtures must remove later fields: `<11` polish,
  `<12` story, `<13` games, `<14` rpg, `<15` creative, `<16` profile/games_extra,
  `<17` care.
- Update every relevant import/export, active/Bank/away/tank-switch path,
  rollback snapshot, simulator fixture, viewer, and documentation together.
- Active-save writes are event-driven/coalesced (2 minutes; 10-minute heartbeat).
  Each SPIFFS save stalls the render loop for ~140-280 ms (one visible hitch),
  and routine buddy/care/journal changes dirty the save almost every 30 s, so
  do not shorten `SAVE_MIN_GAP_S`. Explicit keeper actions, sleep, restarts and
  updates still save at once. The port logs `persist: saved in N ms`.
  Do not add per-frame flash writes. Failed loads must not overwrite saves.
- Rendering uses internal RAM, cached/tiled backgrounds and DMA stripe buffers.
  Check `memory_plan.h`, heap and application-slot headroom after changes.
  Each OTA app slot is `0x1e0000` bytes; do not casually grow large assets.

### FPS, internal RAM, PSRAM, and storage budgets

- **There is no PSRAM on the supported board.** Required buffers, renderer
  scratch, masks, task stacks and DMA transfers must fit internal RAM.
  Do not "fix" an allocation failure by requiring PSRAM, selecting a PSRAM board,
  or enabling PSRAM-only code. SD storage cannot replace working RAM.
- The loop has a **33,334-us target interval (30 FPS ceiling)**, not a
  promise of 30 FPS in a moving aquarium. Absolute deadlines preserve the
  fractional phase without per-frame tick-rounding drift; late frames yield
  one tick and discard their deadline, never queue catch-up frames.
  The practical populated-tank goal is
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
- Clock pages index commands into40-row bands, retaining painter order and
  replaying cross-band commands only once. Alpha255 is the original255/256
  blend, not an opaque store. Preserve exact boundary correction tests.
  The selected static ornament uses a128-byte dimmed RGB565 palette and the
  existing span painter; do not reintroduce a full RGB565 sprite allocation
  that fails alongside clock caches or a slower duplicate pixel/mask loop.
  Castle/crown caches help only scenes containing those objects.
  CYD frame scopes prepare clock metadata once and freeze wall time across
  tiles: begin after simulation, end after the LCD drain, before any mutation
  of tank/device settings/resolution/cache ownership. Unscoped rendering must
  retain exact output. Opaque span paired stores must support both halfword
  alignments, lengths0-320 and padded strides; never weaken alpha blending.
  Native stress compares scoped full/tile/retained pixels and wall-time rollovers.
  Frame metadata must stay an optional leased cache, accounted and freed before
  HTTPS/SD, with a logged live-format fallback. A permanent key plus a second
  timing histogram caused real COM4 TLS PSA -141 (insufficient memory), despite
  about25KiB free before HTTPS. Keep completion diagnostics to mean/max counters;
  verify actual HTTPS after any internal-RAM budget change, not just heap totals.
- Daily backup capture/semantic validation stays on the tank task; immutable
  JSON ownership transfers to a CPU1 priority1 worker below LCD priority4.
  The worker must not access mutable tank/progression state. Keep its4-KiB
  stack,32-KiB JSON bound,512-byte I/O scratch, cooperative yields, exact
  readback, failure rollback and identity-safe success-only daily marker.
  Service completion unconditionally even when automatic backup is disabled.
  Keep optional caches released and exclude new weather/SD/network operations
  while busy; reject user network entry explicitly with a retry message.
  Defer console backup requests to shallow tank service: nested validation
  under director dispatch overflowed the12-KiB tank stack. CPU0 worker
  starvation stretched storage to16.6s; CPU1 measured about442ms.
  `sd auto-verify` tests the real worker without changing saves, counters or day
  markers and removes its diagnostic file. Run
  `python tools\test_cyd_sd_backup.py --cc gcc` for ownership/failure/admission
  regressions. Do not claim backups are hitch-free: capture/validation remains
  about105ms on the latest measured four-resident CYD (about109ms before).
  The working-tree follow-up optimizes canonical field/array traversal, not
  validation coverage. Unordered imports still need exhaustive duplicate
  checks; do not make a trusted-export exception that skips semantic/identity
  validation. Re-measure the on-device pause after validator changes.
  This is normally once per UTC day when enabled, after a90-second boot settle
  and only if that day's success marker is absent. Failed attempts retry after
  an hour; minute eligibility polls are not minute backups. Do not prioritize
  this rare hitch over rendering/LCD costs paid every moving frame.
- Never put a `save_t` on a stack or heap-copy it for save/load/backup/restore:
  `progression_save`, `load_save`, `progression_backup_json` and
  `progression_restore_json` share the static `s_save_scratch` (tank task only,
  never nested). It is `RTC_NOINIT_ATTR` (RTC slow memory, ~6.9 of 8 KB used):
  in DRAM .bss it cut the shop's heap from 29/10 KB to 23/3 KB, so buying
  (`progression_buy` mallocs a `tank_t`) failed with NOT ENOUGH MEMORY and the
  weather TLS handshake (needs ~24 KB contiguous) went offline. Every
  internal-RAM static costs heap one-for-one on the CYD (no PSRAM): check the
  map's largest `.bss` symbols and the boot `weather: https start ... largest`
  line after growing any array (`DECOR_SLOTS` sizes `piece_t`/`scene_sig_t`,
  which are packed to 16/8-bit fields for this reason). Likewise the net
  worker task exits when nothing is leased and the radio is down (its 12 KB
  stack returns; `begin()`/`net_port_weather()` recreate it), weather results
  and the city list are heap-allocated only while in use, and shop/inventory
  rollback snapshots of `tank_t` go to IRAM heap (`MALLOC_CAP_EXEC|32BIT`,
  falling back to malloc) through volatile 32-bit word copies only: IRAM
  faults on byte access, so never touch the snapshot as a struct. At 6424 bytes a stack copy overflowed the 12-KiB tank stack on
  the console `save`; a heap copy left restore's cJSON too little room. Restore
  validation holds one top-level JSON value at a time (`json_snapshot` records
  slices, `json_expand`/`json_collapse`/`json_read_lazy`); the whole tree peaked
  near 40 KB on the ESP32 against 39 KB free. `test_tank_backup` asserts the
  host peak stays under 32 KiB. See
  [save memory](firmware/CYD.md#save-and-restore-memory).
- Fish stay full-rate; scenery clocks are independent (Balanced effects5Hz/
  grass2Hz, Enhanced10Hz/4Hz, staggered grass beds). Damage planning compares
  merged rectangle costs, not the old fixed40% promotion threshold. Serial
  `benchmark` compares synchronous/full and pipeline full/partial/adaptive
  modes with identical40-row tiles. Frame pacing logs include2-ms upper-bound
  p50/p95/p99, exact maxima, slow counts, and main-stage maxima; worker timings
  overlap rendering and must not be added again to the main-task frame budget.
  LCD fill profiling now counts each flush once, including all-skipped tiles;
  older stripe-cumulative fill totals were inflated and are not an A/B baseline.
  Keep the native elapsed=fill+wait+send assertion. Render/LCD completion
  intervals are useful presentation diagnostics, not a panel-VSYNC timestamp.
- At boot, `tank task up`, and periodic `TAG`/`display` lines, inspect **free
  internal heap and largest internal block**, cache usage, and rows rendered/
  flushed. A healthy total heap does not prove a contiguous DMA allocation
  will fit. Check network startup, SD operations and both display modes too.
- Prefer preallocated scratch, bounded static/cached scenery and localized
  dirty regions. Avoid per-frame allocation, full-screen animated effects,
  unconditional full redraws and new continuous simulation. Keep old/new
  damage bounds and cached/live pixels equivalent.
- Flash runs at **80 MHz DIO** (`CONFIG_ESPTOOLPY_FLASHFREQ_80M` in
  `sdkconfig.defaults` and the identical `sdkconfig.cyd`; edit `build_*\sdkconfig`
  too, because defaults only seed new keys). The app reapplies its own header's
  clock at startup, so OTA carries it; the bootloader log still says 40MHz.
  All code executes from flash through a 32 KB cache: unrelated code growth can
  reshuffle layout and cost 10%+ FPS with identical host profiles (see
  [FPS regression](firmware/CYD.md#fps-regression-since-6510382-flash-cache-not-work)).
  Do not pin hot code with `IRAM_ATTR`: IRAM heap is internal heap, and the
  37.7 KB experiment broke Wi-Fi init. Compare device `benchmark` mode 3 instead.
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
python tools\test_fish_behavior.py --cc gcc
python tools\test_companion_feedback.py --cc gcc
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
  When the save schema grows, add it to the script's `restore()` downgrade list
  and pop the new top-level JSON key there, or staging fails with
  "Incompatible format". Creature-card taps retry because random story notices
  can take the first tap.
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
