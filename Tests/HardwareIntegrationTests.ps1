# Opens actual enumerated drivers only in newly created temporary profiles.
# Output is muted before opening any device; samples are counted, never recorded.
param([string[]]$Backends = @('Windows Audio', 'Windows Audio (Exclusive Mode)', 'Windows Audio (Low Latency Mode)', 'DirectSound', 'ASIO'),
      [string]$OutputDirectory = 'out/hardware-validation')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$profileRoot = Join-Path $repo 'out\test-profiles'
$executable = Join-Path (Get-TestBuildDirectory) 'LightHostModern_artefacts\Release\LightHostModern.exe'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$productionFiles = @((Join-Path $env:APPDATA 'LightHostModern\LightHostModern.settings'), (Join-Path $env:LOCALAPPDATA 'LightHostModern\ui-settings.ini'))
$productionBefore = @{}
foreach ($file in $productionFiles) { $productionBefore[$file] = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { '' } }
$results = [Collections.Generic.List[object]]::new()
function Snapshot { Send-HostRequest $script:pipe 'snapshot' -TimeoutMs 10000 }
function Mutate([string]$Command, [object[]]$Arguments=@()) {
    $accepted = Send-HostRequest -PipeName $script:pipe -Command $Command -Arguments $Arguments -Session $script:hostSession
    Wait-HostOperation -PipeName $script:pipe -Accepted $accepted -TimeoutMs 60000
}
function Selection($Snapshot, [bool]$Effective = $true) {
    $setup = if ($Effective -and $Snapshot.audioSelection.driverAvailable) { $Snapshot.audioSelection.effective } else { $Snapshot.audioSelection.configured }
    $request = @{}
    foreach ($property in $setup.PSObject.Properties) { $request[$property.Name] = $property.Value }
    $request.expectedGeneration = [string]$Snapshot.audioSelection.generation
    return $request
}
function Select-Audio($Request) {
    $result = Mutate 'select-audio-device' @($Request)
    if ($result.status -ne 'ok') { throw ('Audio selection failed: ' + ($result | ConvertTo-Json -Depth 6 -Compress)) }
    return $result
}
foreach ($backend in $Backends) {
    $testProfileName = 'hardware-' + [guid]::NewGuid().ToString('N')
    $profileDirectory = Join-Path $profileRoot $testProfileName
    $metadata = Join-Path $profileDirectory 'profile.json'
    $row = [ordered]@{ backend=$backend; profile=$profileDirectory; status='running'; scenarios=@(); physicalHotplug='not_executed'; audioOutputMuted=$true }
    $process = $null
    try {
        $process = Start-Process -FilePath $executable -ArgumentList @("--test-profile=$testProfileName", ('--profile-root="'+$profileRoot+'"')) -WindowStyle Hidden -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        while (!(Test-Path -LiteralPath $metadata)) {
            if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Temporary host did not start.' }
            Start-Sleep -Milliseconds 100
        }
        $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
        $script:pipe = $info.pipe
        $initial = Snapshot; $script:hostSession = $initial.hostSession
        if ($initial.audioSelection.driverAvailable -or $initial.activePlugins.Count) { throw 'Hardware profile was not initially empty and suspended.' }
        $mute = Mutate 'set-global-mute' @($true)
        if ($mute.status -ne 'ok') { throw 'Output could not be muted before driver open.' }
        $options = Send-HostRequest $script:pipe 'audio-device-options' @($backend)
        $row.options = $options
        if (!$options.available -or (!$options.suggestedInput -and !$options.suggestedOutput)) {
            $row.status = 'unavailable'; continue
        }
        $request = @{backend=$backend; input=[string]$options.suggestedInput; output=[string]$options.suggestedOutput;
            inputMask='0'; outputMask='0'; defaultInputChannels=$true; defaultOutputChannels=$true; sampleRate=0; bufferSize=0;
            expectedGeneration=[string]$initial.audioSelection.generation}
        $candidates = [Collections.Generic.List[object]]::new()
        $candidates.Add($request.Clone())
        foreach ($size in @(512, 1024)) {
            $candidate = $request.Clone(); $candidate.sampleRate=48000; $candidate.bufferSize=$size; $candidates.Add($candidate)
        }
        if ($options.separateInputsAndOutputs) {
            foreach ($role in @('input','output')) {
                $candidate=$request.Clone(); $candidate[$role]=''; $candidate.sampleRate=48000; $candidate.bufferSize=512; $candidates.Add($candidate)
            }
        } else {
            foreach ($device in $options.outputs) {
                if ($device -eq $request.output) { continue }
                $candidate=$request.Clone(); $candidate.input=$device; $candidate.output=$device; $candidates.Add($candidate)
            }
        }
        $row.openAttempts=@(); $opened=$null
        foreach ($candidate in $candidates) {
            $candidate.expectedGeneration=[string](Snapshot).audioSelection.generation
            $attempt=Mutate 'select-audio-device' @($candidate)
            $row.openAttempts += @{request=$candidate; result=$attempt}
            if ($attempt.status -eq 'ok' -and $attempt.audioSelection.driverAvailable) { $opened=$attempt; break }
            if ($attempt.error.code -ne 'audio_configuration_failed') { throw ('Unexpected open error: '+($attempt|ConvertTo-Json -Depth 8 -Compress)) }
        }
        if (!$opened) { $row.status='unavailable'; $row.error='Enumerated drivers rejected every tested configuration; this backend was not validated.'; continue }
        if (!$opened.audioSelection.driverAvailable) { throw 'Successful selection did not open a driver.' }
        $before = Snapshot
        Start-Sleep -Seconds 3
        $active = Snapshot
        if (!$active.globalMuted -or $active.diagnostics.processedSamples -le $before.diagnostics.processedSamples -or $active.diagnostics.processedBlocks -le $before.diagnostics.processedBlocks) { throw 'Driver did not deliver audio callbacks.' }
        if ($active.diagnostics.processFailures -ne 0) { throw 'Real driver processing failed.' }
        $row.scenarios += @{ name='open and process'; status='passed'; effective=$active.audioSelection.effective;
            blocks=$active.diagnostics.processedBlocks-$before.diagnostics.processedBlocks; samples=$active.diagnostics.processedSamples-$before.diagnostics.processedSamples; xruns=$active.diagnostics.xRunCount }
        $original = Selection $active
        $rate = @($active.audioConfig.sampleRates | Where-Object { $_ -ne $active.audioSelection.effective.sampleRate }) | Select-Object -First 1
        $buffer = @($active.audioConfig.bufferSizes | Where-Object { $_ -ne $active.audioSelection.effective.bufferSize -and $_ -ge 128 }) | Select-Object -First 1
        foreach ($change in @(@{key='sampleRate'; value=$rate}, @{key='bufferSize'; value=$buffer})) {
            if (!$change.value) { $row.scenarios += @{name=$change.key; status='unavailable'}; continue }
            $state = Snapshot; $request = Selection $state; $request[$change.key] = $change.value
            $changed = Mutate 'select-audio-device' @($request)
            $actual = Snapshot
            if ($changed.status -eq 'ok') {
                if (!$actual.audioSelection.driverAvailable -or !$actual.globalMuted) { throw 'Reconfiguration lost the muted driver.' }
                $row.scenarios += @{name=$change.key; status='passed'; requested=$request; effective=$actual.audioSelection.effective}
            } else {
                if ($changed.error.code -ne 'audio_configuration_failed' -or !$actual.audioSelection.driverAvailable) { throw 'Unsupported format did not leave a valid permitted configuration.' }
                $row.scenarios += @{name=$change.key; status='driver_rejected'; requested=$request; error=$changed.error; effective=$actual.audioSelection.effective}
            }
        }
        $state = Snapshot; $request = Selection $state
        $request.inputMask = if ($state.audioConfig.maxInputChannels -gt 0) { '1' } else { '0' }
        $request.outputMask = if ($state.audioConfig.maxOutputChannels -gt 0) { '1' } else { '0' }
        $request.defaultInputChannels = $false; $request.defaultOutputChannels = $false
        $channels = Select-Audio $request
        if ($channels.audioSelection.effective.inputMask -ne $request.inputMask -or $channels.audioSelection.effective.outputMask -ne $request.outputMask) { throw 'Explicit channel masks were not reflected by the driver.' }
        $row.scenarios += @{name='channel masks'; status='passed'; effective=$channels.audioSelection.effective}
        $state = Snapshot; $none = Selection $state
        $none.input=''; $none.output=''; $none.inputMask='0'; $none.outputMask='0'
        Select-Audio $none | Out-Null
        Start-Sleep -Seconds 1
        if ((Snapshot).audioSelection.driverAvailable) { throw 'None reopened a driver.' }
        $state = Snapshot; $original.expectedGeneration=[string]$state.audioSelection.generation
        $reopened = Select-Audio $original
        if (!$reopened.audioSelection.driverAvailable) { throw 'Driver did not reopen after explicit None.' }
        $row.scenarios += @{name='close and reopen driver'; status='passed'; effective=$reopened.audioSelection.effective}
        $row.final = Snapshot; $row.status='passed'
    } catch { $row.status='failed'; $row.error="$($_.Exception.Message)" }
    finally {
        if ($process -and !$process.HasExited) {
            try { Send-HostRequest -PipeName $script:pipe -Command 'quit-host' -Session $script:hostSession -TimeoutMs 2000 | Out-Null } catch {}
            if (!$process.WaitForExit(10000)) {
                # Only this explicitly created temporary process is terminated.
                if ($process.Path -ne $executable) { throw 'Temporary process identity changed.' }
                Stop-Process -Id $process.Id -Force
                $row.forcedShutdown=$true
            }
        }
        $results.Add([pscustomobject]$row)
        $results | ConvertTo-Json -Depth 16 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
        Write-Output "$backend`: $($row.status) $($row.error)"
    }
}
foreach ($file in $productionFiles) {
    $after = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { '' }
    if ($after -ne $productionBefore[$file]) { throw "Production preferences changed: $file" }
}
@{hostSha256=(Get-FileHash -LiteralPath $executable).Hash; productionPreferencesUnchanged=$true; computer=(Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer,Model,TotalPhysicalMemory); capturedUtc=[DateTime]::UtcNow.ToString('o')} |
    ConvertTo-Json -Depth 5 | Set-Content "$OutputDirectory/environment.json" -Encoding UTF8
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
