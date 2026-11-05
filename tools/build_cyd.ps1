# build_cyd.ps1 - the one build entry point for all three board families.
#
#   .\tools\build_cyd.ps1                          # build the CYD image
#   .\tools\build_cyd.ps1 -Board elegoo            # build the ELEGOO image
#   .\tools\build_cyd.ps1 -Board unified           # build the shared-profile image
#   .\tools\build_cyd.ps1 -Flash -Port COM4        # build, then app-only flash
#
# Requires an ESP-IDF PowerShell terminal (IDF_PATH set); it throws otherwise.
# Each board gets its own build_<board> directory and sdkconfig, so the three
# never share stale configuration.
#
# Flashing is app-only and deliberately conservative. It queries the running
# slot over serial and refuses when the pending boot slot differs, when the
# installed board marker does not match the built image, or when the slot cannot
# be detected. NVS, the partition table and SD are never rewritten, so saves and
# the OTA selection survive. Never assume ota_0 (0x20000) versus ota_1
# (0x200000) by hand.
#
# -Board unified additionally requires -HardwareProfile cyd|elegoo and a new
# -Backup file, and delegates to flash_unified.py, which takes a full 4 MiB dump
# and verifies readback. There is no migration bypass and no explicit-slot
# shortcut for unified images.
#
# -AllowBoardMigration -AppSlot ota_0|ota_1 is the deliberate local recovery path
# for converting a board's identity. It is not a general cross-board bypass: it
# still demands a full-flash backup and an explicit slot, and it warns loudly.
# Factory flashing is destructive and is never a fallback for a refused update.
param(
    [ValidateSet('cyd', 'elegoo', 'unified')]
    [string]$Board = 'cyd',
    [string]$Port = 'COM4',
    [switch]$Flash,
    [switch]$AllowBoardMigration,
    [ValidateSet('cyd', 'elegoo')]
    [string]$HardwareProfile,
    [string]$Backup,
    [ValidateSet('auto', 'ota_0', 'ota_1')]
    [string]$AppSlot = 'auto'
)
$ErrorActionPreference = 'Stop'
if (-not $env:IDF_PATH) { throw 'Run this script from an ESP-IDF PowerShell terminal.' }
if ($Board -eq 'unified' -and $Flash -and
    (-not $HardwareProfile -or -not $Backup -or $AppSlot -ne 'auto' -or $AllowBoardMigration)) {
    throw 'Unified flashing requires -HardwareProfile cyd|elegoo and -Backup NEW_FILE. Slot detection and family validation are mandatory; no migration bypass or explicit slot is accepted.'
}
$backupPath = if ($Backup) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Backup) }
$firmware = Join-Path (Split-Path $PSScriptRoot -Parent) 'firmware'
$build = "build_$Board"
Push-Location $firmware
try {
    & cmake -S . -B $build -G Ninja "-DSDKCONFIG=$build\sdkconfig" '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults' "-DPT_BOARD_VARIANT=$Board"
    if ($LASTEXITCODE -ne 0) { throw 'CYD configure failed.' }
    & ninja -C $build
    if ($LASTEXITCODE -ne 0) { throw 'CYD build failed.' }
    if ($Flash) {
        if ($Board -eq 'unified') {
            & python (Join-Path $PSScriptRoot 'flash_unified.py') --port $Port --hardware $HardwareProfile --image "$build\pocket_tank.bin" --backup $backupPath
            if ($LASTEXITCODE -ne 0) { throw 'Unified app-only update failed; inspect recovery output.' }
            return
        }
        if ($AllowBoardMigration -and $AppSlot -eq 'auto') {
            throw 'Deliberate board recovery requires an explicit -AppSlot and a full-flash backup. No flash written.'
        }
        if ($AppSlot -eq 'auto') {
            $query = @'
import re, sys, time
import serial
p = serial.Serial()
p.port, p.baudrate, p.timeout = sys.argv[1], 115200, 0.2
p.dtr = p.rts = False
try:
    p.open()
    p.reset_input_buffer()
    p.write(b"ota\n")
    end = time.monotonic() + 8
    while time.monotonic() < end:
        line = p.readline().decode("utf-8", "replace")
        match = re.search(r"running from (ota_[01]), next boot (ota_[01])", line)
        if match:
            if match[1] != match[2]:
                raise RuntimeError("Pending boot slot differs; reboot before USB flashing")
            print(match[1])
            break
    else:
        raise RuntimeError("Cannot detect active slot; use -AppSlot ota_0 or ota_1 only for deliberate recovery")
finally:
    p.close()
'@
            $AppSlot = & python -c $query $Port
            if ($LASTEXITCODE -ne 0 -or $AppSlot -notin @('ota_0', 'ota_1')) { throw 'Could not detect CYD application slot; no flash written.' }
        }
        $offset = if ($AppSlot -eq 'ota_1') { '0x200000' } else { '0x20000' }
        $markerOffset = if ($AppSlot -eq 'ota_1') { '0x200120' } else { '0x20120' }
        $markerFile = [IO.Path]::GetTempFileName()
        try {
            & python -m esptool --chip esp32 --port $Port --baud 460800 read-flash $markerOffset 32 $markerFile
            if ($LASTEXITCODE -ne 0) { throw 'Cannot read installed board identity; no flash written.' }
            $checkBoard = @'
import pathlib, sys
sys.path.insert(0, sys.argv[1])
from board_profiles import BOARDS
profile = BOARDS[sys.argv[2]]
installed = pathlib.Path(sys.argv[3]).read_bytes()
image = pathlib.Path(sys.argv[4]).read_bytes()
if image[0x120:0x140] != profile.marker:
    raise RuntimeError("Built application does not match selected board; no flash written")
if installed != profile.marker:
    if sys.argv[5] != "True" or installed not in [b.marker for b in BOARDS.values()]:
        raise RuntimeError("Wrong/unknown installed board. Deliberate app-only conversion requires a full-flash backup, -AllowBoardMigration and an explicit -AppSlot")
    print("WARNING: explicitly authorized board identity conversion; app only")
'@
            & python -c $checkBoard $PSScriptRoot $Board $markerFile "$build\pocket_tank.bin" $AllowBoardMigration.IsPresent
            if ($LASTEXITCODE -ne 0) { throw 'Board verification failed; no flash written.' }
        } finally {
            Remove-Item -LiteralPath $markerFile
        }
        Write-Host "Flashing $AppSlot at $offset (NVS and SD preserved)"
        # App-only upgrade: preserve the existing partition table, NVS and SD.
        & python -m esptool --chip esp32 --port $Port --baud 460800 write-flash $offset "$build\pocket_tank.bin"
        if ($LASTEXITCODE -ne 0) { throw 'CYD flash failed.' }
    }
} finally {
    Pop-Location
}
