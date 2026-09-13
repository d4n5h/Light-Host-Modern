param([Parameter(Mandatory)][string]$Metadata,
      [string]$OutputFile='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root=Join-Path $repo 'out\test-profiles'
$info=Get-Content -LiteralPath $Metadata -Raw -Encoding UTF8 | ConvertFrom-Json
if (!$info.name -or ![IO.Path]::GetFullPath($info.profile).StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path $info.profile -Leaf) -ne $info.name) { throw 'A workspace test profile is required.' }
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.hostPid)"
if (!$process -or !$process.ExecutablePath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or
    !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw 'The host no longer belongs to this test profile.' }
$snapshot=Send-HostRequest $info.pipe 'snapshot'
if ($OutputFile) { $snapshot | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $OutputFile -Encoding UTF8 }
[ordered]@{profile=$info.name;hostPid=$info.hostPid;hostSession=$snapshot.hostSession;
    globalMuted=$snapshot.globalMuted;globalBypassed=$snapshot.globalBypassed;
    driverAvailable=$snapshot.audioSelection.driverAvailable;loadedPlugins=$snapshot.diagnostics.loadedPlugins;
    uiProcesses=@(Get-CimInstance Win32_Process -Filter "Name='LightHostWinUI.exe'" | Where-Object {
        $_.CommandLine.Contains('--test-profile='+$info.name)
    } | Select-Object ProcessId,ExecutablePath,CommandLine)} | ConvertTo-Json -Depth 5
