[CmdletBinding(SupportsShouldProcess = $true)]
param(
    # Default is the unversioned personal LUA plugins directory, not plugins/4.x.
    [string]$PluginDirectory = (Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Wireshark/plugins'),
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../tools/wireshark/sigmadsp.lua')).Path
$directory = [IO.Path]::GetFullPath($PluginDirectory)
$destination = Join-Path $directory 'sigmadsp.lua'
function FileHash([string]$file) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($file))).Replace('-','') }
    finally { $sha.Dispose() }
}
$sourceHash = FileHash $source
if (Test-Path -LiteralPath $destination) {
    if ((FileHash $destination) -eq $sourceHash) {
        Write-Output "Already installed: $destination"
        return
    }
    if (-not $Force) { throw "A different sigmadsp.lua exists at $destination. Review it first; use -Force only to explicitly replace that file." }
}
if ($PSCmdlet.ShouldProcess($destination, 'Install passive SigmaStudio+ Wireshark Lua dissector')) {
    if (-not (Test-Path -LiteralPath $directory)) { New-Item -ItemType Directory -Path $directory -Force | Out-Null }
    Copy-Item -LiteralPath $source -Destination $destination -Force:$Force
    if ((FileHash $destination) -ne $sourceHash) { throw 'Installed hash differs from source.' }
    Write-Output "Installed: $destination"
    Write-Output "SHA256: $sourceHash"
    Write-Output 'Restart Wireshark or use Analyze > Reload Lua Plugins (if supported). No capture, board connection or firmware change was performed.'
}
