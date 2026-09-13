param([string]$OutputDirectory = 'out/ui-audio-selection', [string]$ExistingProfile = '')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$profileRoot = Join-Path $repo 'out\test-profiles'
$testProfileName = 'ui-audio-' + [guid]::NewGuid().ToString('N')
$profileDirectory = Join-Path $profileRoot $testProfileName
$metadata = Join-Path $profileDirectory 'profile.json'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
if (!$ExistingProfile) {
$hostProcess = Start-Process -FilePath "$repo\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" -ArgumentList @("--test-profile=$testProfileName", ('--profile-root="'+$profileRoot+'"')) -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(30)
while (!(Test-Path -LiteralPath $metadata)) {
    if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Test profile failed to start.' }
    Start-Sleep -Milliseconds 100
}
$profile = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
$script:pipe = $profile.pipe
$launch = rtk proxy winapp run WinUI/x64/Release/LightHost.WinUI --manifest WinUI/LightHost.WinUI/Package.appxmanifest --exe LightHostWinUI.exe --detach --json -- "--test-profile=$testProfileName" "--profile-root=$profileRoot" "--host-pipe=$($profile.pipe)" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Test UI failed to start.' }
$script:AppPid = $launch.ProcessId
@{hostPid=$hostProcess.Id; uiPid=$AppPid; name=$testProfileName; root=$profileRoot; profile=$profileDirectory; pipe=$profile.pipe} | ConvertTo-Json | Set-Content "$OutputDirectory/profile.json" -Encoding UTF8
} else {
    $profile = Get-Content -LiteralPath $ExistingProfile -Raw | ConvertFrom-Json
    if (!$profile.name.StartsWith('ui-audio-') -or ![IO.Path]::GetFullPath($profile.profile).StartsWith($profileRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'An isolated UI audio profile is required.' }
    $script:AppPid = $profile.uiPid; $script:pipe = $profile.pipe
}
$results = [Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    $output = rtk proxy winapp ui @Arguments -a $script:AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "$output" }
    $output | ConvertFrom-Json
}
function Snapshot { Send-HostRequest $script:pipe 'snapshot' }
function Mutate([string]$Command, [object[]]$Arguments=@()) {
    $current = Snapshot
    $accepted = Send-HostRequest -PipeName $script:pipe -Command $Command -Arguments $Arguments -Session $current.hostSession
    $result = Wait-HostOperation -PipeName $script:pipe -Accepted $accepted
    if ($result.status -ne 'ok') { throw ($result | ConvertTo-Json -Depth 8) }
}
function Scenario([string]$Name, [scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name; status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name; status='failed'; error="$($_.Exception.Message)"}) }
}
function Choose([string]$Combo, [string]$Text) {
    UI @('invoke', $Combo) | Out-Null
    $match = (UI @('search', $Text)).matches | Where-Object { $_.type -eq 'ListItem' -and $_.name -eq $Text -and -not $_.isOffscreen } | Select-Object -First 1
    if (!$match) { throw "ComboBox item not found: $Text" }
    UI @('invoke', $match.selector) | Out-Null
}
function Open-Preferred {
    UI @('invoke', 'NavSettings') | Out-Null
    UI @('scroll-into-view', 'PreferredDevicePicker') | Out-Null
    UI @('invoke', 'PreferredDevicePicker') | Out-Null
    UI @('wait-for', 'RecoveryAudioBackend', '-t', '6000') | Out-Null
    UI @('wait-for', 'Save', '--property', 'IsEnabled', '--value', 'True', '-t', '6000') | Out-Null
}
UI @('wait-for', 'NavDashboard', '-t', '10000') | Out-Null
Mutate 'set-audio-persistence-mode' @('custom')
Start-Sleep -Milliseconds 500
$initial = Snapshot
Scenario 'A new temporary session keeps the driver closed' {
    if ($initial.audioSelection.driverAvailable -or $initial.audioSelection.recoveryState -ne 'suspended') { throw 'The temporary profile opened audio automatically.' }
    UI @('invoke','NavAudio') | Out-Null
    UI @('screenshot','-o',"$OutputDirectory/none.png") | Out-Null
}
Scenario 'Cancel discards local preferred device choices without changing generation' {
    $before = Snapshot
    Open-Preferred
    $backend = @($before.audioConfig.backendNames | Where-Object { $_ -ne $before.appConfig.audioPersistenceCustomBackend })[0]
    Choose 'RecoveryAudioBackend' $backend
    UI @('wait-for', 'Save', '--property', 'IsEnabled', '--value', 'True', '-t', '6000') | Out-Null
    $during = Snapshot
    if ($during.audioSelection.generation -ne $before.audioSelection.generation -or $during.audioSelection.driverAvailable) { throw 'Editing preferred devices changed the host.' }
    UI @('screenshot','-o',"$OutputDirectory/draft.png") | Out-Null
    UI @('invoke','Cancel') | Out-Null
    UI @('wait-for', 'RecoveryAudioBackend', '--gone', '-t', '3000') | Out-Null
    $after = Snapshot
    if ($after.audioSelection.generation -ne $before.audioSelection.generation -or $after.appConfig.audioPersistenceCustomBackend -ne $before.appConfig.audioPersistenceCustomBackend) { throw 'Cancelling wrote preferences.' }
    $focused = UI @('get-focused')
    if (($focused | ConvertTo-Json -Depth 5) -notmatch 'PreferredDevicePicker') { throw 'Focus did not return to the preferred device button.' }
}
Scenario 'Save commits backend and both directions once without opening the driver' {
    Open-Preferred
    $before = Snapshot
    $script:chosenBackend = @($before.audioConfig.backendNames | Where-Object { $_ -ne 'ASIO' })[0]
    Choose 'RecoveryAudioBackend' $script:chosenBackend
    UI @('wait-for', 'Save', '--property', 'IsEnabled', '--value', 'True', '-t', '6000') | Out-Null
    $options = Send-HostRequest $script:pipe 'audio-device-options' @($script:chosenBackend)
    $script:chosenInput = if (@($options.inputs).Count) { $options.inputs[0] } else { '' }
    $script:chosenOutput = if (@($options.outputs).Count) { $options.outputs[0] } else { '' }
    Choose 'RecoveryInputDevice' $(if ($script:chosenInput) { $script:chosenInput } else { 'None' })
    Choose 'RecoveryOutputDevice' $(if ($script:chosenOutput) { $script:chosenOutput } else { 'None' })
    UI @('invoke','Save') | Out-Null
    UI @('wait-for', 'RecoveryAudioBackend', '--gone', '-t', '3000') | Out-Null
    Start-Sleep -Milliseconds 500
    $after = Snapshot
    if ([long]$after.audioSelection.generation -ne ([long]$before.audioSelection.generation + 1) -or $after.audioSelection.driverAvailable) { throw 'Save was not one non-opening transaction.' }
    if ($after.appConfig.audioPersistenceCustomBackend -ne $script:chosenBackend -or $after.appConfig.audioPersistenceCustomInputDevice -ne $script:chosenInput -or $after.appConfig.audioPersistenceCustomOutputDevice -ne $script:chosenOutput) { throw 'Saved preferred target differs from the local draft.' }
}
Scenario 'A stale dialog cannot overwrite a newer configuration' {
    Open-Preferred
    Mutate 'set-audio-persistence-mode' @('lastSelected')
    $before = Snapshot
    UI @('invoke','Save') | Out-Null
    UI @('wait-for', 'RecoveryAudioBackend', '--gone', '-t', '3000') | Out-Null
    Start-Sleep -Milliseconds 500
    $after = Snapshot
    if ($after.audioSelection.generation -ne $before.audioSelection.generation -or $after.appConfig.audioPersistenceMode -ne 'lastSelected') { throw 'Stale draft overwrote a newer selection.' }
    UI @('screenshot','-o',"$OutputDirectory/stale.png") | Out-Null
}
$results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
$results | Format-Table name,status,error -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
