param([Parameter(Mandatory)][int]$AppPid, [Parameter(Mandatory)][string]$PipeName,
      [string]$OutputDirectory = 'out/ui-pages')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Test-Scenario([string]$Name, [scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name; status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name; status='failed'; error="$($_.Exception.Message)"}) }
}
function UI([string[]]$Arguments) {
    $output = rtk proxy winapp ui @Arguments -a $AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "$output" }
    return $output | ConvertFrom-Json
}
function Counters {
    $result = Send-HostRequest $PipeName 'transport-diagnostics'
    if ($result.status -eq 'error') { throw "Transport diagnostics failed: $($result.message)" }
    return $result
}
Test-Scenario 'Dashboard globals work before other pages are created' {
    UI @('invoke', 'DashboardGlobalMute') | Out-Null
    UI @('wait-for', 'DashboardGlobalMute', '--value', 'On', '-t', '3000') | Out-Null
    UI @('invoke', 'DashboardGlobalMute') | Out-Null
    UI @('wait-for', 'DashboardGlobalMute', '--value', 'Off', '-t', '3000') | Out-Null
}
Test-Scenario 'Visible meters update at no more than 20 Hz' {
    $before = Counters
    Start-Sleep -Seconds 2
    $after = Counters
    $delta = $after.telemetryRequests - $before.telemetryRequests
    if ($delta -lt 10 -or $delta -gt 43) { throw "Unexpected telemetry requests in 2 seconds: $delta" }
}
Test-Scenario 'Plugins load on first access with native virtualized ListView' {
    UI @('invoke', 'NavPlugins') | Out-Null
    UI @('invoke', 'PluginsRunningTab') | Out-Null
    UI @('wait-for', 'RunningPluginsList', '-t', '5000') | Out-Null
    $property = UI @('get-property', 'RunningPluginsList', '-p', 'ControlType')
    if (($property | ConvertTo-Json) -notmatch 'List') { throw 'Running is not a native list.' }
    UI @('screenshot', '-o', "$OutputDirectory/plugins.png") | Out-Null
}
Test-Scenario 'Search is local and preserves text through navigation' {
    Start-Sleep -Milliseconds 500
    $before = Counters
    UI @('set-value', 'RunningPluginSearchInput', 'missing') | Out-Null
    UI @('invoke', 'NavDashboard') | Out-Null
    UI @('invoke', 'NavPlugins') | Out-Null
    UI @('wait-for', 'RunningPluginSearchInput', '--value', 'missing', '-t', '3000') | Out-Null
    $after = Counters
    if ($after.snapshotRequests -ne $before.snapshotRequests) { throw 'Search/navigation requested a new network snapshot.' }
}
Test-Scenario 'Hidden meters do not request telemetry' {
    Start-Sleep -Milliseconds 200
    $before = Counters
    Start-Sleep -Seconds 2
    $after = Counters
    if ($after.telemetryRequests -ne $before.telemetryRequests) { throw 'Plugins page is polling meters.' }
}
foreach ($entry in @(@('NavAudio','AudioBackend'), @('NavSupport','SupportKoFiButton'), @('NavSettings','AppLanguage'))) {
    Test-Scenario "Create $($entry[0]) on first access" {
        UI @('invoke', $entry[0]) | Out-Null
        UI @('wait-for', $entry[1], '-t', '5000') | Out-Null
        UI @('screenshot', '-o', "$OutputDirectory/$($entry[0]).png") | Out-Null
    }
}
Test-Scenario 'Minimized UI keeps heartbeat and stops visual telemetry' {
    UI @('invoke', 'NavDashboard') | Out-Null
    UI @('invoke', 'Minimize') | Out-Null
    Start-Sleep -Milliseconds 300
    $before = Counters
    Start-Sleep -Seconds 6
    $after = Counters
    if ($after.telemetryRequests -ne $before.telemetryRequests) { throw 'Minimized UI requested visual telemetry.' }
    if ($after.heartbeatRequests -le $before.heartbeatRequests) { throw 'Minimized UI stopped connectivity checks.' }
}
$results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
$results | Format-Table name,status,error -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
