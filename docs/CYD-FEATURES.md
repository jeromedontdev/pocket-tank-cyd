# Pocket Tank CYD: a more personal little ocean

**An illustrated tour of Pocket Tank CYD (release 1.3.0)**
Updated October 24, 2026

This fork keeps Pocket Tank's feeding, growth, personality, trust and
aquarium care, while adding more ways to decorate, collect, progress and
keep favorite fish. It is built specifically for the **ESP32-2432S028
Cheap Yellow Display**, rather than trying to fit every supported board.

The headline additions:

- **A 20-slot Fish Bank** with confirmed deposits, returns and swaps.
- **Purchasable aquariums:** 500 sand dollars each, up to eight named tanks.
- **Ten creature appearances**, nine markings and 24 colors for both body
  and markings.
- **Genetics and a family tree:** hidden markings, colors and recessive traits
  pass from parents to fry, with rare mutations.
- **Five rare visitors**, a visitor log and seasonal sand scenery.
- **Room-light dimming, a back LED glow,** daily SD backups and screenshots.
- **An Aquarium Journal** with 15 Keeper levels, objectives, an appearance
  album and unlockable backdrops.
- **Six mini-games** using your fish, with sand-dollar and Journal XP rewards.
- **A desk-companion hub:** optional Cozy Care, fish bonds, discoveries,
  collections and safe story adventures.
- **Game records and medals**, permanent challenges and relaxed play without
  countdowns in Bubble Pop or Feed Frenzy.
- **Optional real weather** beside the idle clock, with on-device city setup.
- **Twenty-five added decorations**, including three simultaneous surface floats.
- **Two upkeep and four fish-care upgrades**, purchased separately for each tank.
- **A price-sorted shop:** gray icons before purchase, full color afterward.
- **Readable menus**, reorganized Settings, SD backups and offline updates.
- **Seven optional clock themes**, with on-screen Wi-Fi setup and NTP time.

> **About the imagery:** every image below is a fresh capture from the
> desktop emulator's actual CYD C renderer, not a mockup or webcam photo.
> Captures use a separate staged demonstration tank with unlocked content;
> the real aquarium was not read or changed. Each 320 x 240 Normal framebuffer is
> enlarged exactly 3x using nearest-neighbor pixels, without smoothing,
> retouching or added interface elements. The emulator is built with the
> hardware's five-active-fish limit and `CYD_FLAT_EDGES=1` (no vignette)
> for these captures. SD operations,
> flashing and touch calibration still run on the physical CYD, not in
> the emulator.

## Get the CYD build

Download a complete bundle from
[GitHub Releases](https://github.com/jeromedontdev/pocket-tank-cyd/releases).
These are independently versioned CYD-only releases, not upstream multi-board releases.
Read the included `INSTALL.txt` and check `SHA256SUMS.txt`.

**New-board browser flashing:** the
[USB web installer](https://jeromedontdev.github.io/pocket-tank-cyd/) works in
desktop Chrome or Edge with a USB data cable. Choose **save-preserving USB update** for
an existing CYD installation: the updater validates the live board and active OTA
slot, then writes only the application. The **factory installer** remains separate and
overwrites saved tanks, Wi-Fi, calibration and collection identity.
Back up first; SD updates remain available.

- **Already using this CYD fork:** back up your active aquarium, power off
  before moving SD, copy `update.ptu` to `pocket-tank/update.ptu`, then install
  through Settings > Device > SD Updates. No SD artwork is required.
- **New board:** use the factory image at `0x0`; all artwork is embedded in firmware.
  **Factory flashing overwrites saved tanks, Wi-Fi credentials, calibration
  and collection identity. Never use it for a normal upgrade.**
- **Advanced USB upgrades:** the application-only image belongs in the detected
  active OTA slot, not blindly at a fixed address.

Packages are verified but unsigned; only install trusted files. See
[installation and release details](../firmware/CYD.md#downloadable-releases).

## Small behaviors, useful inspection

Calm fish get acquainted while swimming near one another. After about half a
minute together, a fish can favor a familiar swim buddy. Short paired swims keep
a steady trailing lane, then take a break; hunger, stress and rest still come
first. Familiar buddies now survive restarting, Bank moves and adventures.
Friendships use stable per-fish identities, not shared names or appearances. Favorite
decoration choices are stable for a fish's original identity, not changed by
renaming or recoloring it. They guide resting and inspecting the reef.
**Home > Fish > Care** names the current buddy and favorite spot.

Tap any helper creature for a card that retains its lifetime progress while
explaining food and activity. Shrimp show both their actual school population
and the representatives currently drawn. **COLORS** jumps straight to the
selected creature's free color preview; SAVE is still required.

| Snail and algae care | Representative shrimp school |
|---|---|
| ![Snail inspection with diet and color shortcut](media/cyd-features/snail-card.png) | ![Shrimp actual population and drawn representatives](media/cyd-features/shrimp-card.png) |

| Grass-trimming urchin | Trust-sharing seahorse |
|---|---|
| ![Urchin activity and food](media/cyd-features/urchin-card.png) | ![Seahorse progress and color shortcut](media/cyd-features/seahorse-card.png) |

Decoration placement is now a reversible **PREVIEW**. Drag, select depth,
change stacking or preview coral colors; **DONE** saves the layout, and
**CANCEL** restores it. A failed save keeps the preview open for retry.
Autosaves and JSON backups do not accidentally commit a draft. Cancelling
placement after a purchase keeps the owned item at its original default spot;
it does not undo the purchase.

The aquarium picker offers a **SUMMARY** for the active tank and a summary
before switching to any other tank: fish, away traveler, Bank, actual shrimp,
other helpers, stat upgrades, theme and last visit. Inactive tanks remain paused.
Last visit needs a synchronized clock; otherwise it is explicitly unknown.

![Active tank summary](media/cyd-features/aquarium-summary.png)

Deep Glow occasionally gains a distant silhouette, Moonlit a short shooting
star, and owned treasure chests/crystals a brief glint. Effects use 4 Hz pose
steps and bounded old/new dirty regions, never a new full-screen background
animation. The opaque clock still occludes aquarium drawing.

### Quiet depth (new in 1.2)

Open **Home > Fish > Care > LIFE / TANK**. The four tabs offer:

- **LIFE:** an individual biography, recorded arrival in powered tank hours,
  life firsts, current needs/activity, remembered buddy and favorite spot.
  Close companions have spent at least ten calm nearby minutes together.
  Older fish keep their existing milestones; their original arrival date
  is explicitly unknown rather than invented.
- **RECAP:** the eight latest real observations, newest first, with an older
  page and a quiet helper-contribution report. There are no interrupting
  recap pop-ups, offline stories, streaks or missed-day penalties.
- **TANK:** optional preset subtitles and emblem labels, founding date when
  known, last verified SD backup and total unknown-visitor sightings.
  Subtitles/emblems also appear in the aquarium picker/summary.
- **OBJECTS:** shop descriptions, ownership/source, recorded acquisition
  hour where available, and a fish that favors that placeable decoration.

Life firsts and adventure keepsakes have their own detail pages. The lantern
shell, pearl bookmark and star stone include short descriptions; newly
completed adventures attach them to the actual returning fish. Existing
postcards remain in Adventures, but older rewards are not retroactively
attributed to an unknown fish. Tap any name in **Discoveries** for a gentle
hint about the missing discovery.

A small unknown drifter may cross the upper reef for 24 seconds. The first
window is **6-12 hours of lit, uncovered aquarium simulation**, with later
windows 8-24 hours apart. Menus, darkness, power-off and inactive tanks do
not advance this counter; the board cannot detect whether someone is
physically watching. Sightings persist even if you miss the encounter.
The visitor is harmless: no feeding, care meter, reward or population slot.
Its small silhouette changes only once per second and uses local dirty
regions, including the existing opaque-clock occlusion handling.

Six new **fixed sand scenery** choices are available in the Shop: Sea Glass
(50), Brain Coral (110), Tiki Statue (130), Bottle Note (70), Anemone (90)
and Old Teapot (80 sand dollars). They use the cached backdrop, do not add
placement slots, and have no continuous animation. Static scenery can still
cost drawing time when the cache is rebuilt; this is not a promise that all
decorations are literally free.

SD restore confirmation previews the source tank name and firmware version.
Successful verified backups record their date; clockless backups explicitly
show an unknown date. Old backups have no source name and are labeled as
older backups. Backup scope remains **active aquarium + shared wallet**,
including its Bank and traveler, not all purchased SD tanks.

| Individual fish life | Quiet recent observations |
|---|---|
| ![Fish biography with remembered buddy and life firsts](media/cyd-features/fish-life.png) | ![Recent observations including a staged visitor sighting](media/cyd-features/tank-recap.png) |

| Tank identity | New fixed scenery |
|---|---|
| ![Tank subtitle, emblem and recorded dates](media/cyd-features/tank-identity.png) | ![Six fixed scenery choices in the actual renderer](media/cyd-features/tank-fixed-scenery.png) |

The visitor capture uses an accelerated demonstration schedule, not a changed
hardware encounter rate:

![A rare unknown drifter crossing the demonstration aquarium](media/cyd-features/tank-rare-visitor.png)

### Family, genes and visitors (new in 1.3)

Every fish now carries genes. Besides the marking and body color you see,
each fish holds a **hidden second marking and body color**, plus four
recessive cosmetic traits: **Shimmer** (a moving sheen), **Painted Fins**
(fins in the marking color), **Pale Belly** and **Gold Eye**. A trait only
shows when a fish inherits it from both parents; one copy is carried silently.
Bred fry take one allele from each parent, so a hidden look can resurface
generations later. About one birth in fourteen brings a brand-new marking, and
about one in twelve a new trait allele; the Family Tree marks these as
**MUTANT!** Existing fish receive randomly seeded hidden genes on upgrade.

Five new markings join Stripes, Spots, Band and Plain: **Saddle, Tiger,
Speckle, Fade and Mask**. They can appear through breeding or be chosen in
**CUSTOMIZE**, and work on every creature look, including jellyfish.

Open **Home > Fish > Care > LIFE**, then **FAMILY**, for a fish's generation,
parents, grandparents, children, siblings, births in this tank, shown genes
and what it carries. Up to twelve departed ancestors keep their names, so a
parent who has gone is still remembered. **NEXT FISH** walks the family.

The rare-visitor encounter now chooses among five harmless species: the
Drifter, Old Turtle, Ghost Fish, Night Manta and very rare Lantern Squid.
**TANK > SIGHTINGS** opens the Visitor Log with sighting counts and the
powered hour each was first seen; unseen visitors stay as **???**.

With a synchronized clock, the sand follows the seasons: spring sprigs,
summer shells and starfish, autumn leaves and a winter snow dusting, plus
holiday lights from December 15 to January 1. Choose **SEASONS: NORTH**,
**SOUTH** or **OFF** on the TANK page. Seasonal scenery is drawn into the
cached backdrop, so it does not add continuous animation.

| Family tree and genes | Visitor log |
|---|---|
| ![Three-generation family tree with inherited Gold Eye mutation](media/cyd-features/family-tree.png) | ![Visitor log with sighting counts](media/cyd-features/visitor-log.png) |

The [save viewer](https://jeromedontdev.github.io/pocket-tank-cyd/viewer.html)
opens any schema 9 or newer JSON backup in the browser, without uploading it,
and draws each fish card with its family, genes and visitor history.

### Room light, back LED and photos (new in 1.3)

The CYD's light sensor beside the screen and RGB LED on the back are now used.
In **Settings > Display**:

- **ROOM LIGHT: DIM** scales the backlight down (to 35% of your brightness)
  as the room darkens. **DIM+NIGHT** also switches the tank to night after
  20 seconds of darkness and back after 5 seconds of light. **OFF** ignores it.
  The sensor learns your lit room automatically.
- **BACK LED: GLOW** breathes softly in the water's color, dimmer at night.
  **ALERTS** adds an orange pulse when a fish is hungry and a pale flicker
  while a visitor passes. **OFF** keeps it dark.

**Settings > SD Backups > DAILY** makes one verified automatic backup per day
(`pocket-tank/auto-NNNN.json`, the newest seven are kept) once the clock is
set. **PHOTO TO SD** saves a BMP screenshot of the aquarium to
`pocket-tank/photos/`.

## Mini-games: play with your fish, not their care stats

Open **Home > Games** for **Bubble Pop, Pearl Match, Fish Slalom,
Treasure Cast, Feed Frenzy and Reef Puzzle**. Tap PLAY AS to choose an active
fish's appearance. Your actual fish are never put at risk or fed by a game.
Aquarium care pauses while the games menu, round or result is open.

| Pick a game | Swim through the open gates |
|---|---|
| ![Six-game selection with PLAY AS fish chooser](media/cyd-features/games-menu.png) | ![Fish Slalom uses the chosen aquarium fish](media/cyd-features/game-fish-slalom.png) |

| Pop bubbles | Remember pearl pairs |
|---|---|
| ![Bubble Pop timed round](media/cyd-features/game-bubble-pop.png) | ![Pearl Match memory cards](media/cyd-features/game-pearl-match.png) |

| Time your cast | Match a fish's snack |
|---|---|
| ![Treasure Cast timing game](media/cyd-features/game-treasure-cast.png) | ![Feed Frenzy with an existing aquarium fish](media/cyd-features/game-feed-frenzy.png) |

![Reef Puzzle rotates real pipe connections from inlet to outlet](media/cyd-features/game-reef-puzzle.png)

| Claim both rewards | Saved once, ready to replay |
|---|---|
| ![Round result offers sand dollars and Journal XP](media/cyd-features/game-rewards.png) | ![Saved rewards cannot be claimed twice](media/cyd-features/game-rewards-saved.png) |

Rounds award **up to 10 sand dollars plus 3 Journal XP per dollar**, helping
unlock Keeper levels and backdrops without faking care objectives.
Tap CLAIM to save both together; failed saves can be retried without losing
or duplicating rewards. BACK/replay asks for a second tap before discarding
an unclaimed prize. Sessions and unclaimed prizes are not saved across reboot.
See the [complete rules and reward table](../README.md#mini-games).

Use **CHALLENGE / RELAXED** to choose the pace. Relaxed Bubble Pop and Feed
Frenzy continue until you tap FINISH; Slalom and Treasure Cast move more
slowly. Relaxed rewards retain the usual cap, but records/medals belong to
challenge mode. **RECORDS** shows your claimed personal bests and the permanent
gold challenge for each game. There are no daily streaks or expiring challenges.

The Journal's Kelp Forest, Coral Reef and Deep Glow backdrops now use stronger
green/blue/midnight palettes, leafy kelp, branching coral and luminous jellyfish.
Their existing unlock levels (3/6/10) and IDs are unchanged. Original stays
unchanged, and purchased decorations and fish remain in front of the scenery.

### Four more backdrops to earn

The Journal's **THEMES** tab has two pages; use its `<` / `>` arrows.
Sunken Ruins unlocks at Keeper 5, Sunset Lagoon at 8, Ice Cavern at 12,
and Moonlit Reef at 15. This brings the selection to eight including Original.

| Sunken Ruins | Sunset Lagoon |
|---|---|
| ![Teal water with weathered stone columns and broken arches](media/cyd-features/tank-ruins.png) | ![Warm peach and purple water with a golden sunset and reflections](media/cyd-features/tank-sunset.png) |

| Ice Cavern | Moonlit Reef |
|---|---|
| ![Blue water with hanging ice and faceted crystals](media/cyd-features/tank-ice.png) | ![Midnight-blue water with a crescent moon and silver reflections](media/cyd-features/tank-moonlit.png) |

![Second theme page shows four new backdrops and their required levels](media/cyd-features/journal-themes-more.png)

### Fish appearances are progression rewards too

New tanks start with Classic fish. Keeper levels unlock Guppy (2), Tetra (3),
Clownfish (4), Angel (6), Betta (8), Puffer (10), Shark (12), Stingray (14) and
Jellyfish (15). The Album lists required levels; customization cycles only
available varieties, and shop fry offers use the same unlock rules.
All colors/patterns remain available. Existing active/banked fish and looks
already recorded in the Journal stay unlocked, so upgrading never takes away
a favorite. These unlocks are per aquarium, like Journal XP.

![A starter aquarium's Album shows required Keeper levels for unavailable varieties](media/cyd-features/journal-variety-unlocks.png)

## Your aquarium as a desk companion

Tap a fish, then its card's **MENU** button to open **Home**. The old
**Keeper > HOME** shortcut also works. Features are organized by purpose:

| Home category | Inside |
|---|---|
| **Fish** | Care & Bonds, Fish Bank, Wardrobe, Names & Looks |
| **Tank** | Shop & Decor, Aquariums, Sand & Water, Creature Colors |
| **Adventures** | Send a fish away, see its return time, claim postcards |
| **Collections** | Journal (themes/rewards/varieties), Discoveries (sets/frames/shelf) |
| **Games** | Six games, records, relaxed mode |
| **Settings** | Display, Clock/Weather, Wi-Fi/Sync, SD Backups, Feeding, Device |

Back returns to the category used to enter a feature; **TANK** on Home
returns to the aquarium. Appearance is no longer nested under fish quests.
Weather belongs to **Settings > CLOCK/WEATHER > WEATHER**.
**Fish > Care & Bonds** offers Cozy/Classic care, permanent fish bonds,
greetings, learned tricks, activity captions and a Calm/Lively setting.
Existing tanks retain Classic care; new tanks start Cozy. Comfort does not
fabricate feeding, cleaning or training achievements.

| Desk companion hub | Care and permanent fish bonds |
|---|---|
| ![Home groups Fish, Tank, Adventures, Collections, Games and Settings](media/cyd-features/desk-companion.png) | ![Care options, separate bond and trust, greetings and trick help](media/cyd-features/companion-home.png) |

SAY HELLO earns 5 bond every 30 powered-on seconds; DART unlocks at 10,
BUBBLES at 25, FOLLOW at 50. Bonds never fade and travel with banked fish.
Bond is friendship (0–100); trust is care confidence (0–10), not the same
stat. **BOND / TRICK HELP** explains both and each trick. Tricks request a
playful dart, a bubble visit or following a tankmate; close CARE to watch.
Confident, energetic fish can also choose dart play on their own; learning a
trick does not permanently enable a speed boost. Playful darts are now gentler
(42–58 internal pixels/second before urgency/vegetation adjustments), rather
than the old 65–95. A requested dart chooses a fresh destination immediately.
Startle reactions and hungry-food pursuit retain their separate behavior.

**Home > Fish > Wardrobe** opens the fish wardrobe. This is the single wardrobe entry point.
Use the arrows to preview an accessory, **NEXT FISH** to choose its owner,
and **BUY + EQUIP** to purchase it with sand dollars.

| Accessory | Journal level | Sand dollars |
|---|---:|---:|
| Party hat | 2 | 20 |
| Bandana | 2 | 20 |
| Top hat | 3 | 45 |
| Bowtie | 4 | 30 |
| Tie | 5 | 35 |
| Crown | 10 | 100 |

Each fish owns its purchases: one accessory equipped at a time, with free
re-equipping and **REMOVE** (which keeps ownership). Previews are free and
do not change the tank. Wearables are cosmetic only, with no XP or stat
bonus, and follow the fish through the Bank, adventures and backups.
Postcards retain the traveler's departure accessory; newborns start bare.
All ten fish varieties support all six accessories.
**Wardrobe > COLORS** gives the selected fish independent primary and detail
colors for its wearables. Each fish keeps one color pair across its wardrobe,
including Bank transfers and adventures; the postcard preserves departure colors.
Colors are free, including before buying an accessory.

**Home > Tank > Creature Colors** opens color settings for the
shrimp colony, snail, urchin and seahorse. **NEXT TYPE** cycles between them.
All shrimp share one color pair; each other creature has its own pair per tank.
Primary colors affect bodies/shells; detail colors affect legs, soft bodies,
spine tips or fins. Eye colors and procedural shading remain intact.

Both editors provide **Natural + 15 colors** for each channel, a live portrait,
and **SAVE COLORS**. Browsing does not change the tank or spend coins.
**NATURAL** previews original colors; tap SAVE to restore them permanently.
Changing colors does not purchase creatures or accessories or grant XP.
Whole-tank, resolution-style scaling is deferred to the next update.

![Fish wardrobe with accessory preview, unlock level, price and equip/remove controls](media/cyd-features/fish-wardrobe.png)

| Creature primary/detail colors | Wearable primary/detail colors |
|---|---|
| ![Blue snail shell and gold body preview](media/cyd-features/creature-colors-snail.png) | ![Red top hat with white brim preview](media/cyd-features/wearable-colors.png) |

| Bond, trust and trick help | Explicit frame unlock guidance |
|---|---|
| ![Help explains friendship, care confidence and each trick](media/cyd-features/companion-help.png) | ![A locked frame explains how to unlock it without overlapping the footer](media/cyd-features/companion-frame-locked.png) |

**Collections > Discoveries** automatically records aquarium activities and saves the discoveries
until you decide to browse them.
Frames show **LOCKED** until a set is completed and claimed; **FRAME >**
then cycles owned frames, with the current name underneath. SHELF toggles
earned aquarium trophies. Collection buttons show discovery counts.

| Discovery Album | Permanent collection cosmetics |
|---|---|
| ![Album records activities without requiring a timed tap](media/cyd-features/companion-album.png) | ![Collection claims unlock cosmetic frames and optional trophies](media/cyd-features/companion-collection.png) |

**Home > Adventures** offers three safe adventures: Lantern Lagoon (**2 hours**),
Pearl Library (**6 hours**), and Starlight Cove (**12 hours**).
Pick a fish and ARCH/BUBBLES postcard scenery (same rewards), then
**SEND FISH > CONFIRM SEND**. The real fish leaves until its automatic return,
with no hunger, aging, injury, failure or expiry. Its tank slot stays reserved.
Each destination has one permanent postcard and one claim: 10/20/30 coins
plus 30/60/90 Journal XP. Timers include unplugged time when a synced clock
is available; otherwise powered-on time advances them. Rewards wait without
a deadline after the fish is home.

| Choose a destination and route | A completed adventure waits for you |
|---|---|
| ![Destination, time away, selected fish and postcard scenery](media/cyd-features/adventure-menu.png) | ![Returned fish leaves a persistent reward claim](media/cyd-features/adventure-ready.png) |

| Earned aquarium trophies | Collection ornament and activity caption |
|---|---|
| ![Optional earned collection shells and game challenge markers](media/cyd-features/tank-trophies.png) | ![Large clock with earned corner ornaments and non-overlapping caption](media/cyd-features/clock-collection.png) |

Under **Settings > CLOCK/WEATHER > WEATHER**, search for a city, select the correct result and
choose F/C. Only your city query/selected coordinates go to Open-Meteo over
HTTPS; nothing infers your location. Saved Wi-Fi is required, and unavailable,
offline or stale readings are identified rather than replaced with fake data.
Weather settings are device-wide, separate from aquarium backups.
Weather refreshes about every **15 minutes**, with a stale warning after
**30 minutes**. Wi-Fi sleeps between refreshes and reconnects when needed;
the last reading stays visible without a false offline warning during sleep.
City searches also temporarily wake Wi-Fi. Failed updates retain the reading
with a stale/offline warning and retry with backoff.
The city and conditions now use a bordered, larger-text clock
area rather than a tiny aquarium caption.
The clock's **SIZE** control also scales this panel: Large uses larger lettering,
while Medium and Small use compact, readable text with narrower borders and spacing.

**Postcards** are saved adventure souvenirs, not photos or messages. Returning fish
leave a destination's find and ARCH/BUBBLES scenery recorded in Adventures. Claiming
the postcard pays that destination's one-time coins and XP; completing the three
stories also progresses the Story collection. There is no deadline to claim.

**Frames** are cosmetic collection rewards, not picture holders. Complete and
claim Nature, Friend or Story in Discoveries to unlock its frame. **FRAME >** cycles
the default and unlocked styles, changing the companion page/portrait border
colors and adding matching clock-corner accents. They do not boost fish stats
or rewards. **SHELF** separately controls earned aquarium trophies.

**Settings > Display > BRIGHTNESS** is a draggable percentage slider, from 10% to 100%.

| Optional city-configured weather | Bordered clock weather |
|---|---|
| ![Weather starts disabled and requires an explicit city choice](media/cyd-features/weather-settings.png) | ![Illustrative Paris weather fixture in its own larger bordered clock area](media/cyd-features/clock-weather.png) |

See [complete companion, quest and weather details](../README.md#desk-companion-discoveries-and-safe-adventures).
Schema 7 backups include the traveling fish and companion progress. Earlier backups still import;
do not downgrade after using the new progression.

## 1. Keep favorites in the Fish Bank

The active aquarium still holds **five fish**. The new bank adds **20
separate storage slots**, so making room no longer has to mean selling a
favorite.

Open **Home > Fish > Fish Bank**:

1. Choose **IN TANK**, select a fish and confirm **STORE**.
2. Choose **STORED > CHOOSE** to inspect a saved fish.
3. Use **RETURN** when the tank has room.
4. If the tank is full, choose **SWAP**, select the active fish to trade
   places with it, then confirm.

| Browse stored fish | Inspect a favorite |
|---|---|
| ![Fish Bank showing 20 occupied slots, five active fish and paginated stored fish](media/cyd-features/bank-stored.png) | ![Stored fish detail with its appearance, age, trust, Return and Swap buttons](media/cyd-features/bank-details.png) |

| Choose the outgoing fish | Confirm before moving either fish |
|---|---|
| ![Active fish selection while swapping with a banked fish](media/cyd-features/bank-swap-selection.png) | ![Swap confirmation naming both the stored and active fish](media/cyd-features/bank-swap-confirmation.png) |

### What storage preserves

Names, body and marking colors, variety, pattern, stage, age, personality,
trust, care state, food counts and milestone reward records travel with the
fish. Stored fish **do not age, grow, eat or need attention**, and do not
earn care rewards while stored.

Storage is free. At least **two fish must stay active**, but swaps still
work at that minimum, at the five-fish tank limit, or with all 20 bank
slots occupied. Finish naming a new fry before moving fish.

Every move saves immediately. If saving fails, neither fish is moved.
Swimming positions and current goals restart on return. Parent links that
refer to a departing active slot are cleared, rather than accidentally
pointing at its replacement.

![Successful swap with 20 banked fish and five active fish still present](media/cyd-features/bank-swap-saved.png)

The bank is included in internal saves and SD backups. Removing the card
does not remove stored fish. Restoring an older, pre-bank backup restores
an empty bank; **Wipe clears the bank along with the aquarium**.

## Additional aquariums: room for another little ocean

Open **Home > Tank > Aquariums**. Each additional aquarium costs
**500 sand dollars from your shared wallet**. The original is free,
with up to **eight total**. Confirming a purchase enters a fresh tank with
two fry and the normal welcome, naming and customization setup.

| Buy another aquarium | Confirm its price |
|---|---|
| ![Aquarium selection with a new tank costing 500 sand dollars](media/cyd-features/aquariums.png) | ![Purchase confirmation explaining shared sand dollars and separate fish, items, Journal and Bank](media/cyd-features/aquarium-purchase.png) |

**Sand dollars are shared across all tanks.** Each tank has its **own fish,
decorations, upkeep/fish-care upgrades, layout, palettes, Journal and
20-slot Fish Bank**. Buying an upgrade in one tank does not unlock it elsewhere.
Switching among owned tanks is free. **NAME TANK** edits the active tank's
name, up to 15 characters: select a letter, use `<` / `>` and SAVE. BACK cancels.
Names persist across switches and restarts.

| Name your active tank | Keep your tanks recognizable |
|---|---|
| ![Tank-name editor with letter selection and left/right arrow controls](media/cyd-features/aquarium-name.png) | ![Saved Reef Garden name in the aquarium selector](media/cyd-features/aquariums-named.png) |
Inactive tanks are fully paused on SD: no aging, hunger, growth or rewards
while you play another aquarium. Finish naming any new fry before leaving.

| A fresh start, without wiping the first tank | Return whenever you like |
|---|---|
| ![Welcome flow for the newly purchased aquarium with two new fry](media/cyd-features/aquarium-welcome.png) | ![Two owned aquariums, one active, with free Open controls and another purchase available](media/cyd-features/aquariums-owned.png) |

Buying or switching requires the card. The outgoing aquarium is saved to
alternating, checksummed SD files and read back before the new active tank
is committed to internal storage. A failed transaction does not charge you
or replace the live tank. The selected tank can keep running without SD,
but the card is needed to access inactive tanks.

**Backup/Restore and Wipe now apply to the active aquarium only.**
Purchased slots and other aquariums remain. Individual JSON backups do not
contain the entire collection, slot ownership or tank names; a JSON restore
keeps the current device's slot identity/name and replaces the active tank's
contents **and the shared wallet**. Also copy the SD card's `pocket-tank/tanks`
directory when archiving it. There is no one-click whole-collection recovery:
SD copies alone cannot recreate collection identity after NVS is erased.
Avoid downgrading to older firmware, which does not preserve the collection.

## 2. Make each fish your own

Open **Keeper > fish portrait > CUSTOMIZE** to edit an existing fish for
free. The adult preview makes the shape and markings easy to judge before
committing.

![Fish customization showing a hot-pink shark, Band pattern, body and marking color controls](media/cyd-features/customization.png)

### Ten creature appearances

**Classic, Angel, Guppy, Tetra, Puffer, Shark, Jellyfish, Stingray,
Clownfish and Betta.**

These are more than palette swaps: sharks have pointed snouts, dorsal
fins and gills; jellyfish pulse and wave their tentacles; stingrays flap
their wings; bettas have flowing fins.
Swimming clearance projects each silhouette, growth size, pitch, animated
fins/tails and wearable above the highest sand edge. Dorsal fins no longer add
an unnecessary downward margin, and gentle upward steering begins only four
pixels before the safe limit. Sharks and jellyfish can approach the sand
without letting their bodies or tentacles sink into it; floor pellets remain
reachable.
Exploration uses the full width in both display modes, with body-size-based
side clearance rather than large empty margins within each third of the tank.
Fish approach destinations more closely before choosing another one. Grass
beds include their last safe frond position near the right glass; there is
still a small clearance for leaf sway rather than drawing past the screen.

Each appearance supports:

- **Nine markings:** Stripes, Spots, Band, Plain, Saddle, Tiger, Speckle, Fade and Mask.
- **24 body colors** and **24 marking colors**, chosen independently.
- Vivid pinks, reds, oranges, yellows, lime, cyan, electric blue, purple,
  magenta, pastels, white and dark slate.
- **DONE** to save, or **CANCEL** to discard the preview.

Fry offers show their appearance, and bred fry inherit appearance traits
from their parents. The varieties share the existing peaceful fish-care
system: sharks do not hunt tankmates, and jellyfish do not introduce a
separate species simulation. A jellyfish-looking fish uses a normal fish
slot; the decorative Jelly Lantern is a different shop item.

## 3. Give everyday care a sense of progression

Open **Home > Collections > Journal** for **Goals**, **Album** and **Themes**.

### Keeper levels and objectives

There are **15 Keeper levels** and **15 one-time objectives**, with three
objectives active at a time. When a goal is ready, tap it to claim its XP
and sand-dollar reward; the next objective takes its place.

![Aquarium Journal Goals tab showing Keeper level, XP and three objectives with readable requirements and rewards](media/cyd-features/journal-goals.png)

Care earns ongoing XP:

| Activity | XP |
|---|---:|
| Eaten meal batches | 8 |
| Cleaned algae colonies | 2 |
| Each 10 cm of plant trimming | 2 |
| A successful calm-hold approach | 10 |

Spending sand dollars does not lower your Keeper level. Level 15 starts at
**4,200 XP**. There are no daily streaks or absence penalties in this
progression system. Existing care totals can qualify for objectives,
but old care does not award a bulk retroactive XP payout.

### A permanent appearance album

The album records which appearances you have seen and which have reached
adulthood. Records remain after a fish is sold or stored. Customizing a
fish counts too: this is an **appearance album**, not a checklist requiring
you to buy every variety.

![Album tab with all ten creature looks and adult records in the staged demonstration tank](media/cyd-features/journal-album.png)

### Unlockable backdrops

| Backdrop | Unlock |
|---|---|
| Original | Always available |
| Kelp Forest | Keeper level 3 |
| Coral Reef | Keeper level 6 |
| Deep Glow | Keeper level 10 |

![Themes tab listing Original, Kelp Forest, Coral Reef and Deep Glow](media/cyd-features/journal-themes.png)

Themes change the backdrop without replacing your chosen sand or water
palette. XP, claimed objectives, album records and the selected theme are
included in saves and SD backups.

| Kelp Forest | Coral Reef |
|---|---|
| ![Decorated demonstration aquarium against the Kelp Forest backdrop](media/cyd-features/tank-kelp.png) | ![The same demonstration layout against the Coral Reef backdrop](media/cyd-features/tank-coral.png) |

![Demonstration aquarium with the Deep Glow backdrop](media/cyd-features/tank-deep-glow.png)

## 4. Decorate beyond the original tank

The fork adds these shop decorations and sets their prices as follows.
Prices are in sand dollars.

![Shop overview showing the existing aquarium upgrades and navigation to further pages](media/cyd-features/shop.png)

| Added decoration | Price |
|---|---:|
| Treasure chest | 120 |
| Scallop shell | 60 |
| Starfish ornament | 60 |
| Sunken anchor | 80 |
| Tethered buoy | 80 |
| Shipwreck | 200 |
| Treasure urn | 60 |
| Crystals | 100 |
| Sea fan | 80 |
| Sunken ruins | 160 |
| Jelly lantern | 120 |
| Zen pebbles | 40 |
| Duck float | 60 |
| Pearl clam | 70 |
| Lily pads | 80 |
| Floating log | 90 |
| Tube sponges | 100 |
| Lost barrel | 110 |
| Dive helmet | 140 |
| Sea glass | 50 |
| Brain coral | 110 |
| Tiki statue | 130 |
| Bottle note | 70 |
| Anemone | 90 |
| Old teapot | 80 |

The shop now sorts unlocks **cheapest first**, keeping equal-price choices
in a stable order. The optional nursery FRY row remains a separate first row.
There are **42 stable unlock IDs** in the catalog (the seasonal pumpkin can
be unavailable), plus the separate nursery offers. All original and added
item icons follow the same ownership rule in both the list and detail dialog:
**muted gray before purchase, full color after purchase**. Gray does not mean
you cannot afford an item; the price/button indicates affordability.

### More scenery, including the surface

The eight original scenic unlocks and six new sand ornaments have **fixed
positions** and stay in their tank;
they have no MOVE control, but SELL refunds the usual 20 percent after two
taps. The shop describes
this before purchase. The **lily pads, floating log and duck float** gently
bob in three separate surface positions and can all be displayed together.

![New fixed floor and surface scenery](media/cyd-features/tank-new-scenery.png)

![Three surface decorations together](media/cyd-features/tank-surface-floats.png)

### Optional upkeep upgrades

- **Slow Garden - 160 sand dollars:** grass grows at half its usual speed.
- **Bio Filter - 180 sand dollars:** new algae grows at half its usual speed.

Both work awake and asleep, only in the tank that owns them. They are
permanent upgrades; they do not remove existing growth, generate cleaning or
trimming rewards, change fish care or slow coral/sword-plant growth. Inactive SD tanks
remain completely paused.

![Price-sorted shop with functional unlocks](media/cyd-features/shop-upkeep.png)

Placeable decorations support positioning, depth/layering, moving and
selling. The buoy and jelly lantern bob above their bottom tethers rather
than sitting on the sand. Placements and ownership survive saves and
backup/restore.

| Decorations and palette upgrades | More decoration choices |
|---|---|
| ![Price-sorted decoration choices with gray unowned icons](media/cyd-features/shop-decorations.png) | ![Original and added items using the same locked-icon style](media/cyd-features/shop-expansion.png) |

### Fish-care upgrades

Each aquarium can buy its own permanent improvements in the price-sorted shop:

| Upgrade | Sand dollars | Benefit |
|---|---:|---|
| Calm Current | 160 | Resting and sleep restore energy 25% faster |
| Cozy Shelter | 180 | Base natural stress decay is 25% faster, awake and asleep |
| Play Kit | 200 | Exploring restores curiosity 25% faster |
| Training Ring | 240 | Calm holds earn an extra 25% of the normal trust rate |

Training Ring adds to the seahorse bonus: together they give 225% of the normal
rate while hitched, not an unlimited multiplier. Stats still cap at 10.
No changes to food rewards, hunger, aging, size or breeding requirements.
Nothing stacks repeatedly; extra tanks and banked fish remain paused.
These upgrades are saved with their own aquarium, not shared between tanks.
Cozy Shelter boosts base calming, not separate vegetation comfort effects.
Calm Current also speeds up the daytime breather: an exhausted fish ducks
into nearby seagrass (or its resting corner) to recover before resuming its
advisor goal. Play Kit does not amplify curiosity spending or food rewards.
Wipe removes these purchases from the active tank. Old backups that predate
the optional care-upgrade field restore without these upgrades.

![Fish-care upgrades in the shop](media/cyd-features/shop-fish-care.png)
![Training Ring purchase details](media/cyd-features/training-ring.png)

### Loud colors, without flashing

**Sand Palette** and **Mood Lights** each cost **300** as one-time
upgrades. After purchase, use the separate **SAND** and **WATER** rows in
**Home > Tank > Sand & Water** to cycle freely.

- **Sand:** Original, Gold, Pearl, Volcanic, Rainbow, Hot Pink,
  Electric Cyan and Neon Lime.
- **Water:** Original, Blue, Teal, Violet, Amber, Rainbow, Hot Pink
  and Neon Lime.

The original look remains selectable. Rainbow is a colorful palette, not
a flashing or strobing effect.

![Demonstration aquarium with Rainbow sand and Rainbow water selected](media/cyd-features/tank-rainbow.png)

## 5. Menus made for a small screen

The CYD uses a 240 x 180 rendering framebuffer on its 320 x 240 panel.
Previously, small logical text could shrink until strokes disappeared.
Text now lands on the framebuffer pixel grid with a complete minimum
**5 x 7 glyph**, brighter secondary labels and wrapped status messages.

Journal cards, fish-bank rows, customization controls and Settings use
readable captions rather than tiny downscaled subtext.
Menus use consistent dark buttons with pale teal borders and white labels.
Page arrows and tank-name letter cycling use `<` / `>`, not plus/minus.

| Keeper navigation | Reorganized Settings |
|---|---|
| ![Keeper page with fish portraits, milestones and Home shortcut](media/cyd-features/keeper.png) | ![Settings grouped into Display, Clock/Weather, Wi-Fi, SD Backups, Feeding and Device](media/cyd-features/settings.png) |

Settings groups brightness and scale in **Display**, network setup in
**Wi-Fi / Sync**, and backup/restore in **SD Backups**. **Device** contains
updates, touch calibration and the confirmed aquarium wipe. Sand and water
palettes live under **Home > Tank > Sand & Water**.

### Whole-tank Normal mode

Open **Settings > Display > SCALE** to switch between **Enlarged** (240x180)
and **Normal** (320x240, formerly Compact). Normal is the default for fresh
installations, resets and newly purchased aquariums. Existing saves keep their
chosen scale; the old Normal view is now called Enlarged. Normal uses
the LCD's native resolution: fish, wearables, creatures, decorations, food,
bubbles and clock graphics occupy about **25% less screen width/height**,
leaving more tank space without cropping or adding an inset border.
Full-page controls retain their readable layout and matching touch targets.

The choice is saved per aquarium and included in backups. Positions and
algae are remapped when switching; fish identity, ownership, coins and XP
are unchanged. A failed save leaves the old scale active. On the no-PSRAM
CYD, Normal draws three horizontal tiles through the existing framebuffer,
so weather still has RAM available; animation can be slower than Enlarged.
Longer Normal frames use bounded simulation steps so care and idle timers
still advance in real time rather than slowing with the animation.
The renderer clips shape/decor work to each tile and reuses row colors;
the LCD runs at 40 MHz. On the tested CYD, the
populated Ice Cavern aquarium measured about 6.5–6.8 FPS in Normal after
artwork/clock caching and weather radio sleep (previous builds measured
about 2.4 FPS, then 4 FPS, then 5–6 FPS). This does not meet a 15 FPS aquarium target.
Actual speed varies with fish, decorations and effects.
An additional tile-culling pass skips off-tile fish and limits seagrass
scanline calculations to the visible rows. Exact full-frame/tiled comparisons
cover all fish varieties, stages and wearables. On-device samples after this
pass ranged about 6.6–7.3 FPS; clock size/state, fish positions and user
interaction affect the result, so this is not a fixed-FPS guarantee.
The latest pass removes the cosmetic edge-darkening vignette on CYD (it has
no gameplay function). The warmed aquarium with large clock/weather measured
about 7.5–7.7 FPS (about 8.2–8.4 FPS before the idle clock appeared); the
vignette profile is now 0 ms. The following Normal pass measured about
10.4-11.1 FPS with the saved Medium Flip clock/weather visible and
10.6-11.5 FPS with the clock OFF after warm-up in the same populated aquarium.
These are measured samples, not a minimum-FPS guarantee; boot, weather
refreshes, different populations and fish positions can still cause dips.
This meets the tested 10 FPS goal, not the original 15 FPS aspiration.
Release 1.2.0 measured 10.6-11.8 FPS after warm-up with the user's
existing one-resident-fish tank and Neon clock/weather visible. Its recent
1.1 baseline was about 10.3 FPS. This sample showed no steady-state slowdown,
but it is not a controlled benchmark or a guarantee for other populations;
the new scenery was exercised separately in renderer tests and demo captures.
A later hard-edged vignette trial used integer RGB565 shifts, a dark perimeter
and wider corners. The shading stage cost about 3 ms/frame, but the complete
trial build measured only 8.0-8.4 FPS after warm-up; that overall regression
was larger than the shading stage alone explains. It was not retained.
Restoring the vignette-free build recovered 10.6-10.7 FPS with clock/weather.

Normal draws half the ambient bubbles and uses gently leaning, more rigid
grass blades with a 2 Hz sway update. Growth and trimming still apply immediately.
Seahorse secondary artwork updates at 5 Hz; its blade attachment uses the same
grass shape. Fish movement and the care simulation are not slowed down.
A shrimp school is represented by one visible shrimp per two actual shrimp,
rounded up (10 actual shrimp show as 5). Its formation is tighter, but the
actual population, feeding/progression rules and card totals are unchanged.
Enlarged mode retains its full bubble/shrimp display and original grass motion.

Normal reuses the unused end of its existing framebuffer for a lossless,
run-length-compressed water/theme/sand and static-scenery cache, without allocating another
full screen or taking RAM away from weather. Complex rows that do not fit
are rendered normally. Cache keys include resolution, theme, water/sand
palettes, day/night dimming, scenery ownership, decoration order/positions and
coral growth/colors. Stable decorations are cached in painter order up to the
first animated/dragged piece; later pieces and foreground redraws remain live,
preserving transparency and fish occlusion. Complex rows still fall back exactly.

The LCD driver fingerprints output blocks and only transmits changed blocks,
so old fish positions are restored when fish move and unchanged clock/menu
regions are not repeatedly sent. Small alternating DMA buffers overlap
pixel preparation with the preceding transfer. This is **changed-block LCD
updating**. Normal additionally tracks conservative **two-dimensional CPU
dirty regions**: old and new creature/overlay rectangles are united
into 32x40 blocks. Unchanged blocks are not drawn or transferred. Algae
changes invalidate their own cells; covering pages or background changes
invalidate the frame. Rectangles merge within the existing 80-row buffer;
over 40% coverage falls back to large canonical tiles to avoid excessive setup
overhead and preserve changed-block LCD fingerprints. Grass invalidates its
old/new bounds only on a sway tick, height change or move, without an extra
repaint on the following frame. Fish bounds follow their projected anatomy,
including fins, jellyfish strands and wearables, rather than a fixed wide box.
This is block-based, not per-pixel damage. Dense grass, bubbles and a large
clock can still select full tiles. This does not redraw the hidden scene:
visible clock/weather panels are opaque, and Normal skips scene pixels and
wholly covered creatures/fronds behind them in a single scene traversal.
A stationary Normal Home menu
measured about 25 FPS with zero pixel blocks transmitted after settling;
that is not the aquarium's animation rate.

The expensive urchin uses 16 reusable spine poses, with separate invalidation
when chewing changes. Floor-snail shell artwork uses eight subpixel heave
positions (at most about 0.09 pixel from the live shape); eye stalks and foot
waves stay live. Colors, water tint, night dimming and blending are still
applied when drawing. Existing glass-snail and seahorse geometry caches remain.
Clock/weather artwork is cached as exact paint runs until displayed time,
text or settings change. Visible panels are solid; hiding the clock face panel
still leaves any weather/activity caption panel opaque and occluded.
These bounded optional caches use small allocations and are released before
weather reconnects; low-memory cases explicitly fall back to live drawing.
SD backup/restore validation also releases them first, so warm artwork caches
do not take memory needed by the JSON safety checks.

Menu icons now live in firmware flash instead of an SD-loaded RAM buffer.
This removes a 12 KiB icon cache and menu card reads; fish and scenery were
already procedural, not loaded from SD. The card is still used for backups,
updates and inactive aquarium snapshots, so keep it inserted for multiple tanks.
Fish keep a short settling interval even during urgent turns, preventing
rapid collision-induced left/right reversals. Both grass beds now use the
same 12-pixel root inset rather than letting a lush left bed start off-screen.

| Display controls | Normal aquarium |
|---|---|
| ![Brightness slider and Enlarged/Normal toggle](media/cyd-features/settings-display.png) | ![Native-resolution aquarium with smaller scene objects and more space](media/cyd-features/tank-normal.png) |

### A clock over your aquarium

Open **Settings > CLOCK/WEATHER** to choose **Minimal, Digital, Analog, Neon, Flip,
Binary or Orbit**.
This is an idle display mode, **large and centered by default**, with
large/medium/small sizes, center or four-corner placement, 12/24-hour time and
a UTC offset with selectable **1-hour / 30-minute / 15-minute** adjustment
steps (tap **STEP**, default 1 hour). The clock starts **OFF**, saves separately
for each tank, and follows its SD snapshots and JSON restores.
**ACTIVATE** offers **15 sec, 30 sec, 1, 2, 5 or 10 min**, or **Manual Only**.
**SHOW CLOCK** enters immediately; the first touch only dismisses it, without
affecting fish or tools. Menus do not count as inactivity. Fish keep swimming
behind the solid panel, without drawing the hidden scene in Normal mode.
Set your standard-time UTC offset, then use
**DST: +1 HOUR** during daylight saving; turn it OFF for standard time. This
manual toggle changes the time and date together and saves per tank. It does
not select regional rules or switch automatically.

**STYLE** offers separate **PRIMARY** and **SECONDARY** selectors, each with
theme defaults plus **white, amber, mint, cyan, rose, lavender and red**.
Primary colors the numerals/hour indicators; secondary colors the captions,
date, border and minute/second indicators. Theme defaults keep the original
multi-color accents. Orbit leaves a clear gap between its circle and AM/PM.
Independently hide **seconds**, the **background panel
and border**, or the **date**. Seconds visibility also controls the analog
second hand, Binary seconds columns and Orbit seconds ring. A missing time sync
still shows **SYNC NEEDED**. Preferences save per tank, and **SHOW CLOCK** previews
the result from either settings page.

| Minimal | Digital |
|---|---|
| ![Large minimal aquarium clock](media/cyd-features/clock-compact.png) | ![Large seven-segment aquarium clock](media/cyd-features/clock-digital.png) |
| Analog | Neon |
| ![Analog face with digital caption](media/cyd-features/clock-analog.png) | ![Neon pink and cyan aquarium clock](media/cyd-features/clock-neon.png) |
| Flip | Binary |
| ![Split-flap numeral tiles](media/cyd-features/clock-flip.png) | ![Binary-coded decimal columns with 8/4/2/1 weights](media/cyd-features/clock-binary.png) |
| Orbit | Custom colors and details |
| ![Concentric hour, minute and second rings](media/cyd-features/clock-orbit.png) | ![Cyan digital numerals without seconds, panel or date](media/cyd-features/clock-custom.png) |

Binary's pairs of columns represent each decimal digit: add the lit weights
(8/4/2/1, top to bottom) in each column. Orbit's gold outer ring tracks the
12-hour cycle, cyan middle ring tracks minutes and pink inner ring tracks
seconds; the center shows the ordinary time.

**WI-FI / SYNC** saves the aquarium and briefly restarts into an
on-screen network picker and case-sensitive password keyboard. Choose a visible
2.4 GHz network, connect, then **BACK TO TANK** to synchronize with NTP.
All letters fit on one QWERTY page with large uppercase keycaps. Typing starts
lowercase; **CAPS** and the case indicator control uppercase entry, while the
password field shows exactly what you typed. **123** opens a separate
number/symbol layout, **#+=** shows more symbols and **ABC** returns to letters.
Dedicated **SPACE** and **DEL** keys let you edit without leaving the keyboard.
Saved networks can be reconnected, changed or forgotten with confirmation.
This flow never checks or downloads firmware.

| Clock settings | Choose a network |
|---|---|
| ![Clock controls; Wi-Fi setup lives in Settings](media/cyd-features/clock-settings.png) | ![Simulated Wi-Fi network picker](media/cyd-features/wifi-networks.png) |
| Password keyboard | Connected |
| ![Case-sensitive Wi-Fi password keyboard](media/cyd-features/wifi-password.png) | ![Connected network confirmation](media/cyd-features/wifi-connected.png) |

![Clock color and visibility controls](media/cyd-features/clock-style.png)

![Orbit with amber primary and cyan secondary colors](media/cyd-features/clock-two-color.png)

### Cycle fish without closing the card

Select a fish, then tap **>** just outside the card's upper-right corner to
select the next fish. It wraps back to the first fish and keeps you in the
tank without feeding. Tap the card itself to open Keeper as before.
The arrow appears only when the active tank has multiple fish, not on the
special-creature cards.

![Selected fish card with next-fish arrow](media/cyd-features/fish-card.png)

The CYD has no battery-backed clock. Once NTP succeeds, time keeps ticking
offline while powered or in deep sleep; power loss requires another sync.
Until then the overlay says **SYNC NEEDED**, rather than showing an approximate
save/build time as real time. Credentials are device-wide, not part of tank
backups. The screenshots use fake emulator networks, not real credentials.

### A deliberate fresh start

**Settings > Device > WIPE AQUARIUM** opens a separate confirmation before starting over
with two fry and first-time setup. Cancel, or wait 20 seconds, to keep
the aquarium.

![Separate Wipe Tank confirmation with Cancel and Wipe choices](media/cyd-features/wipe-confirmation.png)

Wipe clears the active tank's fish, bank contents, decorations, care/upkeep
upgrades, clock preferences and Journal
progress. With multiple tanks, the global sand-dollar wallet is kept; with
only the original tank, its wallet resets too. It **does not delete purchased
slots, other tanks, SD backup files or touch calibration**.
Restore a compatible SD snapshot to bring back the saved tank and bank.

## 6. Make the SD card useful

### Tank backups and restores

Settings provides versioned JSON backups on SD, including fish,
appearance, shared wallet, owned items, care/upkeep upgrades, placement,
palettes, Journal and bank.
Backups are written and read back for validation. A restore first creates
a safety snapshot of the current aquarium.

New backups use **schema 12**, including family lineage, genes, visitor sightings, fish identities/stories, remembered buddies,
recaps, tank identity, fixed scenery, backup status, display scale, traveling fish, creature colors, per-fish wearables/colors,
companion progression and the optional `tank.stat_upgrades` field for the four
fish-care purchases. Schemas 1 through 11 remain importable; older firmware
cannot restore schema 12 files. Keep a matching backup
when deliberately downgrading.

Backup export processes sections and stored fish one record at a time to
reduce RAM use beside weather's Wi-Fi buffers on the no-PSRAM CYD. Restore releases the input buffer
before writing its safety snapshot, then rereads and checks the original
file before committing. The compact internal save stays within its
4,520-byte layout (including aquarium identity, care upgrades, colors, scale and the reserved traveler),
so normal play and bank storage do not depend on keeping
the card inserted.

### Offline firmware updates

**Settings > Device > SD Updates** checks a local package at
`pocket-tank/update.ptu` on SD. It does not require Wi-Fi or install
anything automatically at boot.

![Offline SD update screen explaining the package location and trusted-file requirement](media/cyd-features/sd-updates.png)

The installer checks the board, image, size and SHA-256, asks for explicit
confirmation, and creates a verified pre-update tank backup. It writes
the inactive application slot and changes the boot selection only after
successful verification.

Packages are **local and unsigned**: the checksum detects corruption,
not an untrusted publisher. Only install trusted packages and keep power
connected. See [the update guide](OTA.md) for packaging and installation.

All artwork is embedded in firmware; no icons are needed on SD. The card provides
useful storage, but does not substitute for the RAM that rendering and JSON
processing need. Optional daily backups and photos are described under
[room light, back LED and photos](#room-light-back-led-and-photos-new-in-13).

## 7. A focused CYD fork, not a different game

The underlying aquarium still has personality-aware goals, feeding,
growth, trust, milestones, breeding and maintenance. This fork uses a
**rules-based advisor**, not an LLM or learned model. Other board targets,
inference/training, audio, battery/PMIC and IMU support were removed from
this CYD-specific build.

The desktop emulator runs the actual gameplay and renderer with an
isolated save. It is useful for feature previews and regression tests,
but is not hardware emulation: the card, LCD, touch controller and update
flashing still need physical-board checks.

## Recreate these screenshots

From the project root, with Python 3.9+, an existing GCC/Clang-compatible
compiler (or Zig), and the firmware's managed cJSON dependency available:

```powershell
python tools\capture_cyd_gallery.py --cc clang
```

Use `--cc C:\path\to\zig.exe` for Zig, or `--out C:\path\to\gallery`
for another destination. The capture tool compiles the emulator, stages
its own temporary demonstration save, navigates the real UI and exports
41 PNGs. It never opens a serial port, touches the board, or replaces
the normal desktop save.

For build instructions and live usage, see the [project README](../README.md)
and [CYD hardware guide](../firmware/CYD.md).
