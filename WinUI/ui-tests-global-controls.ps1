param([string]$OutputDirectory='out/ui-global-controls')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$metadata=Join-Path $OutputDirectory 'profile.json'
$results=[Collections.Generic.List[object]]::new()
function Start-Ui([switch]$Existing) {
    $arguments=@('-NoProfile','-File',"$PSScriptRoot\..\Tests\StartIsolatedProfile.ps1",'-Purpose','ui-global','-OutputDirectory',$OutputDirectory)
    if ($Existing) { $arguments+=@('-ExistingProfile',$metadata) }
    & powershell @arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not launch the isolated profile.' }
    $script:info=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
}
function UI([string[]]$Arguments) {
    $output=& winapp ui @Arguments -a $script:info.uiPid --json
    if ($LASTEXITCODE -ne 0) { throw "UI command $($Arguments -join ' ') failed: $output" }
    $output | ConvertFrom-Json
}
function Snapshot { Send-HostRequest $script:info.pipe 'snapshot' }
function Assert-State([bool]$Muted,[bool]$Bypassed,[string]$Prefix='Running') {
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {
        $snapshot=Snapshot
        if ($snapshot.globalMuted -eq $Muted -and $snapshot.globalBypassed -eq $Bypassed) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "The actual host did not reach mute=$Muted bypass=$Bypassed." }
        Start-Sleep -Milliseconds 25
    } while ($true)
    if ($snapshot.audioSelection.driverAvailable) { throw 'Global UI validation must keep the audio driver closed.' }
    foreach ($control in @(@('Mute',$Muted),@('Bypass',$Bypassed))) {
        UI @('wait-for',($Prefix+'Global'+$control[0]),'-p','ToggleState','--value',$(if ($control[1]) {'On'}else{'Off'}),'-t','5000') | Out-Null
        UI @('wait-for',($Prefix+'Global'+$control[0]),'-p','IsEnabled','--value','True','-t','5000') | Out-Null
    }
}
function Scenario([string]$Name,[scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name;status='passed'}); Write-Host "PASS: $Name" }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error="$($_.Exception.Message)"}); throw }
}
function Close-Ui {
    $process=Get-Process -Id $script:info.uiPid -ErrorAction SilentlyContinue
    if ($process) {
        UI @('invoke','Close') | Out-Null
        if (!$process.WaitForExit(10000)) { throw 'Closing the UI did not exit its process.' }
    }
}
try {
    Start-Ui
    $firstSession=(Snapshot).hostSession
    Scenario 'A new actual host starts with both global controls off' {
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsRunningTab') | Out-Null
        Assert-State $false $false
    }
    Scenario 'Running controls update confirmed host state in both directions' {
        UI @('invoke','RunningGlobalMute') | Out-Null
        Assert-State $true $false
        UI @('invoke','RunningGlobalBypass') | Out-Null
        Assert-State $true $true
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsRunningTab') | Out-Null
        Assert-State $true $true 'Running'
        UI @('invoke','RunningGlobalMute') | Out-Null
        Assert-State $false $true 'Running'
        UI @('invoke','RunningGlobalBypass') | Out-Null
        Assert-State $false $false 'Running'
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsRunningTab') | Out-Null
        Assert-State $false $false
    }
    Scenario 'Repeated keyboard activation keeps focus and processes every accepted toggle' {
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsRunningTab') | Out-Null
        foreach ($control in @('Mute','Bypass')) {
            $id='RunningGlobal'+$control
            UI @('focus',$id) | Out-Null
            for ($index=0;$index -lt 6;$index++) {
                UI @('send-keys','space','--via','send-input') | Out-Null
                $enabled=($index % 2) -eq 0
                Assert-State ($control -eq 'Mute' -and $enabled) ($control -eq 'Bypass' -and $enabled) 'Running'
                if (((UI @('get-focused')) | ConvertTo-Json -Depth 6) -notmatch $id) { throw "Completion lost keyboard focus on $id." }
            }
        }
    }
    Scenario 'Closing and reopening the UI preserves the live host and both flags' {
        UI @('invoke','RunningGlobalMute') | Out-Null
        Assert-State $true $false 'Running'
        UI @('invoke','RunningGlobalBypass') | Out-Null
        Assert-State $true $true 'Running'
        UI @('screenshot','-o',"$OutputDirectory/running.png") | Out-Null
        Close-Ui
        if ((Snapshot).hostSession -ne $firstSession) { throw 'Closing the UI restarted the host.' }
        Start-Ui -Existing
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsRunningTab') | Out-Null
        Assert-State $true $true
        UI @('screenshot','-o',"$OutputDirectory/reopened.png") | Out-Null
    }
    Scenario 'Restarting the same saved profile resets mute and bypass' {
        Close-Ui
        $current=Snapshot
        Send-HostRequest $script:info.pipe 'quit-host' -Session $current.hostSession | Out-Null
        Get-Process -Id $script:info.hostPid -ErrorAction SilentlyContinue | Wait-Process -Timeout 15
        $hostProcess=Start-Process -FilePath "$(Get-TestBuildDirectory)\LightHostModern_artefacts\Release\LightHostModern.exe" -ArgumentList @("--test-profile=$($script:info.name)", ('--profile-root="'+$script:info.root+'"')) -WindowStyle Hidden -PassThru
        $script:info.hostPid=$hostProcess.Id
        $script:info | ConvertTo-Json | Set-Content $metadata -Encoding UTF8
        $deadline=[DateTime]::UtcNow.AddSeconds(15)
        do {
            try { $newSession=(Send-HostRequest $script:info.pipe 'hello' -TimeoutMs 1000).hostSession } catch { $newSession='' }
            if ($newSession -and $newSession -ne $firstSession) { break }
            if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'The same profile did not restart.' }
            Start-Sleep -Milliseconds 100
        } while ($true)
        Start-Ui -Existing
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsRunningTab') | Out-Null
        Assert-State $false $false
    }
} finally {
    $results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
    $results | Format-Table name,status,error -AutoSize
    if (Test-Path -LiteralPath $metadata) { & powershell -NoProfile -File "$PSScriptRoot\..\Tests\StopIsolatedProfile.ps1" -Metadata $metadata }
}
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
