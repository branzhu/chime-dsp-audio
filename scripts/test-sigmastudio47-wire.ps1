param(
    [string]$InstallPath = 'C:\Program Files\Analog Devices\SigmaStudio 4.7',
    [Parameter(Mandatory)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$outputFile = [IO.Path]::GetFullPath($OutputPath)
if ([IntPtr]::Size -ne 4) {
    $powershell32 = Join-Path $env:SystemRoot 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
    & $powershell32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -InstallPath $InstallPath -OutputPath $outputFile
    if ($LASTEXITCODE -ne 0) { throw 'SigmaStudio native driver wire test failed.' }
    return
}
if (Test-Path -LiteralPath $outputFile) { throw "Refusing to overwrite: $outputFile" }
$dll = Join-Path (Resolve-Path -LiteralPath $InstallPath).Path 'TCPIP.dll'
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne 'c04b854c5fcff1783c1cf00743e96267931d184f3f1e46bc040d18d7f049b07a') {
    throw 'Unverified TCPIP.dll; inspect and record the new version before changing this test.'
}
$assembly = [Reflection.Assembly]::LoadFrom($dll)
$type = $assembly.GetType('TCPIP.ADAU1452EB_TCPIP', $true)
$driver = [Activator]::CreateInstance($type)
$flags = [Reflection.BindingFlags]'Public,NonPublic,Instance'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$client = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork, [Net.Sockets.SocketType]::Stream, [Net.Sockets.ProtocolType]::Tcp)
$client.Connect($listener.LocalEndpoint)
$server = $listener.AcceptSocket()
$server.ReceiveTimeout = 2000
$client.SendTimeout = 2000
$driver.SocWorker = $client
$driver.ChipAddress = 0x70
$driver.AddressByteLength = 2
$buffer = New-Object byte[] 65550
$cases = [Collections.Generic.List[object]]::new()

function Receive-Exact([int]$Length) {
    $offset = 0
    while ($offset -lt $Length) {
        $received = $server.Receive($buffer, $offset, $Length - $offset, [Net.Sockets.SocketFlags]::None)
        if ($received -eq 0) { throw 'Native driver disconnected before the complete frame.' }
        $offset += $received
    }
    return [BitConverter]::ToString($buffer, 0, $Length).Replace('-', '').ToLowerInvariant()
}

try {
    foreach ($safe in $false, $true) {
        $type.GetMethod('WriteData', $flags).Invoke($driver, [object[]]([uint16]0x1234, [byte[]](1, 2, 3, 4), [int]4, $safe)) | Out-Null
        $type.GetMethod('Flush', $flags).Invoke($driver, @()) | Out-Null
        $hex = Receive-Exact 18
        $expected = if ($safe) { '090100000000127000000004123401020304' } else { '090000000000127000000004123401020304' }
        if ($hex -ne $expected) { throw "Native write differs: $hex" }
        $cases.Add([ordered]@{ action = 'WriteData + Flush'; safeArgument = $safe; hex = $hex })
    }
    $type.GetMethod('ReadData', $flags).Invoke($driver, [object[]]([uint16]0x1234, [int]4)) | Out-Null
    # Inspect/send the native read buffer directly: Flush arms a background receive
    # which cannot be joined through this vendor API in a headless test process.
    $nativeBuffer = $type.GetField('databuffer', $flags).GetValue($driver)
    $request = [byte[]]$nativeBuffer.ToArray([byte])
    if ($client.Send($request) -ne $request.Length) { throw 'Read request send was incomplete.' }
    $nativeBuffer.Clear()
    $hex = Receive-Exact 14
    if ($hex -ne '0a0000000e700000000412340000') { throw "Native read differs: $hex" }
    $cases.Add([ordered]@{ action = 'ReadData buffer + harness socket send'; hex = $hex })

    # Feed actual bytes through the installed receive callback. No UI is created.
    # Only successful responses are tested: status=1 opens a vendor MessageBox.
    # Use a fresh driver/socket for controlled response input.
    $type.GetField('_timer', $flags).GetValue($driver).Dispose()
    $client.Close()
    $server.Close()
    $driver = [Activator]::CreateInstance($type)
    $client = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork, [Net.Sockets.SocketType]::Stream, [Net.Sockets.ProtocolType]::Tcp)
    $client.Connect($listener.LocalEndpoint)
    $server = $listener.AcceptSocket()
    $read = $type.GetMethods($flags) | Where-Object {
        $_.Name -eq 'Read' -and $_.GetParameters()[0].ParameterType.Name -eq 'Package'
    } | Select-Object -First 1
    $packageType = $read.GetParameters()[0].ParameterType
    foreach ($addressHex in '1234', '3412') {
        $package = [Activator]::CreateInstance($packageType, [object[]]($null, $null, [uint32]0x1234))
        $valuesField = $packageType.GetField('values', $flags)
        $valuesField.SetValue($package, [byte[]](0, 0, 0, 0))
        $queue = $type.GetField('_packQueue', $flags).GetValue($driver)
        $queue.Clear()
        $entry = [Collections.ArrayList]::new()
        $entry.Add($package) | Out-Null
        $entry.Add([int]0) | Out-Null
        $queue.Add($entry) | Out-Null
        $stateType = $assembly.GetType('TCPIP.StateObject', $true)
        $state = [Activator]::CreateInstance($stateType)
        $stateType.GetField('workSocket', $flags).SetValue($state, $client)
        $receiveBuffer = $stateType.GetField('buffer', $flags).GetValue($state)
        $responseHex = '0b000000127000000004' + $addressHex + '000001020304'
        $response = New-Object byte[] ($responseHex.Length / 2)
        for ($index = 0; $index -lt $response.Length; $index++) {
            $response[$index] = [Convert]::ToByte($responseHex.Substring($index * 2, 2), 16)
        }
        $pending = $client.BeginReceive($receiveBuffer, 0, $receiveBuffer.Length, [Net.Sockets.SocketFlags]::None, $null, $state)
        $sent = $server.Send($response)
        if ($sent -ne $response.Length -or !$pending.AsyncWaitHandle.WaitOne(2000)) { throw 'Native receive callback timed out.' }
        $type.GetMethod('ReceiveCallBack', $flags).Invoke($driver, [object[]]($pending)) | Out-Null
        $pending.AsyncWaitHandle.Close()
        $accepted = $queue.Count -eq 0 -and [BitConverter]::ToString($valuesField.GetValue($package)) -eq '01-02-03-04'
        if ($accepted -ne ($addressHex -eq '1234')) { throw "Unexpected native response address acceptance: $addressHex" }
        $cases.Add([ordered]@{ action = 'ReceiveCallBack'; hex = $responseHex; matchedPackage = $accepted })
    }
} finally {
    $client.Close()
    $server.Close()
    $listener.Stop()
    $type.GetField('_timer', $flags).GetValue($driver).Dispose()
}
$result = [ordered]@{
    schema = 'audio-dsp.sigmastudio-wire-test.v1'
    collectedAt = [DateTime]::UtcNow.ToString('o')
    dllSha256 = $hash
    driver = $type.FullName
    configuredChipAddress = 0x70
    cases = @($cases.ToArray())
    evidenceScope = 'Installed native driver methods and loopback sockets; not a SigmaStudio GUI project, compiled download or hardware test. SafeLoad high-level semantics, channel selection and read failures remain unverified.'
}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputFile)) | Out-Null
$json = ($result | ConvertTo-Json -Depth 8).Replace("`r`n", "`n") + "`n"
[IO.File]::WriteAllText($outputFile, $json, [Text.UTF8Encoding]::new($false))
Write-Output "Native driver wire tests passed: $outputFile"
