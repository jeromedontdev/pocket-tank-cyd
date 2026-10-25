# CYD fork updates

<p align="center"><a href="https://jeromedontdev.github.io/pocket-tank-cyd/"><img src="../web/logo.svg" width="200" alt="Pocket Tank CYD aquarium logo"></a></p>

## Save-preserving browser USB updates

Since 2.3.6, select the exact **original CYD** or **ELEGOO ESP32-32E 2.8-inch
ILI9341V** first. Both choices download the same unified application;
the manifest binds its `cydesp32profiles` family and the explicit hardware
profile. Existing legacy firmware must match that hardware's old identity;
an existing unified image must report the matching saved runtime profile.
Unknown families and mismatched profiles are refused.
Legacy ELEGOO firmware still uses
`pocket-tank-elegoo-app.bin`, `elegoo-update.ptu`, `elegoo-ota.json` and
`elegoo-SHA256SUMS.txt`, with a separate ZIP/factory image. Original CYD asset
names remain unchanged. If original CYD firmware was mistakenly installed on
ELEGOO, use the backed-up [explicit app-only conversion](../firmware/CYD.md#recovering-a-wrong-cyd-image-on-elegoo-without-resetting-fish),
not a factory reset or an unrestricted cross-board update.

Migrate legacy firmware to unified once over USB. After the app checksum is
verified, the installer reboots and writes only a missing hardware-profile key,
then confirms it over serial. It does not overwrite an existing different
profile, calibration, Wi-Fi or aquarium saves. Subsequent unified SD/Wi-Fi
updates use **unified-update.ptu** / **unified-ota.json** and preserve that
profile. Local migration with a full flash backup is documented in the hardware
guide. New-board factory images contain the same app with different blank NVS
profiles; they are destructive and are never a save-preserving migration.

Open the [USB installer](https://jeromedontdev.github.io/pocket-tank-cyd/flash.html) in
desktop Chrome or Edge and choose **save-preserving USB update**. Make an SD backup first
and close serial monitors. The updater normally resets the board, checks its
CYD boot identity and exact `ota` partition report, and refuses unknown layouts
or a pending different boot slot. It validates the application checksum and
board marker before flashing only the detected running application slot.
Internal profile storage, NVS, Wi-Fi credentials, calibration, partition table
and SD files are not erased. Pocket Tank CYD 2.0.0 is a distinct game with
expanded saves: legacy saves migrate, but do not downgrade an upgraded profile.

Unlike SD updates, this overwrites the running application slot in the ROM
bootloader rather than installing to the inactive slot. Keep power and USB
connected; an interrupted write may require retrying that same application
address. Do not use factory reset as recovery unless deliberately erasing
saved state. The updater reports the detected recovery address on failure.
It preserves the last committed save; unsaved changes cannot be recovered.
Old or unrecognized firmware that cannot provide a valid report must use the
documented SD/manual upgrade path instead.

**Factory Reset is a separate destructive flow**, never a normal upgrade.

## Offline SD updates

Prebuilt packages are available from
[CYD Releases](https://github.com/jeromedontdev/pocket-tank-cyd/releases).
Download the bundle, read `INSTALL.txt`, and verify `SHA256SUMS.txt`.
Copy `update.ptu` to
`pocket-tank/update.ptu` while powered off. Follow steps 3 and 4 below.
For unified firmware on either board, copy **unified-update.ptu** to that same
`pocket-tank/update.ptu` path. For legacy ELEGOO use **elegoo-update.ptu**;
legacy original CYD uses **update.ptu**. Family checks refuse a different
package; SD updates do not perform legacy-to-unified migration.
Artwork is embedded in firmware; no SD icons are needed.
The application-only binary and factory image are not SD update packages.
**Do not factory-flash an existing tank: it overwrites NVS and loses saved
state, credentials, calibration and collection identity.**

**Settings > Device > Firmware Updates** (called **SD Updates** in published
2.3.4 and older) offers SD and GitHub Wi-Fi updates side by side.
The **Check SD Card** option installs a trusted local package from
`pocket-tank/update.ptu` on the SD card without using Wi-Fi.

1. Build the CYD application, then package the application binary (not a
   merged flash image):

   ```powershell
   python tools\make_sd_update.py firmware\build_cyd\pocket_tank.bin --release 1.0.0 --out update.ptu
   ```

2. Copy `update.ptu` into the SD card's `pocket-tank` directory. Alternatively,
   add `--port COM4` to the command above to copy the package through the USB
   console without removing the card. USB copying requires pyserial and
   refuses to overwrite an existing package or interrupted `.upload` file.
   Remove those specific files from the card before another USB copy.
3. Open **Settings > Device > Firmware Updates > Check SD Card**. The device validates the
   package and displays its release label.
4. Tap **Install and Restart** within 60 seconds to confirm. If the offer
   expired or the file changed, the device asks for confirmation again.
   Keep power connected until the tank returns.

The installer validates the package magic, CYD board marker, application
header, exact file length, slot capacity and SHA-256 before any flash write.
It verifies SHA-256 again while writing, and ESP-IDF validates the image
before the boot slot is changed. The running slot is never overwritten.
Missing, truncated, corrupt and wrong-board packages leave it selected.
The package stays on the card after installation; it is never auto-installed
at boot. Reinstallation/downgrades require the same explicit confirmation.

Before installation, the current tank is committed to internal profile storage
(legacy firmware used NVS) and a verified
`before-update-NNNN.json` safety snapshot is created on SD. Installation
stops if either fails. NVS, calibration, SD files and normal tank backups
are not erased. Older firmware may not understand newer saves; use a
compatible backup when deliberately downgrading.
Individual backups cover the active tank and shared wallet, not purchased
slot identity or the entire aquarium collection. In releases through 2.3.3,
also archive the SD tank snapshot directory. From 2.3.4, the firmware
imports those tanks internally and does not refresh the old SD copies: visit
each tank to make current JSON backups and preserve a full-flash recovery
image for the complete internal collection. Avoid erasing NVS/save storage.

**Trust model:** these are local, unsigned packages. SHA-256 detects corruption,
not malicious modifications. Only install packages you trust. SD installation
is a separate, explicitly local path; standard Wi-Fi releases use the trusted
HTTPS/manifest model described below, not publisher signatures.

The package format is an 84-byte little-endian header followed by the
application: magic `PTCYDSD1` (8 bytes), board (24-byte NUL-padded ASCII),
application size (uint32), SHA-256 (32 bytes), release label (16-byte
NUL-padded printable ASCII). The release label is informational, not an
authenticated version claim. The serial `sd update` command performs the
same check/confirm operation as the button.

## On-screen Wi-Fi for the clock

**Settings > WI-FI / SYNC** saves the aquarium and briefly
restarts into a Wi-Fi-only version of the existing network picker and password
keyboard. It can connect, change, or confirm forgetting a device-wide saved
network. Connection and credential-save errors are visible; cancel leaves
the tank untouched. It never checks an update manifest or downloads firmware.
On returning to the tank, boot-time NTP synchronizes the optional clock and
switches the radio off. After power loss a fresh sync is needed; without it,
the overlay says **SYNC NEEDED**. See [clock setup and time limitations](../firmware/CYD.md#optional-tank-clock-and-on-screen-wi-fi-setup).

## GitHub Wi-Fi updates (2.3.2 and newer)

Install 2.3.2 once by save-preserving USB or SD update to enable the default
channel. Earlier releases had no default Wi-Fi firmware channel.
Prefer 2.3.3 or newer for that bootstrap: it frees aquarium-only rendering
scratch in update mode and delays the manifest buffer until after TLS,
fixing insufficient-memory certificate verification on GitHub's asset host.
The aquarium simulation and SD services do not run during the download;
certificate and image verification are never disabled to recover memory.

1. Make an SD backup if available and keep USB power connected.
2. Open **Settings > Device > Firmware Updates > Check GitHub via Wi-Fi**.
   The page retains its existing SD entry for compatibility; the Wi-Fi button
   is separate from **Check SD Card / Install and Restart**.
3. The aquarium must save successfully before restarting into update mode.
   Choose a network if needed, or let the saved network connect.
4. The board checks the latest stable release from this fork. An equal or
   older version reports **UP TO DATE**. A newer compatible version shows
   an offer; only **UPDATE** starts the download. **NOT NOW** returns to care.
5. Keep power connected through verification and restart.

The stable channel is
`https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/ota.json`.
The ELEGOO build instead defaults to
`https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest/download/elegoo-ota.json`.
Both channels enforce their distinct board marker.
Each release workflow packages this small manifest alongside the application.
It pins the download URL to that exact release tag, never `main` or an
unversioned app URL, so a changing latest release cannot mix version and image.
Only the app binary is downloaded, never a factory image.

The format-1 manifest requires `fork: pocket-tank-cyd`, the exact CYD board,
a bounded three-part `release`, and `app` with HTTPS `url`, integer `size`
and 64-hex-digit `sha256`. Optional fields are matching `release_num`, `build`,
`min_from`, `note` and `needs_cable`. Unsupported, incomplete, oversized and
wrong-board manifests fail explicitly. HTTPS manifest redirects are bounded
and cannot downgrade to HTTP; image downloads also require HTTPS.

Before selecting the inactive slot, the updater checks complete reception,
exact manifest size, the image board marker, embedded application version,
and SHA-256 of the entire downloaded asset. ESP-IDF then validates the image
and selects it for boot. Cancellation, read/hash failure, a mismatched digest
or an invalid image aborts without selecting that slot. Existing A/B boot
confirmation/rollback is retained; no save/partition erase occurs.

**Trust model:** standard release images are not publisher-signed. HTTPS
certificate validation authenticates the release host, and the manifest hash
detects mismatched/corrupt assets; it does not protect against a compromised
release account publishing both a malicious image and its matching hash.
This is the same trusted-release model as USB/SD downloads, not secure boot.
Do not disable TLS verification or claim SHA-256 is a digital signature.
The optional `make_ota_manifest.py` tool still requires signed images for
separately managed signing deployments; it is not the standard release path.

The serial `ota check` command opens the same flow. `ota url <https-url>`
sets a deliberate channel override; `ota url default` removes it and restores
the GitHub channel. Never point it at upstream firmware or the website's
`update.json`: that separate file describes the USB installer, not Wi-Fi OTA.

## Publishing CYD releases

Push `main`, then a new immutable tag matching `vMAJOR.MINOR.PATCH`,
for example `v1.0.0`. The label without `v` must fit 15 characters.
The [release workflow](../.github/workflows/release.yml) builds with ESP-IDF
v6.1, runs Python/native/emulator checks, and publishes a normal GitHub release
marked Latest, making the built downloads available at
[Latest release](https://github.com/jeromedontdev/pocket-tank-cyd/releases/latest).
It includes the bundle, app-only image, new-board factory image, SD package,
install instructions, license and SHA-256 checksums. No separate icon bank is
distributed: artwork is embedded in the firmware.
Normal branch pushes run checks without creating a release.

Pocket Tank CYD uses its own semantic version starting at 1.0.0; the firmware,
SD update label and release tag agree. SD package labels are
informational, not signed version claims. Do not overwrite old tags or assets.
See [the hardware guide](../firmware/CYD.md#downloadable-releases) for local
packaging instructions and the factory-image warning.
