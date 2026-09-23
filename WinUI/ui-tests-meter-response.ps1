param([Parameter(Mandatory)][string]$ProfileInfo,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$info=Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.uiPid)"
if (!$process -or !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw 'An isolated fixture UI is required.' }
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,WindowsBase
$control=Join-Path (Split-Path -Parent $ProfileInfo) 'control.json'
$log=Join-Path (Split-Path -Parent $ProfileInfo) 'requests.jsonl'
& winapp ui invoke NavDashboard -a $info.uiPid --json | Out-Null
$window=(& winapp ui list-windows -a $info.uiPid --json | ConvertFrom-Json | Where-Object { $_.title.StartsWith('LightHostModern [Test:') } | Select-Object -First 1).hwnd
$root=[Windows.Automation.AutomationElement]::FromHandle([IntPtr][long]$window)
$condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'InputMeter')
$meter=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
if (!$meter) { throw 'Missing live meter.' }
function Signal([double]$Peak) {
    $json=@{inputPeak=$Peak;outputPeak=$Peak;telemetryDelayMs=900} | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($control+'.tmp',$json)
    if ([IO.File]::Exists($control)) { [IO.File]::Replace($control+'.tmp',$control,[NullString]::Value) }
    else { [IO.File]::Move($control+'.tmp',$control) }
}
$samples=@()
try {
    Signal .1
    Start-Sleep -Milliseconds 1200
    for($index=0;$index -lt 20;$index++) {
        # Downward readings intentionally decay at 30 dB/s. Measure transport
        # latency on rising edges after allowing that decay to settle.
        Signal .2
        Start-Sleep -Milliseconds 650
        $peak=.8
        $expected=(20*[Math]::Log10($peak)).ToString('F1',[Globalization.CultureInfo]::InvariantCulture)+' dBFS'
        $watch=[Diagnostics.Stopwatch]::StartNew()
        Signal $peak
        do {
            if ($meter.Current.HelpText -eq $expected) { break }
            Start-Sleep -Milliseconds 5
        } while ($watch.ElapsedMilliseconds -lt 400)
        if ($meter.Current.HelpText -ne $expected) { throw 'Meter frame was lost or delayed beyond 400 ms.' }
        $samples+=$watch.Elapsed.TotalMilliseconds
        Start-Sleep -Milliseconds 60
    }
    $sorted=@($samples | Sort-Object)
    $p95=$sorted[[Math]::Ceiling($sorted.Count*.95)-1]
    if ($p95 -gt 180) { throw "Meter p95 response is too slow: $p95 ms." }
    & winapp ui screenshot -w $window -o "$OutputDirectory/live-meters.png" --json | Out-Null
    & winapp ui invoke NavPlugins -a $info.uiPid --json | Out-Null
    Start-Sleep -Milliseconds 350
    $before=@(Get-Content $log | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object command -eq 'meter-levels').Count
    Start-Sleep -Milliseconds 350
    $after=@(Get-Content $log | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object command -eq 'meter-levels').Count
    if($before -ne $after) { throw 'Hidden meters keep requesting frames.' }
    [ordered]@{status='passed';samplesMs=$samples;p95Ms=$p95;maxMs=$sorted[-1];simulatedDiagnosticsDelayMs=900;hiddenMeterRequests=$after-$before} |
        ConvertTo-Json | Tee-Object -FilePath "$OutputDirectory/meter-response.json"
} finally { [IO.File]::WriteAllText($control,'{}') }
