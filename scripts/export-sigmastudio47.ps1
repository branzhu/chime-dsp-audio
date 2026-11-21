param(
    [string]$InstallPath = 'C:\Program Files\Analog Devices\SigmaStudio 4.7',
    [Parameter(Mandatory)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$outputFile = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.IntPtr]::Size -ne 4) {
    $powershell32 = Join-Path $env:SystemRoot 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
    & $powershell32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -InstallPath $InstallPath -OutputPath $outputFile
    if ($LASTEXITCODE -ne 0) { throw '32-bit SigmaStudio inspection failed.' }
    return
}

if (Test-Path -LiteralPath $outputFile) { throw "Refusing to overwrite: $outputFile" }
$installDirectory = (Resolve-Path -LiteralPath $InstallPath).Path
$files = foreach ($name in 'SStudio.exe', 'TCPIP.dll', 'ADAU145X.dll', 'Analog.SigmaStudioServer.dll') {
    $file = Get-Item -LiteralPath (Join-Path $installDirectory $name)
    if ($file.VersionInfo.FileVersion -notmatch '^4\.07\.') {
        throw "Not a SigmaStudio 4.7 file: $name ($($file.VersionInfo.FileVersion))"
    }
    [ordered]@{
        name = $name
        fileVersion = $file.VersionInfo.FileVersion
        productVersion = $file.VersionInfo.ProductVersion
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$assembly = [System.Reflection.Assembly]::LoadFrom((Join-Path $installDirectory 'TCPIP.dll'))
$binding = [System.Reflection.BindingFlags]'Public,NonPublic,Instance,Static,DeclaredOnly'
$interfaces = foreach ($name in 'TCPIP.ADAU1452EB_TCPIP', 'TCPIP.TCPIPADAU1452') {
    $type = $assembly.GetType($name, $true)
    [ordered]@{
        type = $type.FullName
        methods = @($type.GetMethods($binding) | Sort-Object Name, MetadataToken | ForEach-Object {
            [ordered]@{
                signature = $_.ToString()
                parameters = @($_.GetParameters() | ForEach-Object {
                    [ordered]@{ name = $_.Name; type = $_.ParameterType.FullName }
                })
            }
        })
    }
}

$result = [ordered]@{
    schema = 'audio-dsp.sigmastudio-installation.v1'
    collectedAt = [DateTime]::UtcNow.ToString('o')
    target = 'SigmaStudio 4.7 classic'
    installPath = $installDirectory
    inspectionProcessBits = 32
    operatingSystem = [Environment]::OSVersion.VersionString
    files = @($files)
    interfaces = @($interfaces)
    evidenceScope = 'Installed file versions, SHA-256 and reflected API only. No DSP project, wire protocol or hardware behavior verified.'
}
$parentDirectory = [System.IO.Path]::GetDirectoryName($outputFile)
[System.IO.Directory]::CreateDirectory($parentDirectory) | Out-Null
$json = ($result | ConvertTo-Json -Depth 10).Replace("`r`n", "`n") + "`n"
[System.IO.File]::WriteAllText($outputFile, $json, [System.Text.UTF8Encoding]::new($false))
Write-Output "Exported SigmaStudio 4.7 installation evidence: $outputFile"
