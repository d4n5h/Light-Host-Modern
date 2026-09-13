param([Parameter(Mandatory)][string] $PackageDirectory,
      [string] $ExpectedVersion = '1.3.0')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$root = [IO.Path]::GetFullPath($PackageDirectory)
if (!$root.StartsWith((Join-Path $repo 'out') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Package inspection requires a workspace output directory.' }
$metadata = Get-Content -LiteralPath (Join-Path $root 'release-artifacts.json') -Raw | ConvertFrom-Json
$results = [Collections.Generic.List[object]]::new()
function Assert([bool] $Condition, [string] $Message) { if (!$Condition) { throw $Message } }
function Scenario([string] $Name, [scriptblock] $Work) {
    try { & $Work; $results.Add([ordered]@{ name = $Name; status = 'passed' }) }
    catch { $results.Add([ordered]@{ name = $Name; status = 'failed'; error = $_.Exception.Message }) }
}
Scenario 'Both local artifacts match their published size and SHA-256 metadata' {
    Assert ($metadata.formatVersion -eq 1 -and $metadata.artifacts.Count -eq 2) 'Invalid artifact metadata'
    foreach ($artifact in $metadata.artifacts) {
        $file = Get-Item -LiteralPath (Join-Path $root $artifact.name)
        Assert ($file.Length -eq $artifact.size -and $artifact.digest -eq ('sha256:' + (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant())) 'Artifact size or digest mismatch'
        Assert ($artifact.version -eq $ExpectedVersion -and $artifact.architecture -eq 'x64') 'Unexpected version or architecture'
    }
}
Scenario 'Portable payload includes host, WinUI, scanner and helper without test fixtures' {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $root 'LightHostModern-Portable.zip'))
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($name in 'Light Host Modern.exe', 'LightHostScanner.exe', 'LightHostUpdateHelper.exe', 'WinUI/x64/Release/LightHost.WinUI/LightHostWinUI.exe', 'release-info.json', 'LICENSE') {
            Assert ($names -contains $name) "Missing payload: $name"
        }
        Assert (@($names | Where-Object { $_ -match '(?i)Dragonfly|LightHost[^/]*Tests|(^|/)(Tests|fixtures|test-profiles|obj|AppX)/|\.(vst3|clap)$' }).Count -eq 0) 'Test material or duplicate output in portable payload'
        $manifest = $zip.Entries | Where-Object { $_.FullName -eq 'release-info.json' }
        $reader = [IO.StreamReader]::new($manifest.Open())
        try { $release = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        Assert ($release.version -eq $ExpectedVersion -and $release.platform -eq 'x64' -and $release.entryPoint -eq 'Light Host Modern.exe') 'Unexpected portable manifest'
    } finally { $zip.Dispose() }
}
Scenario 'MSI preserves machine scope, upgrade identity, shortcuts and legacy migration' {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase((Join-Path $root 'LightHostModern-Setup.msi'), 0)
    function Rows([string] $Query) {
        $view = $database.OpenView($Query); [void] $view.Execute()
        try { while ($record = $view.Fetch()) { $record.StringData(1) } } finally { [void] $view.Close() }
    }
    Assert ((Rows "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='ProductVersion'") -eq $ExpectedVersion) 'Unexpected MSI version'
    Assert ((Rows "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='ALLUSERS'") -eq '1') 'MSI scope changed'
    Assert ((Rows "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='UpgradeCode'") -eq '{8F28E61C-DC90-4927-B7B4-3E74E4B5960B}') 'MSI upgrade identity changed'
    $features = @(Rows 'SELECT `Feature` FROM `Feature`')
    Assert ($features -contains 'StartMenuShortcutFeature' -and $features -contains 'DesktopShortcutFeature') 'Shortcut features changed'
    $components = @(Rows 'SELECT `Component` FROM `Component`')
    Assert ($components -contains 'LegacyInstallCleanupComponent') 'Legacy migration component missing'
    $actions = @(Rows 'SELECT `Action` FROM `CustomAction`')
    Assert ($actions -contains 'SetARPINSTALLLOCATION') 'MSI installation location is not recorded'
    $summary = $database.SummaryInformation(0)
    Assert ($summary.Property(7).StartsWith('x64;')) 'MSI architecture mismatch'
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
Scenario 'Staging rejects a stale WinUI record before touching its destination' {
    $stageScript = Join-Path $repo 'Utilities\WinUI Output.ps1'
    $source = & $stageScript -Mode Resolve -Platform x64 -Configuration Release
    $stampPath = Join-Path $source 'lighthost-build.json'
    $original = [IO.File]::ReadAllBytes($stampPath)
    $destination = Join-Path $root 'stale-stage-test'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $sentinel = Join-Path $destination 'sentinel.txt'
    [IO.File]::WriteAllText($sentinel, 'keep')
    try {
        $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
        $stamp.sourceHash = 'not-current'
        $stamp | ConvertTo-Json | Set-Content -LiteralPath $stampPath -Encoding UTF8
        $rejected = $false
        try { & $stageScript -Mode Stage -Configuration Release -Platform x64 -Destination $destination }
        catch { $rejected = $_.Exception.Message -like '*does not match current sources*' }
        Assert ($rejected -and [IO.File]::ReadAllText($sentinel) -eq 'keep') 'Stale WinUI was staged or destination was changed before validation'
    } finally { [IO.File]::WriteAllBytes($stampPath, $original) }
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'package-inspection-results.json') -Encoding UTF8
$results | Format-Table -AutoSize
if (@($results | Where-Object { $_.status -eq 'failed' }).Count) { exit 1 }
