param(
    [string]$InstallPath = 'C:\Analog Devices\SigmaStudioPlus-Rel3.4.0',
    [string]$PluginPath = 'C:\ProgramData\Analog Devices\SigmaStudio 3.4\Plugins',
    [Parameter(Mandatory)][string]$OutputPath
)

# Read-only vendor inspection: no plugin constructors, methods, sockets or GUI.
$ErrorActionPreference = 'Stop'
$outputFile = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $outputFile) { throw "Refusing to overwrite: $outputFile" }
if ($PSVersionTable.PSEdition -eq 'Core') {
    # ReflectionOnlyLoadFrom is a .NET Framework metadata API, not available in Core.
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -InstallPath $InstallPath -PluginPath $PluginPath -OutputPath $outputFile
    if ($LASTEXITCODE -ne 0) { throw 'SigmaStudio+ metadata inspection failed.' }
    return
}
$InstallPath = (Resolve-Path -LiteralPath $InstallPath).Path
$PluginPath = (Resolve-Path -LiteralPath $PluginPath).Path
$hostPath = Join-Path $InstallPath 'Host'
$files = @(
    (Join-Path $InstallPath 'AE_46_SSPlus_ReleaseNotes.pdf'),
    (Join-Path $InstallPath 'product.xml'),
    (Join-Path $hostPath 'SigmaStudioPlus.exe'),
    (Join-Path $hostPath 'TypeDefinitions.dll'),
    (Join-Path $PluginPath 'TCPIPChannels.dll'),
    (Join-Path $PluginPath 'TCPIPDriver.dll'),
    (Join-Path $PluginPath 'ADAU145x.dll'),
    (Join-Path $PluginPath 'ADAU145XCompiler.dll')
)
$inventory = foreach ($file in $files) {
    $item = Get-Item -LiteralPath $file
    $assemblyVersion = $null
    if ($item.Extension -in '.exe', '.dll') {
        $assemblyVersion = [Reflection.AssemblyName]::GetAssemblyName($item.FullName).Version.ToString()
        $expectedVersion = if ($item.Name -eq 'SigmaStudioPlus.exe') { '3.4.9650.21121' } else { '3.4.0.0' }
        if ($assemblyVersion -ne $expectedVersion) { throw "Not the inspected 3.4.0 assembly baseline: $file ($assemblyVersion)" }
    }
    [ordered]@{
        name = $item.Name; path = $item.FullName; bytes = $item.Length
        fileVersion = $item.VersionInfo.FileVersion; assemblyVersion = $assemblyVersion
        sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$resolve = [ResolveEventHandler]{
    param($sender, $event)
    $name = ([Reflection.AssemblyName]$event.Name).Name + '.dll'
    foreach ($directory in @($hostPath, $PluginPath)) {
        $candidate = Join-Path $directory $name
        if (Test-Path -LiteralPath $candidate) { return [Reflection.Assembly]::ReflectionOnlyLoadFrom($candidate) }
    }
    return [Reflection.Assembly]::ReflectionOnlyLoad($event.Name)
}
[AppDomain]::CurrentDomain.add_ReflectionOnlyAssemblyResolve($resolve)
try {
    $metadata = foreach ($name in @('TCPIPChannels.dll', 'TCPIPDriver.dll', 'ADAU145x.dll')) {
        $assembly = [Reflection.Assembly]::ReflectionOnlyLoadFrom((Join-Path $PluginPath $name))
        foreach ($type in $assembly.GetTypes() | Where-Object { $_.FullName -like 'AnalogDevices.*' }) {
            [ordered]@{
                assembly = $name; type = $type.FullName
                declaredPublicMethods = @($type.GetMethods([Reflection.BindingFlags]'Public,Instance,Static,DeclaredOnly') | ForEach-Object { $_.ToString() } | Sort-Object)
            }
        }
    }
} finally {
    [AppDomain]::CurrentDomain.remove_ReflectionOnlyAssemblyResolve($resolve)
}
$result = [ordered]@{
    schema = 'audio-dsp.sigmastudio-plus-inventory.v1'
    collectedAt = (Get-Date).ToUniversalTime().ToString('o')
    target = 'SigmaStudio+ 3.4.0'; installPath = $InstallPath; pluginPath = $PluginPath
    scope = 'File identity and reflection-only metadata. No vendor code execution, GUI, wire capture, project compilation or hardware validation.'
    compatibility = 'NOT_VERIFIED'; files = @($inventory); metadata = @($metadata)
}
$parent = Split-Path -Parent $outputFile
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 10) + "`n")
$stream = [IO.File]::Open($outputFile, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
Write-Output "Wrote metadata inventory only (not compatibility evidence): $outputFile"
