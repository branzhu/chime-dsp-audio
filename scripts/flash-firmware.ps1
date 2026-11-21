param(
    [string]$BuildPath = (Join-Path $PSScriptRoot '../software/build'),
    [string]$OpenOcdPath = 'C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260703\openocd-esp32\bin\openocd.exe',
    [ValidateSet('Jtag', 'Serial')][string]$Transport = 'Jtag',
    [ValidateRange(100, 40000)][int]$AdapterSpeedKHz = 5000,
    [string]$Port = 'COM4',
    [ValidateRange(9600, 2000000)][int]$Baud = 115200,
    [string]$PythonPath = 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildPath).Path
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $build 'audio_dsp_firmware.bin')
Write-Warning 'Bluetooth SBC + USB automatic source: A2DP connected selects BT, disconnected returns USB. Bonded phone reconnect/playback can enable amps. Factory partition is now 3MiB at 0x10000; flash the matching partition table, NOT app-only over the old 1500KiB table. NVS/PHY offsets unchanged; do NOT erase credentials/bonds. Bluetooth/SRC/pop-free switching are NOT hardware-accepted. Start with low phone AND PC volume. Flashing default Chime firmware: built-in DSP + USB Audio + provisioning/Web/SS+ bridge. USB host playback can enable LR and SUB amps after guards; DSP board-bound-v4 (compiler-imported processing, name-bound gain addresses/values, routing preserves attenuation), fixed USB 1/8 attenuation REMOVED and amp DIG_VOL=0x30 (digital 0dB), AGAIN=0x10 (analog -8dB); preserve compiled DSP gains. USB fixed attenuation removal raises level: reduce host level by at least 18dB and start low before playback. Default L+R+Sub; SUB uses two independent BTL channels (two speakers; NO external output paralleling) with the compiled project gain, not a forced unity gain; start SUB tests at low level. SS downloads need manual Web audition approval; every SS write stops audition; disconnect retains external RAM; unknown external Safeload rejected. firmware does not certify DSP filtering, subsonic or power protection; low-level tests only. No EEPROM operations.'
if ($Transport -eq 'Jtag') {
    & (Join-Path $PSScriptRoot 'flash-hardware-diagnostics.ps1') -BuildPath $build -OpenOcdPath $OpenOcdPath -AdapterSpeedKHz $AdapterSpeedKHz -DryRun:$DryRun
    return
}
# Use the generated manifest, not hard-coded partition offsets. No erase_flash,
# no automatic transport switch/retry: a failed write may already have changed flash.
$flash = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $build 'flasher_args.json') | ConvertFrom-Json
if ($flash.extra_esptool_args.chip -ne 'esp32s31') { throw 'Expected an ESP32-S31 build.' }
$python = (Get-Command $PythonPath -CommandType Application -ErrorAction Stop).Source
$arguments = @('-m', 'esptool', '--chip', 'esp32s31', '--port', $Port, '--baud', "$Baud", '--before', 'default-reset', '--after', 'hard-reset', '--no-stub', 'write-flash',
    '--flash-mode', [string]$flash.flash_settings.flash_mode, '--flash-size', [string]$flash.flash_settings.flash_size, '--flash-freq', [string]$flash.flash_settings.flash_freq)
if (-not $flash.flash_files.PSObject.Properties.Count) { throw 'No flash images in manifest.' }
foreach ($entry in $flash.flash_files.PSObject.Properties) {
    $image = (Resolve-Path -LiteralPath (Join-Path $build $entry.Value)).Path
    $arguments += @([string]$entry.Name, $image)
}
Write-Output "Flashing ESP32-S31 over ROM serial $Port at $Baud baud (no stub). Close monitor first."
if ($DryRun) { Write-Output ($python + ' ' + ($arguments -join ' ')); return }
& $python @arguments
if ($LASTEXITCODE -ne 0) { throw "ROM serial flash failed, exit code: $LASTEXITCODE. Keep device in download mode and preserve the full log; do not boot a partly written image." }
