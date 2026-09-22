param([string]$OutputDirectory='out/callback-measurement', [switch]$RequireAllocationAudit)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root=Join-Path $repo 'out\test-profiles'
$name='callback-'+[guid]::NewGuid().ToString('N')
$profile=Join-Path $root $name
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$result=[ordered]@{profile=$profile;status='running';inputOpened=$false;outputMuted=$true;performanceAcceptance=$false}
$process=$null
function Mutate([string]$Command,[array]$Arguments=@()) {
    Wait-HostOperation $script:pipe (Send-HostRequest $script:pipe $Command $Arguments -Session $script:hostSession)
}
try {
    $process=Start-Process -FilePath "$repo\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe" -ArgumentList @("--test-profile=$name",('--profile-root="'+$root+'"')) -WindowStyle Hidden -PassThru
    $metadata=Join-Path $profile 'profile.json'
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'The isolated host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $script:pipe=(Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json).pipe
    $initial=Send-HostRequest $pipe 'snapshot'; $script:hostSession=$initial.hostSession
    if ($initial.audioSelection.driverAvailable) { throw 'The profile opened audio before explicit selection.' }
    if ((Mutate 'measure-callbacks' @(1,2)).status -ne 'ok') { throw 'The measurement could not be armed.' }
    $duplicate=Mutate 'measure-callbacks' @(0,1)
    if ($duplicate.error.code -ne 'measurement_unavailable') { throw 'The host overwrote an already armed measurement.' }
    if ((Mutate 'set-global-mute' @($true)).status -ne 'ok') { throw 'The output could not be muted before opening audio.' }
    $options=Send-HostRequest $pipe 'audio-device-options' @('Windows Audio')
    if (!$options.available -or !$options.suggestedOutput) { throw 'Shared WASAPI output is unavailable on this machine.' }
    $selection=@{backend='Windows Audio';input='';output=[string]$options.suggestedOutput;inputMask='0';outputMask='0';
        defaultInputChannels=$false;defaultOutputChannels=$true;sampleRate=48000;bufferSize=0;expectedGeneration=[string]$initial.audioSelection.generation}
    $opened=Mutate 'select-audio-device' @($selection)
    if ($opened.status -ne 'ok' -or !$opened.audioSelection.driverAvailable) { throw ('The selected output did not open: '+($opened|ConvertTo-Json -Depth 8 -Compress)) }
    $result.selection=$opened.audioSelection
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do {
        $measurement=Send-HostRequest $pipe 'callback-measurement'
        if ($measurement.phase -eq 'completed') { break }
        if ($measurement.phase -eq 'interrupted' -or [DateTime]::UtcNow -ge $deadline) { throw ('The measurement did not complete: '+($measurement|ConvertTo-Json -Compress)) }
        Start-Sleep -Milliseconds 200
    } while ($true)
    $final=Send-HostRequest $pipe 'snapshot'
    $result.measurement=$measurement; $result.diagnostics=$final.diagnostics
    if (!$final.globalMuted -or $final.diagnostics.inputChannels -ne 0) { throw 'The smoke test opened input or lost output mute.' }
    if ([uint64]$measurement.windowEndTick-[uint64]$measurement.windowStartTick -ne 2*[uint64]$measurement.frequency) { throw 'The measured interval has an incorrect duration.' }
    $count=[uint64]$measurement.callbacks; $samples=[uint64]$measurement.samples
    if (!$count -or !$samples -or $samples -gt $final.diagnostics.processedSamples -or $count -gt $final.diagnostics.processedBlocks) { throw 'Callback work was lost or miscounted.' }
    if ($samples -ne $count*[uint64]$final.diagnostics.bufferSize) { throw 'Delivered block and sample counts disagree.' }
    if ($null -eq $measurement.p95UpperTicks -or $null -eq $measurement.p99UpperTicks -or [uint64]$measurement.p95UpperTicks -gt [uint64]$measurement.p99UpperTicks) { throw 'Completed percentiles are missing or reversed.' }
    if ([double]$measurement.p99UpperTicks -gt [double]$measurement.maximumTicks*(1.0+[double]$measurement.quantileRelativeErrorBound)) { throw 'The measured percentile exceeded its declared upper-bound precision.' }
    if ($RequireAllocationAudit -and !$measurement.hostAllocationAuditAvailable) { throw 'The Release host allocation audit is not installed.' }
    if ($measurement.hostAllocationAuditAvailable -and ([uint64]$measurement.hostAllocations -ne 0 -or [uint64]$measurement.hostFrees -ne 0)) { throw 'The real host audio callback allocated or freed memory.' }
    if ($measurement.thirdPartyAllocationAuditAvailable) { throw 'The host falsely claimed coverage of third-party allocation internals.' }
    $result.status='passed'
    Write-Host "PASS: real callback measurement completed $count blocks / $samples samples, with exact window and bounded percentiles."
} catch { $result.status='failed'; $result.error=$_.Exception.Message; throw }
finally {
    try {
        if ($process -and !$process.HasExited -and $script:hostSession) {
            Send-HostRequest $script:pipe 'quit-host' -Session $script:hostSession | Out-Null
            if (!$process.WaitForExit(15000)) { throw 'The isolated host did not exit normally.' }
        }
    } catch { $result.status='failed'; $result.shutdownError=$_.Exception.Message }
    $result | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
}
if ($result.status -eq 'failed') { exit 1 }
