param(
    [string]$InstallPath = 'C:\Analog Devices\SigmaStudioPlus-Rel3.4.0',
    [string]$PluginPath = 'C:\ProgramData\Analog Devices\SigmaStudio 3.4\Plugins',
    [Parameter(Mandatory)][string]$OutputPath,
    [switch]$Worker
)
$ErrorActionPreference = 'Stop'
$outputFile = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $outputFile) { throw "Refusing to overwrite: $outputFile" }
if (-not $Worker) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $outputFile) -Force | Out-Null
    $exe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $stdout = $outputFile + '.stdout.log'; $stderr = $outputFile + '.stderr.log'
    if ((Test-Path -LiteralPath $stdout) -or (Test-Path -LiteralPath $stderr)) { throw 'Refusing to overwrite test logs.' }
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),
        '-InstallPath',('"'+$InstallPath+'"'),'-PluginPath',('"'+$PluginPath+'"'),
        '-OutputPath',('"'+$outputFile+'"'),'-Worker')
    $child = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    # Keep the process handle so Windows PowerShell retains the exit code.
    $null = $child.Handle
    if (-not $child.WaitForExit(30000)) {
        $child.Kill(); $child.WaitForExit()
        throw 'Native driver test timed out; only the spawned test process was terminated.'
    }
    $child.WaitForExit() # Drain redirected stdout/stderr before inspecting results.
    if ($child.ExitCode -ne 0) { throw "Native test failed. Inspect $stderr" }
    Write-Output "Native 3.4.0 loopback test complete: $outputFile (not GUI/hardware validation)"
    return
}
$hostPath = Join-Path (Resolve-Path -LiteralPath $InstallPath).Path 'Host'
$PluginPath = (Resolve-Path -LiteralPath $PluginPath).Path
$driverFile = Join-Path $PluginPath 'TCPIPDriver.dll'
$typesFile = Join-Path $hostPath 'TypeDefinitions.dll'
function FileHash([string]$file) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($file))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
$driverHash = FileHash $driverFile
$typesHash = FileHash $typesFile
if ($driverHash -ne '77ba8dfeb8364781ef5efa5a66efc2e09067c3751f4af0621c46f884eff0aca5' -or
    $typesHash -ne '80a1513f41167a99325d15b0f9e2627eec185577915d5af32a6887ea433dbb09') {
    throw 'Unverified native DLL; inspect the installation before changing this test.'
}
[AppDomain]::CurrentDomain.add_AssemblyResolve({
    param($sender,$event)
    $name = ([Reflection.AssemblyName]$event.Name).Name + '.dll'
    foreach ($directory in @($hostPath,$PluginPath)) {
        $candidate = Join-Path $directory $name
        if (Test-Path -LiteralPath $candidate) { return [Reflection.Assembly]::LoadFrom($candidate) }
    }
    return $null
})
[Reflection.Assembly]::LoadFrom($typesFile) | Out-Null
$assembly = [Reflection.Assembly]::LoadFrom($driverFile)
$type = $assembly.GetType('AnalogDevices.Plugins.Communication.TCPIP.TCPIPHWDriver',$true)
$netstandard = Join-Path $env:SystemRoot 'Microsoft.NET\assembly\GAC_MSIL\netstandard\v4.0_2.0.0.0__cc7b13ffcd2ddd51\netstandard.dll'
Add-Type -Path (Join-Path $PSScriptRoot 'sigmastudio-plus-wire-harness.cs') -ReferencedAssemblies @($typesFile,$driverFile,$netstandard)
$cases = [Collections.Generic.List[object]]::new()
$sessions = [Collections.Generic.List[object]]::new()
function Hex([byte[]]$bytes) { return [BitConverter]::ToString($bytes).Replace('-','').ToLowerInvariant() }
foreach ($trial in @(@{ reverse=$false; chip=0x70; channel=0 },@{ reverse=$true; chip=0x70; channel=0 },@{ reverse=$false; chip=0x38; channel=3 },@{ reverse=$false; chip=0; channel=0 },@{ reverse=$false; chip=0; channel=0; register=$true })) {
    Write-Output ("Trial chip={0} channel={1} reversed={2}" -f $trial.chip,$trial.channel,$trial.reverse)
    $server = [SigmaPlusWireHarness]::new($trial.reverse,[bool]$trial.register)
    $driver = [Activator]::CreateInstance($type,$true)
    try {
        $driver.TCPIPAddress = [Net.IPAddress]::Loopback
        $driver.Port = $server.Port; $driver.DriverPlacement = $trial.channel
        $driver.InitChannel(0,0,$trial.chip,[AnalogDevices.TypeDefinitions.Package.EPartProtocol]::I2C,$false)
        $driver.RegisterAsyncReadCallback([Delegate]::CreateDelegate([AnalogDevices.Plugins.Communication.TCPIP.AsyncReadEventHandler],$server,'OnAsyncRead'))
        if (-not $driver.DoCommunicationInit()) { throw 'Native connection failed.' }
        foreach ($mode in $(if($trial.register){@('ReadRequest')}else{@('MemWrite','Safeload','ReadRequest')})) {
            $pack = [AnalogDevices.TypeDefinitions.Package.TargetPackage]::new()
            $pack.Address = 0x1234; $pack.AddressByteLength = 2; $pack.RegisterByteLength = 2; $pack.DataWidth = 4
            if($trial.register){$pack.Address=0xf000;$pack.DataWidth=2}
            $pack.Protocol = [AnalogDevices.TypeDefinitions.Package.EPartProtocol]::I2C
            $pack.Mode = [Enum]::Parse([AnalogDevices.TypeDefinitions.Package.EPackageMode],$mode)
            $pack.Values = [Collections.Generic.List[byte]]::new()
            foreach ($value in $(if ($trial.register) { [byte[]](0,0) } elseif ($mode -eq 'ReadRequest') { [byte[]](0,0,0,0) } else { [byte[]](1,2,3,4) })) { $pack.Values.Add($value) }
            if ($mode -eq 'ReadRequest') { $driver.Read($pack) | Out-Null }
            elseif (-not $driver.Write($pack)) { throw "Native $mode failed." }
        }
        $deadline = [DateTime]::UtcNow.AddMilliseconds(1500)
        while ([DateTime]::UtcNow -lt $deadline -and -not ($server.HasAcceptedResponse() -or $server.HasAcceptedRegisterResponse())) { Start-Sleep -Milliseconds 10 }
        $accepted = if($trial.register){$server.HasAcceptedRegisterResponse()}else{$server.HasAcceptedResponse()}
        if ($accepted -eq $trial.reverse) { throw 'Native async response acceptance did not match the controlled endian test.' }
        $frames = @($server.Snapshot() | ForEach-Object { Hex $_ })
        $prefix = '1b00400600010101000001'
        $chip = '{0:x2}' -f $trial.chip; $channel = '{0:x2}' -f $trial.channel
        $expected = @($prefix,('0900'+$channel+'00000012'+$chip+'00000004123401020304'),
            ('0901'+$channel+'00000012'+$chip+'00000004123401020304'),('0a0000000e'+$chip+'0000000412340000'))
        if($trial.register){$expected=@($prefix,'0a0000000e0000000002f0000000')}
        if (($frames -join ',') -ne ($expected -join ',')) { throw ('Native frames differ: '+($frames -join ',')) }
        $reply = '0b00000012'+$chip+'00000004'+$(if($trial.reverse){'3412'}else{'1234'})+'000001020304'
        if($trial.register){$reply='0b000000100000000002f00000000060'}
        $sessions.Add([ordered]@{ chipArgument=$trial.chip; channelArgument=$trial.channel; updatedFirmwareArgument=$false
            initChannelArguments=@(0,0,$trial.chip,'I2C',$false); connectionMethod='DoCommunicationInit'
            frames=$frames; mockResponseHex=$reply; responseAccepted=$accepted
            acceptanceEvidence=$(if($trial.register){'Native async callback address=0xF000 and Values=0060; input read Values were zero; chip 00 echoed.'}else{'Native async callback address=0x1234 and Values=01020304; input read Values were all zero. Immediate Read return is NOT acceptance.'}) })
        if ($trial.chip -eq 0x70 -and $trial.channel -eq 0 -and -not $trial.reverse) {
            for($i=1;$i -le 3;$i++){ $cases.Add([ordered]@{ action=@('MemWrite','Safeload','ReadRequest')[$i-1]; hex=$frames[$i] }) }
            $cases.Add([ordered]@{ action='mock response BE';hex=$reply;responseAccepted=$accepted })
        } elseif ($trial.chip -eq 0x70 -and $trial.channel -eq 0) {
            $cases.Add([ordered]@{ action='mock response reversed';hex=$reply;responseAccepted=$accepted })
        }
    } finally {
        $driver.CloseCommunication(); $driver.Dispose(); $server.Dispose()
    }
}
$result = [ordered]@{
    schema='audio-dsp.sigmastudio-plus-wire-test.v1'; collectedAt=[DateTime]::UtcNow.ToString('o')
    target='SigmaStudio+ 3.4.0'; driverSha256=$driverHash; typesSha256=$typesHash
    scope='Public native driver methods and loopback mock only; no GUI/project/download/hardware validation. Connection prefix 0x1B is observed but undocumented and rejected by strict R/W profiles; bench image opt-in exact-capture no-op is a separate compatibility quirk.'
    cases=@($cases); sessions=@($sessions)
}
$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 10)+"`n")
$stream = [IO.File]::Open($outputFile,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
