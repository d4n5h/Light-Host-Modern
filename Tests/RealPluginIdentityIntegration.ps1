param([string]$MetadataCache='out/real-plugin-test/ScannerMetadataCache-VST/003bb3719822beac5c81e46f5e82aa5dc610997445e9f92425e0e797801822aa.xml',
      [string]$OutputDirectory='out/real-plugin-identity')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root=Join-Path $repo 'out\test-profiles'
$hostExe=Join-Path (Get-TestBuildDirectory) 'LightHostModern_artefacts\Release\LightHostModern.exe'
[xml]$cache=Get-Content -LiteralPath $MetadataCache -Raw -Encoding UTF8
$entry=$cache.SelectSingleNode('/SCAN/ENTRY')
$plugin=$entry.SelectSingleNode('PLUGIN')
if (!$plugin -or $entry.GetAttribute('verifiedMetadata') -ne 'verified' -or $plugin.GetAttribute('format') -ne 'VST' -or
    $plugin.GetAttribute('name') -ne 'PurestGain64' -or !(Test-Path -LiteralPath $plugin.GetAttribute('file'))) {
    throw 'The officially sourced, verified PurestGain VST2 fixture is required.'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$report=[ordered]@{hostSha256=(Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash;audioOpened=$false;runs=[Collections.Generic.List[object]]::new()}
$script:hostProcess=$null; $script:pipe=''; $script:session=''
$savedInstance=$null
function Write-Preferences([string]$Destination,$Instance) {
    $xml=[xml]'<PROPERTIES/>'
    $enabled=$xml.CreateElement('VALUE'); $enabled.SetAttribute('name','enableVst2'); $enabled.SetAttribute('val','1'); [void]$xml.DocumentElement.AppendChild($enabled)
    $bank=$xml.CreateElement('VALUE'); $bank.SetAttribute('name','pluginList'); [void]$xml.DocumentElement.AppendChild($bank)
    $known=$xml.CreateElement('KNOWNPLUGINS'); [void]$bank.AppendChild($known); [void]$known.AppendChild($xml.ImportNode($plugin,$true))
    $value=$xml.CreateElement('VALUE'); $value.SetAttribute('name','pluginInstancesV1'); [void]$xml.DocumentElement.AppendChild($value)
    $chain=$xml.CreateElement('LIGHTHOSTSESSION'); $chain.SetAttribute('version','1'); $chain.SetAttribute('revision','0'); [void]$value.AppendChild($chain)
    [void]$chain.AppendChild($xml.ImportNode($Instance,$true))
    $settings=[Xml.XmlWriterSettings]::new(); $settings.Encoding=[Text.UTF8Encoding]::new($false); $settings.Indent=$true
    $writer=[Xml.XmlWriter]::Create($Destination,$settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
}
function Class-Identity($Description) {
    $uid=$Description.GetAttribute('uniqueId'); if (!$uid -or $uid.TrimStart('0') -eq '') { $uid=$Description.GetAttribute('uid') }
    $uid=$uid.TrimStart('0').ToLowerInvariant(); if (!$uid) { $uid='0' }
    $identity=''
    foreach ($field in @($Description.GetAttribute('format'),$Description.GetAttribute('file'),$uid)) {
        $length=[regex]::Matches($field,'[\uD800-\uDBFF][\uDC00-\uDFFF]|[\s\S]').Count
        $identity+=[string]$length+':'+$field
    }
    $hash=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity)))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
if ((Class-Identity $plugin) -ne $entry.GetAttribute('knownId')) { throw 'Fixture identity encoding differs from the scanner.' }
function Start-Host([string]$Name,[string]$Directory) {
    $script:pipe=''; $script:session=''
    $script:hostProcess=Start-Process -FilePath $hostExe -ArgumentList @("--test-profile=$Name",('--profile-root="'+$root+'"')) -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do {
        $metadata=Join-Path $Directory 'profile.json'
        if (Test-Path -LiteralPath $metadata) {
            try {
                $info=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
                if ($info.pid -eq $script:hostProcess.Id) {
                    $script:pipe=$info.pipe
                    $snapshot=Send-HostRequest $script:pipe 'snapshot'
                    if ($snapshot.hostSession) { $script:session=$snapshot.hostSession; return $snapshot }
                }
            } catch { }
        }
        if ($script:hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'The identity test host did not start.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Stop-Host {
    if ($script:hostProcess -and !$script:hostProcess.HasExited) {
        if (!$script:pipe) { throw 'The test host has no shutdown endpoint.' }
        Send-HostRequest $script:pipe 'quit-host' -Session $script:session | Out-Null
        if (!$script:hostProcess.WaitForExit(15000)) { throw 'The identity test host did not finish saving and closing.' }
    }
}
foreach ($scenario in @('exact-class','replaced-class')) {
    $name='identity-'+[guid]::NewGuid().ToString('N'); $directory=Join-Path $root $name
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $preferences=Join-Path $directory 'LightHostModern.settings'
    $run=[ordered]@{scenario=$scenario;profile=$directory;status='running'}; $report.runs.Add($run)
    try {
        if (!$savedInstance) {
            $xml=[xml]'<INSTANCE id="11111111111111111111111111111111" identityResolved="1" bypassed="0" customName="" loading="unloaded" error="" stateCaptureAllowed="1"><STATE/></INSTANCE>'
            [void]$xml.DocumentElement.AppendChild($xml.ImportNode($plugin,$true))
            $xml.DocumentElement.SetAttribute('identity',$entry.GetAttribute('knownId')); $instance=$xml.DocumentElement
        } else {
            $instance=$savedInstance.CloneNode($true)
            $description=$instance.SelectSingleNode('PLUGIN'); $description.SetAttribute('uniqueId','badcafe'); $description.SetAttribute('uid','badcafe')
            $instance.SetAttribute('identity',(Class-Identity $description)); $instance.SetAttribute('identityResolved','0')
            $instance.SetAttribute('loading','unloaded'); $instance.SetAttribute('error','')
        }
        Write-Preferences $preferences $instance
        $snapshot=Start-Host $name $directory
        if ($snapshot.audioSelection.driverAvailable -or $snapshot.activePlugins.Count -ne 1) { throw 'The isolated identity scenario opened audio or lost its instance.' }
        if ($scenario -eq 'exact-class') {
            if ($snapshot.activePlugins[0].loading -ne 'loaded' -or $snapshot.diagnostics.loadedPlugins -ne 1) { throw 'The verified class did not load.' }
        } else {
            $details=Send-HostRequest $script:pipe 'instance-details' @($snapshot.activePlugins[0].instanceId)
            if ($snapshot.diagnostics.loadedPlugins -ne 0 -or $details.error -ne 'plugin_identity_mismatch') { throw ('A replaced class was not rejected before restoration: '+($details|ConvertTo-Json -Compress)) }
            $run.errorCode=$details.error
        }
        Stop-Host
        $sessionFile=Get-Content -LiteralPath ($preferences+'.session.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        [xml]$sessionXml=$sessionFile.sessionXml
        $persisted=$sessionXml.SelectSingleNode('/LIGHTHOSTSESSION/INSTANCE')
        if ($scenario -eq 'exact-class') {
            if (!$persisted.SelectSingleNode('STATE').InnerText) { throw 'The real plugin did not provide a state to preserve.' }
            $savedInstance=$persisted.CloneNode($true)
        } elseif ($persisted.GetAttribute('id') -ne $savedInstance.GetAttribute('id') -or
            $persisted.SelectSingleNode('STATE').InnerText -ne $savedInstance.SelectSingleNode('STATE').InnerText -or
            $persisted.GetAttribute('error') -ne 'plugin_identity_mismatch') { throw 'Rejecting the replaced class lost saved state, UUID or recovery reason.' }
        $run.status='passed'; Write-Output "PASS: $scenario"
    } catch { $run.status='failed'; $run.error=$_.Exception.Message; Write-Output "FAIL: $scenario - $($run.error)" }
    finally {
        try { Stop-Host } catch { $run.status='failed'; $run.shutdownError=$_.Exception.Message }
        $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
    }
    if ($run.status -eq 'failed') { break }
}
if (@($report.runs|Where-Object status -eq 'failed').Count) { exit 1 }
