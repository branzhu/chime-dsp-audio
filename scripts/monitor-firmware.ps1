param(
    [string]$Port = 'COM4',
    [string]$BuildPath = (Join-Path $PSScriptRoot '../software/build'),
    [string]$PythonPath = 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe',
    [string]$IdfPath = 'C:\esp\v6.1\esp-idf'
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildPath).Path
$elf = (Resolve-Path -LiteralPath (Join-Path $build 'audio_dsp_firmware.elf')).Path
$monitor = (Resolve-Path -LiteralPath (Join-Path $IdfPath 'tools/idf_monitor.py')).Path
$python = (Resolve-Path -LiteralPath $PythonPath).Path
$env:PYTHONUTF8 = '1'
Write-Output 'Monitor uses --no-reset. Once connected, press board RESET (not BOOT) to print startup and setup AP information. Ctrl+] exits.'
& $python $monitor -p $Port -b 115200 --no-reset --target esp32s31 --toolchain-prefix riscv32-esp-elf- $elf
if ($LASTEXITCODE -ne 0) { throw "Monitor exited with code $LASTEXITCODE." }
