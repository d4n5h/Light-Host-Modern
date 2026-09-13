# Actual scanner, IPC/events and UI; each run owns a fresh no-audio profile.
param([string]$OutputDirectory='out/ui-scanner', [switch]$KeepOpen)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$profileRoot=Join-Path $repo 'out\test-profiles'
$name='ui-scan-'+[guid]::NewGuid().ToString('N')
$profileDirectory=Join-Path $profileRoot $name
New-Item -ItemType Directory -Force -Path $profileDirectory,$OutputDirectory | Out-Null
$results=[Collections.Generic.List[object]]::new()
$hostProcess=$null; $script:appPid=0; $script:pipe=''
function UI([string[]]$Arguments) {
    $output=rtk proxy winapp ui @Arguments -a $script:appPid --json
    if ($LASTEXITCODE -ne 0) { throw "UI command $($Arguments -join ' ') failed: $output" }
    $output | ConvertFrom-Json
}
function Snapshot { Send-HostRequest $script:pipe 'snapshot' }
function Mutate([string]$Command, [object[]]$Arguments=@()) {
    $accepted=Send-HostRequest -PipeName $script:pipe -Command $Command -Arguments $Arguments -Session $script:hostSession
    $result=Wait-HostOperation $script:pipe $accepted -TimeoutMs 30000
    if ($result.status -ne 'ok') { throw ($result | ConvertTo-Json -Depth 8) }
}
function Wait-ScanIdle([long]$AfterRevision=0) {
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    do {
        $status=Send-HostRequest $script:pipe 'plugin-scan-status'
        if (!$status.active -and [long]$status.revision -gt $AfterRevision) { return $status }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Scan did not finish with a new revision.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Failures($Status) {
    $items=@()
    while ($items.Count -lt $Status.failureCount) {
        $page=Send-HostRequest $script:pipe 'plugin-scan-failures' @(@{scanId=$Status.scanId;revision=$Status.revision;offset=$items.Count;limit=100})
        if ($page.status -ne 'ok' -or !$page.failures.Count -or $page.failures.Count -gt 100) { throw 'Invalid failure page.' }
        $items += $page.failures
    }
    return $items
}
function Select-Failure($Failure) {
    $match=(UI @('search',$Failure.path)).matches | Where-Object { $_.type -eq 'ListItem' -and !$_.isOffscreen } | Select-Object -First 1
    if (!$match) { throw "Visible failure row was not found: $($Failure.path)" }
    UI @('invoke',$match.selector) | Out-Null
    $details=UI @('get-value','ScanFailureDetails')
    $text=$details | ConvertTo-Json -Depth 5
    if (!$text.Contains($Failure.path.Replace('\','\\')) -or !$text.Contains('Attempt 1')) { throw "Full failure details were missing: $text" }
}
function Scenario([string]$Name,[scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name;status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error="$($_.Exception.Message)"}); throw }
}
try {
    rtk proxy "$repo\out\build\windows-vs2022\Release\LightHostPluginInstanceTests.exe" --write-ui-fixture (Join-Path $profileDirectory 'Light Host Modern.settings') 3
    if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
    $hostProcess=Start-Process -FilePath "$repo\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" -ArgumentList @("--test-profile=$name",('--profile-root="'+$profileRoot+'"')) -WindowStyle Hidden -PassThru
    $metadata=Join-Path $profileDirectory 'profile.json'
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Temporary host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $profile=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    $script:pipe=$profile.pipe; $initial=Snapshot; $script:hostSession=$initial.hostSession
    if ($initial.audioSelection.driverAvailable -or $initial.knownPluginList.Count -ne 3) { throw 'The isolated fixture was not preserved without audio.' }
    Mutate 'begin-plugin-scan'
    $paths=@(0..150 | ForEach-Object { Join-Path $profileDirectory ('missing-audio-'+$_) }) -join ';'
    Mutate 'scan-plugin-path' @($paths)
    $script:status=Wait-ScanIdle
    $script:failures=Failures $script:status
    $launch=rtk proxy winapp run "$repo\WinUI\x64\Release\LightHost.WinUI" --manifest "$repo\WinUI\LightHost.WinUI\Package.appxmanifest" --exe LightHostWinUI.exe --detach --json -- "--test-profile=$name" "--profile-root=$profileRoot" "--host-pipe=$script:pipe" | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'UI did not launch.' }
    $script:appPid=$launch.ProcessId
    @{hostPid=$hostProcess.Id;uiPid=$appPid;name=$name;root=$profileRoot;profile=$profileDirectory;pipe=$script:pipe} | ConvertTo-Json | Set-Content "$OutputDirectory/profile.json" -Encoding UTF8
    UI @('wait-for','NavPlugins','-t','10000') | Out-Null
    UI @('invoke','NavPlugins') | Out-Null
    UI @('invoke','PluginsInstalledTab') | Out-Null
    Scenario 'Complete failure pages expose full details and retain selection' {
        if ($script:failures.Count -lt 151) { throw 'Failures were truncated.' }
        UI @('wait-for','ViewScanFailures','--property','IsEnabled','--value','True','-t','8000') | Out-Null
        UI @('invoke','ViewScanFailures') | Out-Null
        UI @('wait-for','ScanFailureList','-t','5000') | Out-Null
        Select-Failure $script:failures[0]
        UI @('screenshot','-o',"$OutputDirectory/page-1.png") | Out-Null
        UI @('invoke','NextFailurePage') | Out-Null
        Select-Failure $script:failures[100]
        UI @('screenshot','-o',"$OutputDirectory/page-2.png") | Out-Null
        UI @('invoke','PreviousFailurePage') | Out-Null
        $row=(UI @('search',$script:failures[0].path)).matches | Where-Object { $_.type -eq 'ListItem' -and !$_.isOffscreen } | Select-Object -First 1
        $properties=UI @('get-property',$row.selector)
        if ($properties.properties.IsSelected -ne 'True') { throw ('Selection was lost when returning to page 1: '+($properties|ConvertTo-Json -Depth 6)) }
    }
    Scenario 'Selected retry updates only the IDs selected across both pages' {
        $previousRevision=$script:status.revision
        UI @('invoke','Retry selected') | Out-Null
        UI @('wait-for','ScanFailureList','--gone','-t','5000') | Out-Null
        $script:status=Wait-ScanIdle $previousRevision
        $updated=Failures $script:status
        $selected=@($script:failures[0].id,$script:failures[100].id)
        if ($updated.Count -ne $script:failures.Count) { throw 'Retry lost prior failures.' }
        foreach ($failure in $updated) {
            $expected=if ($selected -contains $failure.id) { 2 } else { 1 }
            if ($failure.attempt -ne $expected) { throw "Wrong attempt for $($failure.id): $($failure.attempt)" }
        }
        $script:failures=$updated
    }
    Scenario 'Retry all preserves the known bank, chain and previous failure IDs' {
        $previousRevision=$script:status.revision
        UI @('invoke','RetryPluginScan') | Out-Null
        $script:status=Wait-ScanIdle $previousRevision
        $updated=Failures $script:status
        foreach ($failure in $updated) {
            $before=$script:failures | Where-Object id -eq $failure.id
            if (!$before -or $failure.attempt -ne ($before.attempt+1)) { throw 'Retry all lost an ID or attempt.' }
        }
        $after=Snapshot
        if (($after.knownPluginList.knownId -join ',') -ne ($initial.knownPluginList.knownId -join ',') -or ($after.activePlugins.instanceId -join ',') -ne ($initial.activePlugins.instanceId -join ',') -or $after.audioSelection.driverAvailable) { throw 'Scan altered the chain, bank or audio device.' }
        UI @('screenshot','-o',"$OutputDirectory/retried.png") | Out-Null
    }
} finally {
    $results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
    $results | Format-Table name,status,error -AutoSize
    if (!$KeepOpen) {
        if ($script:appPid -and (Get-Process -Id $script:appPid -ErrorAction SilentlyContinue)) {
            $dialog=UI @('search','ScanFailureList')
            if ($dialog.matches.Count) { UI @('invoke','Close') | Out-Null }
            UI @('invoke','Close') | Out-Null
            Get-Process -Id $script:appPid -ErrorAction SilentlyContinue | Wait-Process -Timeout 15
        }
        if ($hostProcess -and !$hostProcess.HasExited) {
            Send-HostRequest $script:pipe 'quit-host' -Session $script:hostSession | Out-Null
            if (!$hostProcess.WaitForExit(15000)) { throw 'Temporary host did not exit.' }
        }
    }
}
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
