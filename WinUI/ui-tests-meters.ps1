param([Parameter(Mandatory)][string]$ProfileInfo,
      [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$info=Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.uiPid)"
if (!$process -or !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw 'An isolated fixture UI is required.' }
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,WindowsBase
$AppPid=[int]$info.uiPid
& winapp ui invoke NavDashboard -a $AppPid --json | Out-Null
$window=(& winapp ui list-windows -a $AppPid --json | ConvertFrom-Json | Where-Object { $_.title.StartsWith('LightHostModern [Test:') } | Select-Object -First 1).hwnd
$root=[Windows.Automation.AutomationElement]::FromHandle([IntPtr][long]$window)
function Element([string]$Id) {
    $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)
    $root.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
}
function Level([string]$Id) {
    $bar=Element $Id
    if (!$bar) { throw "Missing meter $Id" }
    if ($bar.Current.HelpText -eq ([string][char]0x2212+[char]0x221e+' dBFS')) { return [double]::NegativeInfinity }
    [double]::Parse($bar.Current.HelpText.Replace(' dBFS',''),[Globalization.CultureInfo]::InvariantCulture)
}
Start-Sleep -Milliseconds 500
$initialInput=Level InputMeter; $initialOutput=Level OutputMeter
if ([Math]::Abs($initialInput+12.0) -gt 0.1 -or [Math]::Abs($initialOutput+6.0) -gt 0.1) { throw 'Known fixture signals did not reach the volume bars.' }
$muted=$false
try {
    & winapp ui invoke NavPlugins -a $AppPid --json | Out-Null
    & winapp ui invoke PluginsRunningTab -a $AppPid --json | Out-Null
    Start-Sleep -Milliseconds 500
    & winapp ui invoke RunningGlobalMute -a $AppPid --json | Out-Null
    Start-Sleep -Milliseconds 250
    & winapp ui invoke NavDashboard -a $AppPid --json | Out-Null
    $muted=$true
    Start-Sleep -Milliseconds 2500
    $mutedInput=Level InputMeter; $mutedOutput=Level OutputMeter
    if ($mutedOutput -ne [double]::NegativeInfinity -or [Math]::Abs($mutedInput-$initialInput) -gt 0.1) { throw "Mute must clear output while preserving input: input=$mutedInput output=$mutedOutput" }
} finally {
    if ($muted) {
        & winapp ui invoke NavPlugins -a $AppPid --json | Out-Null
        & winapp ui invoke RunningGlobalMute -a $AppPid --json | Out-Null
        Start-Sleep -Milliseconds 250
        & winapp ui invoke NavDashboard -a $AppPid --json | Out-Null
    }
}
Start-Sleep -Milliseconds 500
$restoredOutput=Level OutputMeter
if ([Math]::Abs($restoredOutput-$initialOutput) -gt 0.1) { throw 'Output meter did not recover after unmuting.' }
$result=[pscustomobject]@{status='passed';input=$initialInput;output=$initialOutput;mutedInput=$mutedInput;mutedOutput=$mutedOutput;restoredOutput=$restoredOutput}
$result | ConvertTo-Json | Set-Content -LiteralPath "$OutputDirectory/meters.json" -Encoding UTF8
$result | ConvertTo-Json
