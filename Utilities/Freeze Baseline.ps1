param([string] $Name = 'completion-20260908', [string] $Preset = 'windows-vs2022')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$target = Join-Path $repo "out\baselines\$Name"
if (Test-Path -LiteralPath $target) { throw "Baseline already exists: $target" }
$hostBuild = Join-Path $repo "out\build\$Preset"
$payload = Join-Path $hostBuild 'LightHost_artefacts\Release'
$ui = Join-Path $repo 'WinUI\x64\Release\LightHost.WinUI'
foreach ($file in @((Join-Path $payload 'Light Host Modern.exe'), (Join-Path $payload 'LightHostScanner.exe'), (Join-Path $ui 'LightHostWinUI.exe'))) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing Release output: $file" }
}
New-Item -ItemType Directory -Path $target | Out-Null
Copy-Item -LiteralPath $payload -Destination (Join-Path $target 'host') -Recurse
$baselineUi = Join-Path $target 'host\WinUI\x64\Release\LightHost.WinUI'
New-Item -ItemType Directory -Force -Path $baselineUi | Out-Null
Copy-Item -Path (Join-Path $ui '*') -Destination $baselineUi -Recurse -Force
Copy-Item -LiteralPath (Join-Path $hostBuild 'Testing\Temporary\LastTest.log') -Destination $target
$diffPath = Join-Path $target 'working-tree.patch'
& rtk proxy git diff --binary "--output=$diffPath"
if ($LASTEXITCODE -ne 0) { throw 'Could not freeze the tracked source diff.' }
$sources = Join-Path $target 'source'
New-Item -ItemType Directory -Path $sources | Out-Null
foreach ($folder in @('Source', 'Tests', 'docs')) { Copy-Item -LiteralPath (Join-Path $repo $folder) -Destination $sources -Recurse }
$manifest = Get-ChildItem -LiteralPath (Join-Path $target 'host') -Recurse -File | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($target.Length + 1); size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
}
$capturedVersion = (Get-Item -LiteralPath (Join-Path $payload 'Light Host Modern.exe')).VersionInfo.ProductVersion
[ordered]@{ version = $capturedVersion; configuration = 'Release'; capturedUtc = [DateTime]::UtcNow.ToString('o'); files = @($manifest); performanceMeasured = $false } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $target 'manifest.json') -Encoding UTF8
Write-Output $target
