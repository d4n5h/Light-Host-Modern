param([Parameter(Mandatory)][string]$HostExecutable,
      [string]$OutputDirectory='out/verbose-log-ui')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=(Resolve-Path "$PSScriptRoot/..").Path
$root=Join-Path $repo 'out/test-profiles'
$testProfileName='verbose-ui-'+[guid]::NewGuid().ToString('N')
$profile=Join-Path $root $testProfileName
New-Item -ItemType Directory -Force -Path $profile,$OutputDirectory | Out-Null
$results=[Collections.Generic.List[object]]::new()
$hostProcess=$null; $uiProcess=$null; $pipe=''; $session=''
function UI([string[]]$Arguments) {
    $result=& winapp ui @Arguments -a $script:uiProcess.Id --json
    if ($LASTEXITCODE -ne 0) { throw "UI command failed: $result" }
    $result | ConvertFrom-Json
}
function Find-Ui {
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do {
        $found=Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains("--test-profile=$testProfileName") }
        if ($found) {
            if (@($found).Count -ne 1) { throw 'Duplicate UI processes for one profile.' }
            $script:uiProcess=Get-Process -Id $found.ProcessId
            if ($script:uiProcess.MainWindowHandle -ne [IntPtr]::Zero) { break }
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Host-launched UI did not become ready.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    UI @('wait-for','NavDashboard','-t','10000') | Out-Null
}
function Start-Host {
    $script:hostProcess=Start-Process -FilePath $HostExecutable -ArgumentList @("--test-profile=$testProfileName","--profile-root=`"$root`"",'--show-ui') -WindowStyle Hidden -PassThru
    $metadata=Join-Path $profile 'profile.json'; $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do {
        if ((Test-Path $metadata) -and (Get-Content $metadata -Raw | ConvertFrom-Json).pid -eq $hostProcess.Id) { break }
        if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Host startup failed.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $script:pipe=(Get-Content $metadata -Raw | ConvertFrom-Json).pipe
    $script:session=(Send-HostRequest $pipe 'hello').hostSession
    Find-Ui
    $snapshot=Send-HostRequest $pipe 'snapshot'
    if ($snapshot.audioSelection.processingAvailable) { throw 'Temporary profile opened audio.' }
}
function Mutate([string]$Command,[object[]]$Arguments=@()) {
    $accepted=Send-HostRequest $pipe $Command $Arguments -Session $session
    $result=Wait-HostOperation $pipe $accepted
    if ($result.status -ne 'ok') { throw ($result | ConvertTo-Json -Depth 6) }
}
function Scenario([string]$Name,[scriptblock]$Work) {
    & $Work
    $results.Add(@{name=$Name;status='passed'})
    Write-Host "PASS: $Name"
}

function Phase([string]$Expected) {
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do {
        $value=Send-HostRequest $pipe 'verbose-log-status'
        if ($value.phase -eq $Expected) { return $value }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Expected logging $Expected; got $($value.phase)." }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
try {
    Start-Host
    UI @('invoke','NavDiagnostics') | Out-Null
    Phase 'off' | Out-Null
    Scenario 'Arming requires restart and restart-later does not collect' {
        UI @('invoke','TrackVerboseLogs') | Out-Null
        UI @('wait-for','CloseButton','-t','5000') | Out-Null
        UI @('invoke','CloseButton') | Out-Null
        $armed=Phase 'armed'
        if ($armed.bytes -ne 0) { throw 'Collected before restart.' }
        UI @('wait-for','VerboseLogsRestartLink','-t','5000') | Out-Null
    }
    Scenario 'Waiting link reopens confirmation and restarts both processes' {
        $oldHost=$hostProcess; $oldUi=$uiProcess
        UI @('invoke','VerboseLogsRestartLink') | Out-Null
        UI @('wait-for','PrimaryButton','-t','5000') | Out-Null
        UI @('invoke','PrimaryButton') | Out-Null
        if (!$oldHost.WaitForExit(15000) -or !$oldUi.WaitForExit(5000)) { throw 'Restart left old processes alive.' }
        $deadline=[DateTime]::UtcNow.AddSeconds(25)
        do {
            $metadata=Get-Content (Join-Path $profile 'profile.json') -Raw | ConvertFrom-Json
            if ($metadata.pid -ne $oldHost.Id -and (Get-Process -Id $metadata.pid -ErrorAction SilentlyContinue)) { break }
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Restart did not launch host.' }
            Start-Sleep -Milliseconds 100
        } while ($true)
        $script:hostProcess=Get-Process -Id $metadata.pid
        $script:pipe=$metadata.pipe
        $script:session=(Send-HostRequest $pipe 'hello').hostSession
        Find-Ui
        $capture=Phase 'collecting'
        if (!$capture.session) { throw 'Missing capture identity.' }
        UI @('invoke','NavDiagnostics') | Out-Null
        UI @('wait-for','SaveVerboseLogs','-t','5000') | Out-Null
    }
    Scenario 'Cancel export retains stopped capture and allows saving again' {
        Mutate 'set-global-mute' @($true)
        UI @('click','SaveVerboseLogs') | Out-Null
        UI @('wait-for','1001','-t','5000') | Out-Null
        UI @('send-keys','escape','--via','send-input') | Out-Null
        Phase 'stopped' | Out-Null
        UI @('wait-for','SaveVerboseLogs','-t','5000') | Out-Null
    }
    Scenario 'Native TXT save exports both processes and turns logging off without restart' {
        $savedHost=$hostProcess.Id; $savedUi=$uiProcess.Id
        UI @('click','SaveVerboseLogs') | Out-Null
        UI @('wait-for','1001','-t','5000') | Out-Null
        $destination=[IO.Path]::GetFullPath((Join-Path $OutputDirectory 'capture.txt'))
        UI @('set-value','1001',$destination) | Out-Null
        UI @('send-keys','enter','--target','1001','--via','send-input') | Out-Null
        Phase 'off' | Out-Null
        $text=Get-Content -LiteralPath $destination -Raw
        if ($text -notmatch 'LightHostModern diagnostic capture' -or $text -notmatch 'set-global-mute' -or $text -notmatch 'ui-' -or $text -notmatch 'host-') { throw 'Export omitted expected capture evidence.' }
        if ($hostProcess.Id -ne $savedHost -or $uiProcess.Id -ne $savedUi -or $hostProcess.HasExited -or $uiProcess.HasExited) { throw 'Saving restarted the app.' }
        UI @('wait-for','TrackVerboseLogs','-p','ToggleState','--value','Off','-t','5000') | Out-Null
        UI @('screenshot','-o',"$OutputDirectory/saved.png") | Out-Null
    }
} catch { $results.Add(@{name='Verbose logs UI integration';status='failed';error=$_.Exception.Message}); throw }
finally {
    if ($hostProcess -and !$hostProcess.HasExited) {
        try { Send-HostRequest $pipe 'quit-host' -Session $session | Out-Null } catch {}
        if (!$hostProcess.WaitForExit(15000)) { Stop-Process -Id $hostProcess.Id -Force }
    }
    if ($uiProcess -and !$uiProcess.HasExited) { Stop-Process -Id $uiProcess.Id -Force }
    $results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
}
