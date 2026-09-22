param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'Win32', 'ARM64')]
    [string] $Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

$manifest = Join-Path $PSScriptRoot "LightHostModern.WinUI\$Platform\$Configuration\LightHostModern.WinUI\AppxManifest.xml"
if (-not (Test-Path -LiteralPath $manifest)) {
    $manifest = Join-Path $PSScriptRoot "$Platform\$Configuration\LightHostModern.WinUI\AppxManifest.xml"
}
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "Generated AppxManifest.xml was not found. Build WinUI\LightHostModern.WinUI.sln first."
}

$packageRoot = Join-Path $PSScriptRoot 'LightHostModern.WinUI\AppPackages\LightHostModern.WinUI'
$testPackage = Get-ChildItem -Path $packageRoot -Directory -Filter "LightHostModern.WinUI_*_${Platform}_${Configuration}_Test" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$dependencyPaths = @()
if ($null -ne $testPackage) {
    $dependencyRoot = Join-Path $testPackage.FullName "Dependencies\$Platform"
    if (Test-Path -LiteralPath $dependencyRoot) {
        $dependencyPaths = Get-ChildItem -Path $dependencyRoot -Filter '*.appx' | Select-Object -ExpandProperty FullName
    }
}

foreach ($dependencyPath in $dependencyPaths) {
    try {
        Add-AppxPackage -Path $dependencyPath -ForceApplicationShutdown
    }
    catch {
        Write-Host "Dependency already installed or skipped: $dependencyPath"
    }
}

Add-AppxPackage -Register $manifest -ForceApplicationShutdown

$package = Get-AppxPackage -Name 'LightHostModern.WinUI' | Select-Object -First 1
if ($null -eq $package) {
    throw 'LightHostModern.WinUI package registration did not complete.'
}

$aumid = "$($package.PackageFamilyName)!App"
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'packaged-aumid.txt') -Value $aumid -Encoding ASCII
Write-Host "Registered LightHostModern.WinUI"
Write-Host "AUMID: $aumid"
