param([Parameter(Mandatory)][string]$HostExecutable,[Parameter(Mandatory)][string]$FixtureExecutable,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$HostExecutable=(Resolve-Path -LiteralPath $HostExecutable).Path
$FixtureExecutable=(Resolve-Path -LiteralPath $FixtureExecutable).Path
$testRoot=Join-Path (Get-Location).Path $OutputDirectory
$name='preferences-'+[guid]::NewGuid().ToString('N')
$profile=Join-Path $testRoot $name
New-Item -ItemType Directory -Force -Path $profile | Out-Null
& $FixtureExecutable --write-ui-fixture (Join-Path $profile 'Light Host Modern.settings') 2
if ($LASTEXITCODE -ne 0) { throw 'Could not write the simulated catalogue.' }
$hostProcess=$null
function Start-TestHost {
    $script:hostProcess=Start-Process -FilePath $HostExecutable -ArgumentList @("--test-profile=$name","--profile-root=`"$testRoot`"",'--no-audio','--no-restore-active-plugins') -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($hostProcess.HasExited) { throw 'Test host exited on startup.' }
        $metadata=Join-Path $profile 'profile.json'
        if (Test-Path -LiteralPath $metadata) {
            $script:info=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
            try {
                $script:snapshot=Send-HostRequest $info.pipe snapshot -TimeoutMs 500
                if ($snapshot.hostPid -eq $hostProcess.Id) { return }
            } catch { }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Test host did not expose its profile pipe.'
}
function Mutate([string]$Command,[array]$Arguments=@()) {
    Wait-HostOperation $info.pipe (Send-HostRequest $info.pipe $Command -Arguments $Arguments -Session $snapshot.hostSession)
}
function Stop-TestHost {
    if ($hostProcess -and !$hostProcess.HasExited) {
        Send-HostRequest $info.pipe quit-host -Session $snapshot.hostSession | Out-Null
        if (!$hostProcess.WaitForExit(10000)) { throw 'Test host did not exit normally.' }
    }
}
function Require([bool]$Value,[string]$Message) { if (!$Value) { throw $Message } }
try {
    Start-TestHost
    Require ($snapshot.diagnosticsEnabled -eq $true) 'New profiles must enable diagnostics.'
    Require ($snapshot.knownPluginList.Count -eq 2 -and !$snapshot.audioSelection.driverAvailable) 'Wrong fixture or audio unexpectedly open.'
    $plugin=$snapshot.knownPluginList[0]; $other=$snapshot.knownPluginList[1]
    $original=$plugin.originalName; $id=$plugin.knownId
    $rename=Mutate rename-known-plugin @($id,'Studio Reverb')
    Require ($rename.status -eq 'ok') 'Installed rename command failed.'
    $details=Send-HostRequest $info.pipe known-plugin-details -Arguments @($id)
    Require ($details.name -eq $original -and $details.customName -eq 'Studio Reverb') 'Rename modified original metadata.'
    $invalid=Mutate rename-known-plugin @($id,"bad`nname")
    Require ($invalid.status -eq 'error') 'Multiline catalogue alias was accepted.'
    $disabled=Mutate set-diagnostics-enabled @($false)
    Require ($disabled.status -eq 'ok') 'Diagnostics opt-out failed.'
    $snapshot=Send-HostRequest $info.pipe snapshot
    Require (!$snapshot.diagnosticsEnabled -and $null -eq $snapshot.diagnostics.hostCpuPercent -and $null -eq $snapshot.diagnostics.workerCpuPercent) 'Disabled diagnostics sampled CPU.'
    $measurement=Mutate measure-callbacks @(1,1)
    Require ($measurement.status -eq 'error') 'Disabled diagnostics still allowed callback timing collection.'
    $peaks=Send-HostRequest ($info.pipe+'-meters') meter-levels
    Require ($peaks.status -eq 'ok') 'Diagnostics opt-out disabled the independent meter transport.'
    Stop-TestHost; Start-TestHost
    Require (!$snapshot.diagnosticsEnabled) 'Diagnostics preference was lost on host restart.'
    $plugin=$snapshot.knownPluginList | Where-Object knownId -eq $id
    Require ($plugin.name -eq 'Studio Reverb' -and $plugin.originalName -eq $original) 'Catalogue alias was lost on host restart.'
    Require (($snapshot.knownPluginList | Where-Object knownId -eq $other.knownId).name -eq $other.name) 'Alias leaked to another plugin.'
    Require ((Mutate rename-known-plugin @($id,'')).status -eq 'ok') 'Restore catalogue name failed.'
    Require ((Mutate set-diagnostics-enabled @($true)).status -eq 'ok') 'Diagnostics did not re-enable.'
    Stop-TestHost; Start-TestHost
    Require ($snapshot.diagnosticsEnabled) 'Re-enabled preference did not persist.'
    $plugin=$snapshot.knownPluginList | Where-Object knownId -eq $id
    Require ($plugin.name -eq $original -and !$plugin.customName) 'Restoring original name did not persist.'
    [ordered]@{status='passed';profile=$name;aliasPersisted=$true;restorePersisted=$true;diagnosticsDefaultEnabled=$true;diagnosticsRestartPersistence=$true;cpuUnavailableWhileDisabled=$true;timingRejectedWhileDisabled=$true;independentMeterTransport=$true;audioOpened=$false} |
        ConvertTo-Json | Tee-Object -FilePath (Join-Path $testRoot 'preferences-integration.json')
} finally { Stop-TestHost }
