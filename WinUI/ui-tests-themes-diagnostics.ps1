param([Parameter(Mandatory)][int]$AppPid, [Parameter(Mandatory)][string]$PipeName,
      [string]$OutputDirectory = 'out/ui-themes-diagnostics')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results = [Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    $output = rtk proxy winapp ui @Arguments -a $AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "$output" }
    $output | ConvertFrom-Json
}
function Scenario([string]$Name, [scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name; status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name; status='failed'; error="$($_.Exception.Message)"}) }
}
function Choose([string]$Combo, [string]$Text) {
    UI @('scroll-into-view', $Combo) | Out-Null
    UI @('invoke', $Combo) | Out-Null
    $match = (UI @('search', $Text)).matches | Where-Object { $_.type -eq 'ListItem' -and $_.name -eq $Text -and -not $_.isOffscreen } | Select-Object -First 1
    if (!$match) { throw "ComboBox item not found: $Text" }
    UI @('invoke', $match.selector) | Out-Null
    Start-Sleep -Milliseconds 250
}
Scenario 'Light, dark and system themes can change with a native popup open' {
    UI @('invoke', 'NavSettings') | Out-Null
    foreach ($theme in @('Light','Dark','System','Light','Dark')) {
        Choose 'AppTheme' $theme
        UI @('invoke', 'NavDashboard') | Out-Null
        UI @('wait-for', 'DashboardGlobalMute', '-t', '3000') | Out-Null
        UI @('screenshot', '-o', "$OutputDirectory/$theme.png") | Out-Null
        UI @('invoke', 'NavSettings') | Out-Null
    }
}
Scenario 'Visible diagnostics use one request per second and report separate process CPU' {
    UI @('invoke', 'NavDiagnostics') | Out-Null
    Start-Sleep -Seconds 2
    $before = Send-HostRequest $PipeName 'transport-diagnostics'
    Start-Sleep -Seconds 3
    $after = Send-HostRequest $PipeName 'transport-diagnostics'
    $delta = $after.telemetryRequests - $before.telemetryRequests
    if ($delta -lt 2 -or $delta -gt 4) { throw "Expected 1 Hz; observed $delta requests in 3 seconds." }
    foreach ($field in @('hostCpuPercent','uiCpuPercent','workerCpuPercent')) {
        $text = (UI @('get-property', "Diagnostic-$field", '-p', 'Name')).properties.Name
        if ($text -notmatch '[0-9]+\.[0-9]+%') { throw "Missing CPU measurement: $text" }
    }
    $driver = (UI @('get-property', 'Diagnostic-sampleRate', '-p', 'Name')).properties.Name
    if ($driver -notmatch 'Unavailable') { throw "Suspended driver reported a format: $driver" }
    UI @('screenshot', '-o', "$OutputDirectory/diagnostics.png") | Out-Null
}
Scenario 'Leaving diagnostics suspends its telemetry while retaining the page' {
    UI @('invoke', 'NavSettings') | Out-Null
    Start-Sleep -Milliseconds 200
    $before = Send-HostRequest $PipeName 'transport-diagnostics'
    Start-Sleep -Seconds 2
    $after = Send-HostRequest $PipeName 'transport-diagnostics'
    if ($after.telemetryRequests -ne $before.telemetryRequests) { throw 'Offscreen diagnostics are polling.' }
}
$results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
$results | Format-Table name,status,error -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
