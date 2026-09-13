param([Parameter(Mandatory)][string]$ProfileInfo,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$info=Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.hostPid)"
if (!$process -or !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw 'An isolated host is required.' }
$snapshot=Send-HostRequest $info.pipe snapshot
if ($snapshot.audioSelection.driverAvailable) { throw 'This transport test must not open audio.' }
$meterPipe=$info.pipe+'-meters'
$first=Send-HostRequest $meterPipe meter-levels
if ($first.status -ne 'ok' -or $first.inputPeak -ne 0 -or $first.outputPeak -ne 0) { throw 'Stopped audio did not report zero peaks.' }
$wrong=Send-HostRequest $meterPipe set-global-mute -Arguments @($true) -Session $snapshot.hostSession
if ($wrong.status -ne 'error' -or $wrong.error.code -ne 'wrong_transport') { throw 'Meter endpoint accepted a state-changing operation.' }
# Occupy the command connection without providing a request. It must not
# delay meter reads or expose the audio callback to a transport lock.
$blocked=[IO.Pipes.NamedPipeClientStream]::new('.', $info.pipe.Replace('\\.\pipe\',''), [IO.Pipes.PipeDirection]::InOut)
$durations=@()
try {
    $blocked.Connect(2000)
    for($i=0;$i -lt 20;$i++) {
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $response=Send-HostRequest $meterPipe meter-levels -TimeoutMs 1000
        $durations+=$watch.Elapsed.TotalMilliseconds
        if($response.hostSession -ne $snapshot.hostSession -or $response.status -ne 'ok') { throw 'Invalid meter session or response.' }
    }
} finally { $blocked.Dispose() }
$sorted=@($durations | Sort-Object)
if($sorted[[Math]::Ceiling($sorted.Count*.95)-1] -gt 150) { throw 'Meter reads waited behind the blocked command connection.' }
$after=Send-HostRequest $info.pipe snapshot
if($after.globalMuted -ne $snapshot.globalMuted) { throw 'Rejected command changed global mute.' }
# Leave a meter connection pending while closing the host normally.
$pending=[IO.Pipes.NamedPipeClientStream]::new('.', $meterPipe.Replace('\\.\pipe\',''), [IO.Pipes.PipeDirection]::InOut)
try {
    $pending.Connect(2000)
    $hostProcess=Get-Process -Id $info.hostPid
    $watch=[Diagnostics.Stopwatch]::StartNew()
    Send-HostRequest $info.pipe quit-host -Session $snapshot.hostSession | Out-Null
    if(!$hostProcess.WaitForExit(3000)) { throw 'Pending meter connection prevented normal shutdown.' }
    $shutdown=$watch.Elapsed.TotalMilliseconds
} finally { $pending.Dispose() }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
[ordered]@{status='passed';blockedCommandMeterResponseMs=$durations;p95Ms=$sorted[[Math]::Ceiling($sorted.Count*.95)-1];shutdownMs=$shutdown;audioOpened=$false;mutationRejected=$true} |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $OutputDirectory 'meter-transport.json')
