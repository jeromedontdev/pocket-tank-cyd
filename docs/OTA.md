# CYD fork updates

## Offline SD updates

Prebuilt packages are available from
[CYD Releases](https://github.com/jeromedontdev/pocket-tank-cyd/releases).
Download the bundle, read `INSTALL.txt`, and verify `SHA256SUMS.txt`.
Copy `update.ptu` to
`pocket-tank/update.ptu` while powered off. Follow steps 3 and 4 below.
Artwork is embedded in firmware; no SD icons are needed.
The application-only binary and factory image are not SD update packages.
**Do not factory-flash an existing tank: it overwrites NVS and loses saved
state, credentials, calibration and collection identity.**

**Settings > Updates** now installs a trusted local package from
`pocket-tank/update.ptu` on the SD card. Wi-Fi is not used.

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
3. Open **Settings > Updates > Check SD Card**. The device validates the
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

Before installation, the current tank is saved to NVS and a verified
`before-update-NNNN.json` safety snapshot is created on SD. Installation
stops if either fails. NVS, calibration, SD files and normal tank backups
are not erased. Older firmware may not understand newer saves; use a
compatible backup when deliberately downgrading.
Individual backups cover the active tank and shared wallet, not purchased
slot identity or the entire aquarium collection. Also archive the SD tank
snapshot directory and avoid erasing NVS.

**Trust model:** these are local, unsigned packages. SHA-256 detects corruption,
not malicious modifications. Only install packages you trust. The existing
signed HTTPS update policy is not relaxed: this is a separate, explicitly
local installation path.

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

## Legacy Wi-Fi firmware channel (serial only)

The default manifest URL is empty. Checking for firmware updates can still
scan, provision and connect Wi-Fi, but without a configured channel it reports
**NO UPDATE CHANNEL** rather than contacting upstream releases. Publishing GitHub releases does not enable this signed Wi-Fi channel.
SD installation and active-slot USB upgrades remain the supported methods.
The legacy Wi-Fi flow remains available through the serial command
`ota check`, not through Settings.

To opt in later:

1. Establish a release host for this fork, not the upstream Waveshare project.
2. Configure `POCKET_TANK_OTA_URL` in ESP-IDF menuconfig, or set a deliberate
   serial override with `ota url https://your-host/latest-cyd2432s028.json`.
   `ota url default` removes the override; the default is empty.
3. Set up and validate ESP-IDF image signing for the existing ESP32 partition
   layout. Never check a private signing key into source control. The current
   USB build is not a verified OTA signing/installation setup.
4. Package a signed image with:

   ```powershell
   python tools\make_ota_manifest.py --base-url https://your-host/releases/v0.3.9 --out ota
   ```

The packager refuses unsigned images, non-CYD markers, invalid release
numbers, non-HTTPS hosts and images larger than the existing 0x1e0000 app slot.
The version comes from `common/version.h`.

The firmware requires `"fork": "pocket-tank-cyd"` in the manifest as well as
the existing CYD board ID and image marker checks. Stock upstream manifests
therefore cannot be offered, even through a stale saved URL override.
There is no model partition, model version negotiation or model download.

Manifest fields: `fork`, `board`, `release`, `release_num`, optional `build`,
`min_from`, `note`, `needs_cable`, and `app` (`url`, `size`, `sha256`).
Existing image verification and rollback protections remain in place.

End-to-end Wi-Fi connection, signed OTA installation and rollback still need
hardware validation before publishing a channel. Do not bypass verification
or replace signing failures with unsigned downloads.

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
