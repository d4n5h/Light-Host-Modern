param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release",

    [string] $Preset = "windows-vs2022",

    [ValidateSet("AUTO", "ON", "OFF")]
    [string] $EnableVst2 = "AUTO",

[ValidateSet("LEGACY", "XAYMAR")]
[string] $Vst2Provider = "XAYMAR",

    [string] $Vst2SdkDir = ""
)

$ErrorActionPreference = "Stop"

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($null -ne $cmake) { $cmake.Source } else { Join-Path $env:ProgramFiles "CMake\bin\cmake.exe" }

if (!(Test-Path $cmakePath)) {
    throw "CMake 3.22+ was not found. Install current CMake and Visual Studio Build Tools 2022."
}

$configureArgs = @(
    "--preset", $Preset,
    "-DLIGHTHOST_ENABLE_VST2=$EnableVst2",
    "-DLIGHTHOST_VST2_PROVIDER=$Vst2Provider"
)

if (![string]::IsNullOrWhiteSpace($Vst2SdkDir)) {
    $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$Vst2SdkDir"
} elseif (![string]::IsNullOrWhiteSpace($env:LIGHTHOST_VST2_SDK_DIR)) {
    $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$env:LIGHTHOST_VST2_SDK_DIR"
}

& $cmakePath @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Compile the UI before the host stages it. Both entry points resolve to the
# same output through LightHostModern.Output.props and validate its source stamp.
$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $vswherePath)) { throw "Visual Studio Installer (vswhere) was not found." }
$uiMSBuild = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (!$uiMSBuild) { throw "MSBuild with WinUI tooling was not found." }
$uiSolution = Join-Path $PSScriptRoot "..\WinUI\LightHostModern.WinUI.sln"
& $uiMSBuild $uiSolution /m "/p:Configuration=$Configuration" /p:Platform=x64 /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmakePath --build --preset "$Preset-$($Configuration.ToLowerInvariant())"
exit $LASTEXITCODE
