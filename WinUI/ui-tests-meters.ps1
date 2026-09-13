param([Parameter(Mandatory)][string]$ProfileInfo,
      [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$info=Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.uiPid)"
if (!$process -or !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw 'An isolated fixture UI is required.' }
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,WindowsBase
$AppPid=[int]$info.uiPid
rtk proxy winapp ui click NavDashboard -a $AppPid --json | Out-Null
$window=(rtk proxy winapp ui list-windows -a $AppPid --json | ConvertFrom-Json | Where-Object { $_.title.StartsWith('Light Host Modern [Test:') } | Select-Object -First 1).hwnd
$root=[Windows.Automation.AutomationElement]::FromHandle([IntPtr][long]$window)
function Element([string]$Id) {
    $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)
    $root.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
}
function Level([string]$Id) {
    $bar=Element $Id
    if (!$bar) { throw "Missing meter $Id" }
    [double]$bar.Current.HelpText.TrimEnd([char]37)
}
Start-Sleep -Milliseconds 500
$initialInput=Level InputMeter; $initialOutput=Level OutputMeter
if ([Math]::Abs($initialInput-25) -gt 0.1 -or [Math]::Abs($initialOutput-50) -gt 0.1) { throw 'Known fixture signals did not reach the volume bars.' }
$muted=$false
try {
    rtk proxy winapp ui click NavPlugins -a $AppPid --json | Out-Null
    rtk proxy winapp ui invoke PluginsRunningTab -a $AppPid --json | Out-Null
    Start-Sleep -Milliseconds 500
    rtk proxy winapp ui invoke RunningGlobalMute -a $AppPid --json | Out-Null
    Start-Sleep -Milliseconds 250
    rtk proxy winapp ui click NavDashboard -a $AppPid --json | Out-Null
    $muted=$true
    Start-Sleep -Milliseconds 500
    $mutedInput=Level InputMeter; $mutedOutput=Level OutputMeter
    if ($mutedOutput -gt 0.1 -or [Math]::Abs($mutedInput-$initialInput) -gt 0.1) { throw "Mute must clear output while preserving input: input=$mutedInput output=$mutedOutput" }
} finally {
    if ($muted) {
        rtk proxy winapp ui click NavPlugins -a $AppPid --json | Out-Null
        rtk proxy winapp ui invoke RunningGlobalMute -a $AppPid --json | Out-Null
        Start-Sleep -Milliseconds 250
        rtk proxy winapp ui click NavDashboard -a $AppPid --json | Out-Null
    }
}
Start-Sleep -Milliseconds 500
$restoredOutput=Level OutputMeter
if ([Math]::Abs($restoredOutput-$initialOutput) -gt 0.1) { throw 'Output meter did not recover after unmuting.' }
$result=[pscustomobject]@{status='passed';input=$initialInput;output=$initialOutput;mutedInput=$mutedInput;mutedOutput=$mutedOutput;restoredOutput=$restoredOutput}
$result | ConvertTo-Json | Set-Content -LiteralPath "$OutputDirectory/meters.json" -Encoding UTF8
$result | ConvertTo-Json
