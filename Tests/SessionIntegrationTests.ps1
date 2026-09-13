param(
    [string]$HostExecutable = "$PSScriptRoot\..\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe",
    [string]$FixtureWriter = "$PSScriptRoot\..\out\build\windows-vs2022\Release\LightHostPluginInstanceTests.exe"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root = Join-Path $repo 'out\test-profiles'
$name = 'session-' + [guid]::NewGuid().ToString('N')
$directory = Join-Path $root $name
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$settings = Join-Path $directory 'Light Host Modern.settings'
$sessionFile = $settings + '.session.json'
rtk proxy $FixtureWriter --write-legacy-fixture $settings
if ($LASTEXITCODE -ne 0) { throw 'Could not write legacy fixture.' }
$originalHash = (Get-FileHash -LiteralPath $settings).Hash
$script:hostProcess = $null
$script:info = $null
$script:session = ''
$evidence = [Collections.Generic.List[string]]::new()
function Start-TestHost([string[]]$Extra = @()) {
    $script:hostProcess = Start-Process -FilePath $HostExecutable -ArgumentList (@("--test-profile=$name", ('--profile-root="' + $root + '"')) + $Extra) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $metadata = Get-Content -LiteralPath (Join-Path $directory 'profile.json') -Raw | ConvertFrom-Json
            if ($metadata.pid -eq $script:hostProcess.Id) {
                $script:info = $metadata
                $hello = Send-HostRequest $info.pipe 'hello'
                $script:session = $hello.hostSession
                return
            }
        } catch { }
        if ($script:hostProcess.HasExited) { throw 'Host exited while restoring session.' }
        Start-Sleep -Milliseconds 50
    }
    throw 'Host startup timed out.'
}
function Snapshot { Send-HostRequest $info.pipe 'snapshot' }
function Mutate([string]$Command, [object[]]$Arguments = @(), [string]$ExpectedError = '') {
    $accepted = Send-HostRequest -PipeName $info.pipe -Command $Command -Arguments $Arguments -Session $session
    $result = Wait-HostOperation -PipeName $info.pipe -Accepted $accepted
    if ($ExpectedError) {
        if ($result.error.code -ne $ExpectedError) { throw "Expected $ExpectedError, received $($result | ConvertTo-Json -Compress -Depth 8)" }
    } elseif ($result.status -ne 'ok') { throw "Operation failed: $Command ($($result | ConvertTo-Json -Compress -Depth 8))" }
    $result
}
function Stop-TestHost {
    Send-HostRequest -PipeName $info.pipe -Command 'quit-host' -Session $session | Out-Null
    if (!$script:hostProcess.WaitForExit(15000)) { throw 'Host did not finish final session save.' }
}
function Assert-Chain([object]$Snapshot, [string[]]$Ids) {
    $actual = @($Snapshot.activePlugins | ForEach-Object instanceId)
    if (($actual -join ',') -ne ($Ids -join ',')) { throw "Chain identity/order mismatch: $actual" }
    if ($Snapshot.diagnostics.sampleRate -ne $null) { throw 'Isolated session unexpectedly opened audio.' }
}
try {
    $ids = @('22222222222222222222222222222222','11111111111111111111111111111111')
    Start-TestHost
    Assert-Chain (Snapshot) $ids
    Mutate 'flush-session' | Out-Null
    if ((Get-FileHash -LiteralPath ($settings + '.pre-session.bak')).Hash -ne $originalHash) { throw 'Original legacy preferences copy changed.' }
    $firstFile = [IO.File]::ReadAllBytes($sessionFile)
    $firstEnvelope = Get-Content -LiteralPath $sessionFile -Raw | ConvertFrom-Json
    [xml]$firstXml = $firstEnvelope.sessionXml
    if ($firstXml.LIGHTHOSTSESSION.INSTANCE[0].STATE -eq $firstXml.LIGHTHOSTSESSION.INSTANCE[1].STATE) { throw 'Migration mixed distinct duplicate states.' }
    $evidence.Add('Exact backup and distinct duplicate states')

    Mutate 'rename-plugin' @($ids[0], 'Durable before restart') | Out-Null
    Mutate 'flush-session' | Out-Null
    $durableBytes = [IO.File]::ReadAllBytes($sessionFile)
    $durableHash = (Get-FileHash -LiteralPath $sessionFile).Hash
    Stop-TestHost
    Start-TestHost @('--safe-mode')
    $safe = Snapshot
    Assert-Chain $safe $ids
    if ($safe.diagnostics.session.writable -or @($safe.activePlugins | Where-Object loading -ne 'suspended').Count) { throw 'Safe mode did not retain a suspended chain.' }
    Mutate 'remove-plugin' @($ids[0]) 'session_read_only' | Out-Null
    Stop-TestHost
    if ((Get-FileHash -LiteralPath $sessionFile).Hash -ne $durableHash) { throw 'Safe mode overwrote the saved session.' }
    $evidence.Add('Safe mode preserves ordered records and file bytes')

    $sessionUtility=Join-Path (Split-Path $FixtureWriter -Parent) 'LightHostSessionTests.exe'
    rtk proxy $sessionUtility --mark-session-failed $settings
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare a valid failed-load marker.' }
    $marked=Get-Content -LiteralPath $sessionFile -Raw -Encoding UTF8 | ConvertFrom-Json
    [xml]$markedXml=$marked.sessionXml
    if (!$markedXml.LIGHTHOSTSESSION.INSTANCE[0].error) { throw 'The failure fixture was not stored.' }
    $states=@($markedXml.LIGHTHOSTSESSION.INSTANCE | ForEach-Object STATE)
    Start-TestHost @('--safe-mode','--clear-failed-plugins')
    Assert-Chain (Snapshot) $ids
    Stop-TestHost
    $cleared=Get-Content -LiteralPath $sessionFile -Raw -Encoding UTF8 | ConvertFrom-Json
    [xml]$clearedXml=$cleared.sessionXml
    if (@($clearedXml.LIGHTHOSTSESSION.INSTANCE | Where-Object error).Count -or
        ((@($clearedXml.LIGHTHOSTSESSION.INSTANCE | ForEach-Object STATE) -join ',') -ne ($states -join ',')) -or
        ((@($clearedXml.LIGHTHOSTSESSION.INSTANCE | ForEach-Object id) -join ',') -ne ($ids -join ','))) {
        throw 'Clearing load failures lost states/UUIDs or retained a failure marker.'
    }
    $durableBytes=[IO.File]::ReadAllBytes($sessionFile)
    $durableHash=(Get-FileHash -LiteralPath $sessionFile).Hash
    $evidence.Add('Clear-failed-plugins clears versioned markers while preserving distinct states and UUIDs')

    Start-TestHost
    Assert-Chain (Snapshot) $ids
    # The deliberately cleared marker can legitimately become a new missing-
    # module result on this normal load. Establish the durable revision before
    # denying replacement; only writes during the lock are compared below.
    Mutate 'flush-session' | Out-Null
    $durableBytes=[IO.File]::ReadAllBytes($sessionFile)
    $durableHash=(Get-FileHash -LiteralPath $sessionFile).Hash
    # A temporary exclusive lock simulates replacement denial. Only this test
    # profile's primary file is locked; normal driver/preferences files are untouched.
    $lock = [IO.File]::Open($sessionFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        Mutate 'rename-plugin' @($ids[0], 'Recovered pending state') | Out-Null
        Mutate 'flush-session' @() 'session_save_failed' | Out-Null
        $failed = Snapshot
        if (!$failed.diagnostics.session.pending -or !$failed.diagnostics.session.error) { throw 'Failed commit was marked saved.' }
        if ((Get-FileHash -LiteralPath $sessionFile).Hash -ne $durableHash) { throw 'Failed replacement altered primary bytes.' }
        if (!(Test-Path -LiteralPath ($sessionFile + '.pending'))) { throw 'Completed recovery temporary was not retained.' }
        # Abrupt termination is intentional and restricted to the test host.
        $script:hostProcess.Kill(); $script:hostProcess.WaitForExit()
    } finally { $lock.Dispose() }
    Start-TestHost
    $recovered = Snapshot
    Assert-Chain $recovered $ids
    if ($recovered.activePlugins[0].customName -ne 'Recovered pending state') { throw 'Restart lost the completed pending revision.' }
    Mutate 'flush-session' | Out-Null
    Stop-TestHost
    $evidence.Add('Locked replacement remains pending and survives process termination')

    # Break only primary; the valid backup must be used and the original retained.
    [IO.File]::WriteAllText($sessionFile, '{corrupt primary')
    Start-TestHost
    Assert-Chain (Snapshot) $ids
    Mutate 'flush-session' | Out-Null
    Stop-TestHost
    $archives = @(Get-ChildItem -LiteralPath $directory -Filter '*.damaged-*')
    if (!$archives.Count -or !($archives | Where-Object { [IO.File]::ReadAllText($_.FullName) -eq '{corrupt primary' })) { throw 'Corrupt original was not preserved.' }
    $evidence.Add('Corrupt primary recovers backup and preserves original bytes')

    # If every recovery candidate is invalid, legacy preferences must not be
    # imported again and an empty chain must not replace these original files.
    [IO.File]::WriteAllText($sessionFile, '{unrecoverable primary')
    [IO.File]::WriteAllText($sessionFile + '.bak', '{unrecoverable backup')
    Start-TestHost @('--clear-failed-plugins')
    $invalid = Snapshot
    if ($invalid.diagnostics.session.writable -or !$invalid.diagnostics.session.recoveryError) { throw 'Unrecoverable session is writable or silent.' }
    if (@($invalid.activePlugins).Count -ne 0) { throw 'Unrecoverable new session silently reimported stale legacy data.' }
    Mutate 'flush-session' @() 'session_save_failed' | Out-Null
    Stop-TestHost
    if ([IO.File]::ReadAllText($sessionFile) -ne '{unrecoverable primary') { throw 'Unrecoverable primary was overwritten.' }
    $evidence.Add('Unrecoverable files reject writes and stale legacy reimport')

    # Explicit recovery followed by an intentional user removal can save empty.
    [IO.File]::WriteAllBytes($sessionFile, $durableBytes)
    [IO.File]::WriteAllBytes($sessionFile + '.bak', $firstFile)
    Start-TestHost
    foreach ($id in $ids) { Mutate 'remove-plugin' @($id) | Out-Null }
    Mutate 'flush-session' | Out-Null
    Stop-TestHost
    $empty = Get-Content -LiteralPath $sessionFile -Raw | ConvertFrom-Json
    if (!$empty.intentionalEmpty) { throw 'Intentional empty session lacks its marker.' }
    Start-TestHost
    if (@((Snapshot).activePlugins).Count -ne 0) { throw 'Empty session reimported legacy plugins.' }
    Stop-TestHost
    $evidence.Add('Intentional empty session remains empty after restart')
    @{passed=$true; profile=$directory; realAudioOpened=$false; scenarios=$evidence} | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $directory 'session-result.json') -Encoding UTF8
    Write-Output "PASS: durable session integration in $directory"
} finally {
    if ($script:hostProcess -and !$script:hostProcess.HasExited) { $script:hostProcess.Kill(); $script:hostProcess.WaitForExit() }
}
