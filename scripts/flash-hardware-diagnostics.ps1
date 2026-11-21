param(
    [string]$BuildPath = (Join-Path $PSScriptRoot '../software/build'),
    [string]$OpenOcdPath = 'openocd',
    [ValidateRange(100, 40000)][int]$AdapterSpeedKHz = 5000,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$buildDirectory = (Resolve-Path -LiteralPath $BuildPath).Path.Replace('\', '/')
if ($buildDirectory -match '[{}]') { throw 'Build path cannot contain Tcl braces.' }
$argumentsFile = Join-Path $buildDirectory 'flasher_args.json'
$flash = Get-Content -Raw -Encoding UTF8 -LiteralPath $argumentsFile | ConvertFrom-Json
if ($flash.extra_esptool_args.chip -ne 'esp32s31') { throw 'Expected an ESP32-S31 build.' }
foreach ($entry in $flash.flash_files.PSObject.Properties) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory $entry.Value) -PathType Leaf)) {
        throw "Missing firmware image: $($entry.Value)"
    }
}
$openocd = (Get-Command $OpenOcdPath -CommandType Application -ErrorAction Stop).Source
$arguments = @('-f', 'board/esp32s31-builtin.cfg', '-c', "adapter speed $AdapterSpeedKHz", '-c', "program_esp_bins {$buildDirectory} flasher_args.json verify reset exit")
Write-Output "Flashing ESP32-S31 over builtin USB-JTAG at $AdapterSpeedKHz kHz: $buildDirectory"
if ($DryRun) { Write-Output ($openocd + ' ' + ($arguments -join ' ')); return }
& $openocd @arguments
if ($LASTEXITCODE -ne 0) { throw "USB-JTAG flash failed (exit $LASTEXITCODE). Do not boot a partly written image. Close debug/monitor sessions, re-enter ROM download mode, then retry or use flash-firmware.ps1 -Transport Serial -Port COM4." }
