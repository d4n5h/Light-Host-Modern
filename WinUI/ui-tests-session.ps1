param([string]$ProfileInfo = 'out/ui-current-profile.json', [string]$OutputDirectory = 'out/ui-session')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
$info = Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$expectedRoot = [IO.Path]::GetFullPath((Join-Path $repo 'out\test-profiles')) + '\'
if (![IO.Path]::GetFullPath($info.profile).StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'A workspace test profile is required.' }
$script:AppPid = $info.uiPid
$sessionFile = Join-Path $info.profile 'LightHostModern.settings.session.json'
$snapshot = Send-HostRequest $info.pipe 'snapshot'
$session = $snapshot.hostSession
$id = $snapshot.activePlugins[0].instanceId
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results = [Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    $output = & winapp ui @Arguments -a $script:AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "$output" }
    $output | ConvertFrom-Json
}
function Mutate([string]$Command, [object[]]$Arguments = @(), [string]$ErrorCode = '') {
    $accepted = Send-HostRequest -PipeName $info.pipe -Command $Command -Arguments $Arguments -Session $session
    $result = Wait-HostOperation -PipeName $info.pipe -Accepted $accepted
    if ($ErrorCode) { if ($result.error.code -ne $ErrorCode) { throw "Expected $ErrorCode" } }
    elseif ($result.status -ne 'ok') { throw "Operation failed: $Command" }
}
function Scenario([string]$Name, [scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name; status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name; status='failed'; error="$($_.Exception.Message)"}) }
}
function Await-Value([string]$Selector, [string]$Value) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        try { UI @('wait-for', $Selector, '-p', 'ToggleState', '--value', $Value, '-t', '2000') | Out-Null; return }
        catch {
            if (!(Get-Process -Id $script:AppPid -ErrorAction SilentlyContinue)) { throw 'Reopened UI exited unexpectedly.' }
            Start-Sleep -Milliseconds 200
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Reopened control did not reach $Value`: $Selector"
}
Scenario 'Session write failure is visible and retry preserves the live UUID' {
    Mutate 'flush-session'
    $hash = (Get-FileHash -LiteralPath $sessionFile).Hash
    $lock = [IO.File]::Open($sessionFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $testName = 'Recovered from UI retry ' + [guid]::NewGuid().ToString('N').Substring(0,8)
    try {
        Mutate 'rename-plugin' @($id, $testName)
        Mutate 'flush-session' @() 'session_save_failed'
        UI @('wait-for', 'RetrySessionSave', '-t', '6000') | Out-Null
        UI @('screenshot', '-o', "$OutputDirectory/write-error.png") | Out-Null
        if ((Get-FileHash -LiteralPath $sessionFile).Hash -ne $hash) { throw 'Failed write changed the last valid file.' }
    } finally { $lock.Dispose() }
    UI @('invoke', 'RetrySessionSave') | Out-Null
    UI @('wait-for', 'RetrySessionSave', '--gone', '-t', '6000') | Out-Null
    $envelope = Get-Content -LiteralPath $sessionFile -Raw | ConvertFrom-Json
    [xml]$xml = $envelope.sessionXml
    if ($xml.LIGHTHOSTSESSION.INSTANCE[0].id -ne $id -or $xml.LIGHTHOSTSESSION.INSTANCE[0].customName -ne $testName) { throw 'UI retry saved the wrong identity or name.' }
    UI @('screenshot', '-o', "$OutputDirectory/recovered.png") | Out-Null
}
Scenario 'Closing and reopening UI preserves live global controls and session' {
    Mutate 'set-global-mute' @($true)
    Mutate 'set-global-bypass' @($true)
    $previousUi = Get-Process -Id $script:AppPid
    UI @('invoke', 'Close') | Out-Null
    if (!$previousUi.WaitForExit(10000)) { throw 'UI process remained alive after closing.' }
    $live = Send-HostRequest $info.pipe 'snapshot'
    if ($live.hostSession -ne $session -or !$live.globalMuted -or !$live.globalBypassed) { throw 'Closing UI changed the live host.' }
    $launch = Start-TestUi -Directory "WinUI/x64/Release/LightHostModern.WinUI" -Arguments @("--test-profile=$($info.name)", "--profile-root=$($info.root)", "--host-pipe=$($info.pipe)")
    if ($LASTEXITCODE -ne 0) { throw 'Could not reopen UI.' }
    $script:AppPid = $launch.ProcessId
    $info.uiPid = $script:AppPid
    $info | ConvertTo-Json | Set-Content -LiteralPath $ProfileInfo -Encoding UTF8
    UI @('wait-for','NavPlugins','-t','10000') | Out-Null
    UI @('invoke','NavPlugins') | Out-Null
    UI @('invoke','PluginsRunningTab') | Out-Null
    Await-Value 'RunningGlobalMute' 'On'
    Await-Value 'RunningGlobalBypass' 'On'
    Mutate 'set-global-mute' @($false)
    Mutate 'set-global-bypass' @($false)
    UI @('screenshot', '-o', "$OutputDirectory/reopened.png") | Out-Null
}
$results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
$results | Format-Table name,status,error -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
