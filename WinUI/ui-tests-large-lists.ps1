param([int[]]$Counts = @(100,500,1000), [string]$OutputDirectory = 'out/ui-large-lists', [string]$ProcessorCache='')
$ErrorActionPreference='Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$repo\Tests\HostProtocol.ps1"
$root = Join-Path $repo 'out\test-profiles'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results = [Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    $output = rtk proxy winapp ui @Arguments -a $script:appPid --json
    if ($LASTEXITCODE -ne 0) { throw "UI command $($Arguments -join ' ') failed: $output" }
    return $output | ConvertFrom-Json
}
foreach ($count in $Counts) {
    $name = "ui-list-$count-"+[guid]::NewGuid().ToString('N')
    $profile = Join-Path $root $name
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    $hostProcess=$null; $appPid=0; $pipe=''
    try {
        $fixtureArguments=@('--write-ui-fixture',(Join-Path $profile 'Light Host Modern.settings'),$count)
        if ($ProcessorCache) { $fixtureArguments=@('--write-loaded-ui-fixture',(Join-Path $profile 'Light Host Modern.settings'),$count,$ProcessorCache) }
        rtk proxy "$repo\out\build\windows-vs2022\Release\LightHostPluginInstanceTests.exe" @fixtureArguments
        if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $hostProcess = Start-Process -FilePath "$repo\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" -ArgumentList @("--test-profile=$name", ('--profile-root="'+$root+'"')) -PassThru -WindowStyle Hidden
        $metadata = Join-Path $profile 'profile.json'
        $deadline = [DateTime]::UtcNow.AddSeconds($(if ($ProcessorCache) { 180 } else { 30 }))
        while (!(Test-Path -LiteralPath $metadata)) {
            if ($hostProcess.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Test host failed to start.' }
            Start-Sleep -Milliseconds 100
        }
        $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
        $pipe=$info.pipe
        $snapshot=Send-HostRequest $pipe 'snapshot'
        if ($snapshot.activePlugins.Count -ne $count -or $snapshot.knownPluginList.Count -ne $count) { throw 'Host fixture count mismatch.' }
        if ($ProcessorCache -and $snapshot.diagnostics.loadedPlugins -ne $count) { throw 'The host did not load every simulated processor.' }
        if ($snapshot.diagnostics.deviceName -notin @('none','None','')) { throw 'Fixture unexpectedly opened an audio device.' }
        $launch = rtk proxy winapp run "$repo\WinUI\x64\Release\LightHost.WinUI" --manifest "$repo\WinUI\LightHost.WinUI\Package.appxmanifest" --exe LightHostWinUI.exe --detach --json -- "--test-profile=$name" "--profile-root=$root" "--host-pipe=$pipe" | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0) { throw 'UI failed to launch.' }
        $script:appPid=$launch.ProcessId
        @{hostPid=$hostProcess.Id;uiPid=$script:appPid;name=$name;root=$root;profile=$profile;pipe=$pipe} | ConvertTo-Json |
            Set-Content (Join-Path $OutputDirectory "profile-$count.json") -Encoding UTF8
        UI @('wait-for','NavPlugins','-t','10000') | Out-Null
        UI @('invoke','NavPlugins') | Out-Null
        UI @('wait-for','PluginsRunningTab','--value',"Running ($count)",'-t','20000') | Out-Null
        $startupMs=$clock.ElapsedMilliseconds
        $last=$snapshot.activePlugins[-1]
        UI @('scroll','RunningPluginsList','--to','bottom') | Out-Null
        UI @('wait-for',('running-'+$last.instanceId),'-t','5000') | Out-Null
        UI @('invoke',('running-'+$last.instanceId)) | Out-Null
        UI @('focus',('running-'+$last.instanceId)) | Out-Null
        UI @('screenshot','-o',"$OutputDirectory/running-$count.png") | Out-Null
        $accepted=Send-HostRequest $pipe 'toggle-bypass' @($snapshot.activePlugins[0].instanceId) -Session $snapshot.hostSession
        $result=Wait-HostOperation $pipe $accepted
        if ($result.status -eq 'error') { throw "Mutation failed: $($result.message)" }
        Start-Sleep -Milliseconds 700
        UI @('wait-for',('running-'+$last.instanceId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        UI @('wait-for',('running-'+$last.instanceId),'-p','IsSelected','--value','True','-t','5000') | Out-Null
        UI @('wait-for',('running-'+$last.instanceId),'-p','HasKeyboardFocus','--value','True','-t','5000') | Out-Null
        UI @('invoke','NavDashboard') | Out-Null
        UI @('invoke','NavPlugins') | Out-Null
        UI @('wait-for',('running-'+$last.instanceId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        $before=Send-HostRequest $pipe 'transport-diagnostics'
        UI @('set-value','RunningPluginSearchInput',$last.name) | Out-Null
        UI @('wait-for',('running-'+$last.instanceId),'-t','5000') | Out-Null
        $after=Send-HostRequest $pipe 'transport-diagnostics'
        if ($after.snapshotRequests -ne $before.snapshotRequests) { throw 'Search caused a network snapshot.' }
        UI @('send-keys','ctrl+a','--target','RunningPluginSearchInput','--via','send-input') | Out-Null
        UI @('send-keys','backspace','--via','send-input') | Out-Null
        $originalOrder=@((Send-HostRequest $pipe 'snapshot').activePlugins.instanceId) -join ','
        UI @('invoke','RunningPluginSort') | Out-Null
        UI @('invoke','Plugin name (Z-A)') | Out-Null
        UI @('scroll','RunningPluginsList','--to','top') | Out-Null
        UI @('wait-for',('running-'+$last.instanceId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        $previous=$snapshot.activePlugins[-2]
        UI @('wait-for',('running-'+$previous.instanceId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        UI @('drag',('running-'+$last.instanceId),('running-'+$previous.instanceId),'--hold-ms','250','--dwell-ms','300') | Out-Null
        Start-Sleep -Milliseconds 400
        if ((@((Send-HostRequest $pipe 'snapshot').activePlugins.instanceId) -join ',') -ne $originalOrder) { throw 'Visual sorting or drag in a sorted view changed the processing order.' }
        UI @('invoke','RunningPluginSort') | Out-Null
        UI @('invoke','Chain order') | Out-Null
        UI @('scroll','RunningPluginsList','--to','top') | Out-Null
        $first=$snapshot.activePlugins[0]; $second=$snapshot.activePlugins[1]
        UI @('wait-for',('running-'+$first.instanceId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        UI @('wait-for',('running-'+$second.instanceId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        UI @('drag',('running-'+$first.instanceId),('running-'+$second.instanceId),'--hold-ms','250','--dwell-ms','500') | Out-Null
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        do {
            $reordered=Send-HostRequest $pipe 'snapshot'
            if ($reordered.activePlugins[0].instanceId -eq $second.instanceId -and $reordered.activePlugins[1].instanceId -eq $first.instanceId) { break }
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Drag in chain order did not move the two UUIDs.' }
            Start-Sleep -Milliseconds 100
        } while ($true)
        if ($reordered.activePlugins.Count -ne $count -or (@($reordered.activePlugins.instanceId | Sort-Object -Unique).Count -ne $count)) { throw 'Reorder lost or duplicated an instance.' }
        UI @('invoke','PluginsInstalledTab') | Out-Null
        UI @('wait-for','InstalledPluginsList','-t','5000') | Out-Null
        UI @('invoke','InstalledPluginSort') | Out-Null
        UI @('invoke','Plugin name (A-Z)') | Out-Null
        UI @('scroll','InstalledPluginsList','--to','bottom') | Out-Null
        $known=@($snapshot.knownPluginList | Where-Object name -eq ('Test Effect '+$count.ToString('D4')))[0]
        if (!$known) { throw 'The final sorted Installed fixture is missing.' }
        UI @('wait-for',('installed-'+$known.knownId),'-t','5000') | Out-Null
        UI @('screenshot','-o',"$OutputDirectory/installed-$count.png") | Out-Null
        UI @('invoke','InstalledGroupByManufacturer') | Out-Null
        UI @('wait-for','InstalledGroupByManufacturer','--value','On','-t','5000') | Out-Null
        UI @('invoke','NavDashboard') | Out-Null
        UI @('invoke','NavPlugins') | Out-Null
        UI @('invoke','PluginsInstalledTab') | Out-Null
        UI @('wait-for','InstalledGroupByManufacturer','--value','On','-t','5000') | Out-Null
        UI @('set-value','InstalledPluginSearchInput',$known.name) | Out-Null
        UI @('wait-for',('installed-'+$known.knownId),'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
        UI @('screenshot','-o',"$OutputDirectory/grouped-search-$count.png") | Out-Null
        $uiProcess=Get-Process -Id $script:appPid
        $hostStats=Get-Process -Id $hostProcess.Id
        $results.Add([pscustomobject]@{count=$count; status='passed'; loadedProcessors=$snapshot.diagnostics.loadedPlugins; startupToListMs=$startupMs; privateBytes=$uiProcess.PrivateMemorySize64; hostPrivateBytes=$hostStats.PrivateMemorySize64; workingSet=$uiProcess.WorkingSet64; uiCpuSeconds=$uiProcess.CPU; profile=$profile; audioOpened=$false; verified=@('selection-focus-scroll','local-search','visual-sort-preserves-chain','drag-disabled-in-visual-sort','drag-by-uuid-in-chain-order','installed-grouping-navigation-search')})
    }
    catch { $results.Add([pscustomobject]@{count=$count; status='failed'; error="$($_.Exception.Message)"; profile=$profile}) }
    finally {
        if ($appPid -and (Get-Process -Id $appPid -ErrorAction SilentlyContinue)) {
            UI @('invoke','Close') | Out-Null
            Get-Process -Id $appPid -ErrorAction SilentlyContinue | Wait-Process -Timeout 15
        }
        if ($hostProcess -and !$hostProcess.HasExited) {
            $shutdownClock=[Diagnostics.Stopwatch]::StartNew()
            $hello=Send-HostRequest $pipe 'hello'
            Send-HostRequest $pipe 'quit-host' -Session $hello.hostSession | Out-Null
            if (!$hostProcess.WaitForExit(60000)) {
                $results[-1].status='failed'
                $results[-1] | Add-Member -NotePropertyName shutdownError -NotePropertyValue 'Temporary host did not exit within 60 seconds.'
            }
            $results[-1] | Add-Member -NotePropertyName shutdownMs -NotePropertyValue $shutdownClock.ElapsedMilliseconds
        }
    }
    $results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
    $results[-1] | ConvertTo-Json
    if ($results[-1].status -eq 'failed') { break }
}
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
