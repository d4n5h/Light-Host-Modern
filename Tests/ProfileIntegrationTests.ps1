param([string] $HostExecutable = "$PSScriptRoot\..\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe")
$ErrorActionPreference = 'Stop'
$VerbosePreference = 'Continue'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root = Join-Path $repo 'out\test-profiles'
$name = 'integration-' + [guid]::NewGuid().ToString('N')
$profileDirectory = Join-Path $root $name
$metadata = Join-Path $profileDirectory 'profile.json'
$process = $null
$files = @((Join-Path $env:APPDATA 'LightHostModern\LightHostModern.settings'), (Join-Path $env:LOCALAPPDATA 'LightHostModern\ui-settings.ini'))
$before = @{}
foreach ($file in $files) { $before[$file] = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { '' } }
$runKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
$startupBefore = if ($runKey) { $runKey.GetValue('LightHostModern'); $runKey.Dispose() } else { $null }
try {
    $process = Start-Process -FilePath (Resolve-Path -LiteralPath $HostExecutable).Path -ArgumentList @("--test-profile=$name", "--profile-root=`"$root`"") -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Test profile failed to start.' }
        Start-Sleep -Milliseconds 100
    }
    $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    if ($snapshot.diagnostics.sampleRate -ne $null -or $snapshot.diagnostics.recoveryState -ne 'suspended') { throw 'Test profile opened audio automatically.' }
    $session = $snapshot.hostSession
    if ([string]::IsNullOrEmpty($session)) { throw 'Missing host session.' }
    if ($snapshot.monoOutput -ne $false) { throw 'Output mono must default to disabled.' }
    $staleGeneration = ([uint64]$snapshot.audioSelection.generation + 1).ToString()
    foreach ($monoCommand in 'set-mono-inputs','set-mono-output') {
        $staleMono = Send-HostRequest $info.pipe $monoCommand @(@{ enabled=$true; expectedGeneration=$staleGeneration }) -Session $session
        $staleResult = Wait-HostOperation $info.pipe $staleMono
        if ($staleResult.status -ne 'error' -or $staleResult.error.code -ne 'stale_configuration') { throw ('Unexpected mono result: ' + ($staleResult | ConvertTo-Json -Depth 8 -Compress)) }
        foreach ($enabled in 'true',$true) {
            $invalidMono = Send-HostRequest $info.pipe $monoCommand @(@{ enabled=$enabled; expectedGeneration=$snapshot.audioSelection.generation }) -Session $session
            $invalidResult = Wait-HostOperation $info.pipe $invalidMono
            if ($invalidResult.error.code -ne 'invalid_arguments') { throw 'Mono accepted a non-boolean or an unconfigured device.' }
        }
    }
    $requestId = [guid]::NewGuid().ToString('N')
    $accepted = Send-HostRequest -PipeName $info.pipe -Command 'set-global-mute' -Arguments @($true) -Session $session -RequestId $requestId
    if ($accepted.status -ne 'operation') { throw 'Mutation did not return an operation.' }
    $result = Wait-HostOperation -PipeName $info.pipe -Accepted $accepted
    if ($result.status -ne 'ok') { throw 'Mutation failed.' }
    $again = Send-HostRequest -PipeName $info.pipe -Command 'set-global-mute' -Arguments @($true) -Session $session -RequestId $requestId
    if ($again.operationState -ne 'completed') { throw 'Repeated ID lost its completed result.' }
    $conflict = Send-HostRequest -PipeName $info.pipe -Command 'set-global-mute' -Arguments @($false) -Session $session -RequestId $requestId
    if ($conflict.error.code -ne 'request_id_conflict') { throw 'Conflicting request ID was accepted.' }
    $wrongHost = Send-HostRequest -PipeName $info.pipe -Command 'operation-status' -Arguments @($requestId, 'previous-host')
    if ($wrongHost.error.code -ne 'host_restarted') { throw 'Host restart was not distinguished.' }
    $startup = Send-HostRequest -PipeName $info.pipe -Command 'set-start-with-windows' -Arguments @($true) -Session $session
    $startupResult = Wait-HostOperation -PipeName $info.pipe -Accepted $startup
    if ($startupResult.status -ne 'error') { throw 'Test profile was permitted to modify startup.' }
    Start-Sleep -Seconds 6
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    if ($snapshot.diagnostics.sampleRate -ne $null -or !$snapshot.globalMuted) { throw 'Watchdog opened audio or lost live state.' }
    Send-HostRequest -PipeName $info.pipe -Command 'quit-host' -Session $session | Out-Null
    if (!$process.WaitForExit(10000)) { throw 'Host did not shut down.' }
    foreach ($file in $files) {
        $after = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { '' }
        if ($after -ne $before[$file]) { throw "Production preferences were modified: $file" }
    }
    $runKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
    $startupAfter = if ($runKey) { $runKey.GetValue('LightHostModern'); $runKey.Dispose() } else { $null }
    if ($startupAfter -cne $startupBefore) { throw 'Production startup registry changed.' }
    @{ passed = $true; hostSession = $session; profile = $profileDirectory; realAudioOpened = $false } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $profileDirectory 'integration-result.json') -Encoding UTF8
    Write-Output "PASS profile isolation, no-audio watchdog, operations, restart detection, startup isolation and host shutdown: $profileDirectory"
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
}
