param(
    [string]$Port = 'COM4',
    [switch]$Flash,
    [ValidateSet('auto', 'ota_0', 'ota_1')]
    [string]$AppSlot = 'auto'
)
$ErrorActionPreference = 'Stop'
if (-not $env:IDF_PATH) { throw 'Run this script from an ESP-IDF PowerShell terminal.' }
$firmware = Join-Path (Split-Path $PSScriptRoot -Parent) 'firmware'
Push-Location $firmware
try {
    & cmake -S . -B build_cyd -G Ninja '-DSDKCONFIG=build_cyd\sdkconfig' '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults'
    if ($LASTEXITCODE -ne 0) { throw 'CYD configure failed.' }
    & ninja -C build_cyd
    if ($LASTEXITCODE -ne 0) { throw 'CYD build failed.' }
    if ($Flash) {
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
        Write-Host "Flashing $AppSlot at $offset (NVS and SD preserved)"
        # App-only upgrade: preserve the existing partition table, NVS and SD.
        & python -m esptool --chip esp32 --port $Port --baud 460800 write-flash $offset build_cyd\pocket_tank.bin
        if ($LASTEXITCODE -ne 0) { throw 'CYD flash failed.' }
    }
} finally {
    Pop-Location
}
