param([ValidatePattern('^[a-z][a-z0-9-]{0,23}$')][string]$Purpose='ui-test',
      [Parameter(Mandatory)][string]$OutputDirectory,
      [string]$ExistingProfile='', [string]$PreferencesFixture='', [switch]$HostOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$testRoot=Join-Path $repo 'out\test-profiles'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$startedHost=$false
$uiStarted=$false
$info=$null
$hello=$null
try {
if ($ExistingProfile) {
    if ($PreferencesFixture) { throw 'A preferences fixture can only seed a new temporary profile.' }
    $info=Get-Content -LiteralPath $ExistingProfile -Raw | ConvertFrom-Json
    if (!$info.name -or ![IO.Path]::GetFullPath($info.profile).StartsWith($testRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $info.profile -Leaf) -ne $info.name) { throw 'An isolated workspace profile is required.' }
    $process=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.hostPid)"
    if (!$process -or !$process.ExecutablePath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw 'The existing host no longer belongs to this profile.' }
    if ($info.uiPid -and (Get-Process -Id $info.uiPid -ErrorAction SilentlyContinue)) { throw 'Close the existing test UI before reopening it.' }
} else {
    $name=$Purpose+'-'+[guid]::NewGuid().ToString('N')
    $directory=Join-Path $testRoot $name
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    if ($PreferencesFixture) { Copy-Item -LiteralPath $PreferencesFixture -Destination (Join-Path $directory 'Light Host Modern.settings') }
    $hostProcess=Start-Process -FilePath "$repo\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" -ArgumentList @("--test-profile=$name", ('--profile-root="'+$testRoot+'"')) -WindowStyle Hidden -PassThru
    $startedHost=$true
    $metadata=Join-Path $directory 'profile.json'
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'The isolated host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $profile=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    $info=[pscustomobject]@{hostPid=$hostProcess.Id;uiPid=0;name=$name;root=$testRoot;profile=$directory;pipe=$profile.pipe}
}
    $hello=Send-HostRequest $info.pipe 'hello'
    if (!$hello.hostSession) { throw 'The isolated host did not provide its session identity.' }
    if ($startedHost -and (Send-HostRequest $info.pipe 'snapshot').audioSelection.driverAvailable) { throw 'A new temporary profile must not open audio automatically.' }
    if ($HostOnly) {
        $info | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'profile.json') -Encoding UTF8
        $info | ConvertTo-Json
        return
    }
    $launch=rtk proxy winapp run "$repo\WinUI\x64\Release\LightHost.WinUI" --manifest "$repo\WinUI\LightHost.WinUI\Package.appxmanifest" --exe LightHostWinUI.exe --detach --json -- "--test-profile=$($info.name)" "--profile-root=$($info.root)" "--host-pipe=$($info.pipe)" | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or !$launch.ProcessId) { throw 'The isolated UI did not start.' }
    $info.uiPid=$launch.ProcessId
    $uiStarted=$true
    $info | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'profile.json') -Encoding UTF8
    rtk proxy winapp ui wait-for NavDashboard -a $info.uiPid -t 10000 --json | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'The isolated UI did not become ready.' }
    $info | ConvertTo-Json
} catch {
    $startupError=$_
    if ($uiStarted) {
        try {
            $uiProcess=Get-Process -Id $info.uiPid -ErrorAction SilentlyContinue
            if ($uiProcess -and !$uiProcess.HasExited) {
                rtk proxy winapp ui invoke Close -a $info.uiPid --json | Out-Null
                if ($LASTEXITCODE -ne 0 -or !$uiProcess.WaitForExit(15000)) { throw 'The failed test UI did not close.' }
            }
        } catch { Write-Warning "Test UI cleanup: $($_.Exception.Message)" }
    }
    if ($startedHost -and !$hostProcess.HasExited) {
        try {
            if (!$info -and (Test-Path -LiteralPath $metadata)) {
                $profile=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
                $info=[pscustomobject]@{hostPid=$hostProcess.Id;uiPid=0;name=$name;root=$testRoot;profile=$directory;pipe=$profile.pipe}
            }
            if (!$info.pipe) { throw 'No IPC endpoint became available for normal shutdown.' }
            if (!$hello.hostSession) { $hello=Send-HostRequest $info.pipe 'hello' }
            Send-HostRequest $info.pipe 'quit-host' -Session $hello.hostSession | Out-Null
            if (!$hostProcess.WaitForExit(15000)) { throw 'The failed test host did not finish closing.' }
        } catch { Write-Warning "Test host cleanup: $($_.Exception.Message)" }
    }
    throw $startupError
}
