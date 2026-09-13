param([string] $HostExecutable = "$PSScriptRoot\..\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root = Join-Path $repo 'out\test-profiles'
$name = 'scan-' + [guid]::NewGuid().ToString('N')
$profileDirectory = Join-Path $root $name
$metadata = Join-Path $profileDirectory 'profile.json'
$process = $null
function Wait-ScanIdle {
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        $status = Send-HostRequest -PipeName $info.pipe -Command 'plugin-scan-status'
        if (!$status.active) { return $status }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Scan did not become idle.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
try {
    $process = Start-Process -FilePath (Resolve-Path -LiteralPath $HostExecutable).Path -ArgumentList @("--test-profile=$name", "--profile-root=`"$root`"") -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Test host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    $session = $snapshot.hostSession
    if ($snapshot.diagnostics.recoveryState -ne 'suspended') { throw 'Audio unexpectedly active.' }
    $accepted = Send-HostRequest -PipeName $info.pipe -Command 'begin-plugin-scan' -Session $session
    if ((Wait-HostOperation -PipeName $info.pipe -Accepted $accepted).status -ne 'ok') { throw 'Begin failed.' }
    $paths = @(0..150 | ForEach-Object { Join-Path $profileDirectory ('missing-audio-' + $_) }) -join ';'
    $accepted = Send-HostRequest -PipeName $info.pipe -Command 'scan-plugin-path' -Arguments @($paths) -Session $session
    if ((Wait-HostOperation -PipeName $info.pipe -Accepted $accepted).status -ne 'ok') { throw 'Enqueue failed.' }
    $status = Wait-ScanIdle
    if ($status.failureCount -lt 151) { throw 'Missing root failures were omitted.' }
    $items = @()
    while ($items.Count -lt $status.failureCount) {
        $page = Send-HostRequest -PipeName $info.pipe -Command 'plugin-scan-failures' -Arguments @(@{scanId=$status.scanId; revision=$status.revision; offset=$items.Count; limit=100})
        if ($page.status -ne 'ok' -or $page.failures.Count -lt 1 -or $page.failures.Count -gt 100) { throw 'Invalid failure page.' }
        $items += $page.failures
    }
    if (@($items.id | Select-Object -Unique).Count -ne $items.Count) { throw 'Failure IDs collided.' }
    foreach ($item in $items) {
        if (!$item.path.StartsWith($profileDirectory) -or $item.kind -ne 'enumeration' -or $item.attempt -ne 1) { throw 'Failure details missing or incorrect.' }
    }
    $selectedId = $items[-1].id
    $accepted = Send-HostRequest -PipeName $info.pipe -Command 'retry-plugin-scan-selection' -Arguments @(@{scanId=$status.scanId; revision=$status.revision; ids=@($selectedId)}) -Session $session
    if ((Wait-HostOperation -PipeName $info.pipe -Accepted $accepted).status -ne 'ok') { throw 'Selected retry failed.' }
    $retried = Wait-ScanIdle
    $stale = Send-HostRequest -PipeName $info.pipe -Command 'plugin-scan-failures' -Arguments @(@{scanId=$status.scanId; revision=$status.revision; offset=0; limit=100})
    if ($stale.error.code -ne 'stale_revision') { throw 'Stale page accepted.' }
    $last = Send-HostRequest -PipeName $info.pipe -Command 'plugin-scan-failures' -Arguments @(@{scanId=$retried.scanId; revision=$retried.revision; offset=($items.Count-1); limit=100})
    if ($last.failures[0].id -ne $selectedId -or $last.failures[0].attempt -ne 2) { throw 'Selected retry did not preserve the ID and increment the attempt.' }
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    if ($snapshot.diagnostics.sampleRate -ne $null -or $snapshot.diagnostics.recoveryState -ne 'suspended') { throw 'Scanner opened an audio device.' }
    Send-HostRequest -PipeName $info.pipe -Command 'quit-host' -Session $session | Out-Null
    if (!$process.WaitForExit(10000)) { throw 'Host did not shut down.' }
    @{passed=$true; failureCount=$items.Count; scanId=$status.scanId; hostSession=$session; realAudioOpened=$false} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $profileDirectory 'scan-result.json') -Encoding UTF8
    Write-Output "PASS isolated scan, complete failure pages, stable retry IDs and revision checks: $profileDirectory"
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
}
