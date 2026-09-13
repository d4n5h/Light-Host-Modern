param([Parameter(Mandatory)][int]$AppPid, [Parameter(Mandatory)][string]$PipeName,
      [Parameter(Mandatory)][ValidateSet(96,144,192)][int]$ExpectedDpi,
      [string]$OutputDirectory='out/ui-accessibility')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
$OutputDirectory=Join-Path $OutputDirectory "dpi-$ExpectedDpi"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results=[Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    $output=rtk proxy winapp ui @Arguments -a $AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "UI command $($Arguments -join ' ') failed: $output" }
    $output | ConvertFrom-Json
}
function Choose([string]$Combo,[string]$Text) {
    UI @('scroll-into-view',$Combo) | Out-Null
    UI @('invoke',$Combo) | Out-Null
    $match=(UI @('search',$Text)).matches | Where-Object { $_.type -eq 'ListItem' -and $_.name -eq $Text -and !$_.isOffscreen } | Select-Object -First 1
    if (!$match) { throw "ComboBox option was not found: $Text" }
    UI @('invoke',$match.selector) | Out-Null
    Start-Sleep -Milliseconds 300
}
function Scenario([string]$Name,[scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name;status='passed'}); Write-Host "PASS: $Name" }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error="$($_.Exception.Message)"}); Write-Host "FAIL: $Name - $($_.Exception.Message)" }
}
function Assert-Visible([string]$Id,[string]$Name='') {
    UI @('wait-for',$Id,'-p','IsOffscreen','--value','False','-t','5000') | Out-Null
    $properties=(UI @('get-property',$Id)).properties
    if (!$properties.Name) { throw "The control has no accessible name: $Id" }
    if ($Name -and $properties.Name -ne $Name) { throw "Untranslated accessible name for $Id`: $($properties.Name)" }
    $bounds=@($properties.BoundingRectangle.Split(',') | ForEach-Object { [double]$_ })
    if ($bounds.Count -ne 4 -or $bounds[0] -lt $script:windowBounds.x -or $bounds[1] -lt $script:windowBounds.y -or
        $bounds[0]+$bounds[2] -gt $script:windowBounds.x+$script:windowBounds.width -or
        $bounds[1]+$bounds[3] -gt $script:windowBounds.y+$script:windowBounds.height) { throw "The control is clipped by the actual window: $Id ($($properties.BoundingRectangle))." }
}
$tree=UI @('inspect','-d','1')
$window=@($tree.windows | Where-Object { $_.title.StartsWith('Light Host Modern [Test:') })
if ($window.Count -ne 1) { throw 'Exactly one isolated application window is required.' }
$handle=$window[0].hwnd
$script:windowBounds=$window[0].elements[0]
$metrics=rtk proxy python -X utf8 "$PSScriptRoot\..\Tests\InspectWindowMetrics.py" --hwnd $handle --pid $AppPid | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $metrics.dpi -ne $ExpectedDpi) { throw "Window DPI $($metrics.dpi) differs from requested $ExpectedDpi." }
$metrics | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'window-metrics.json') -Encoding UTF8
if (!$metrics.withinWorkArea) { throw 'The initial application window extends outside its monitor work area.' }
$initial=Send-HostRequest $PipeName 'snapshot'
if ($initial.audioSelection.driverAvailable) { throw 'Layout validation requires a suspended temporary audio profile.' }
foreach ($language in @('en-us','pt-br')) {
    $catalog=Get-Content -LiteralPath "$PSScriptRoot\LightHost.WinUI\Locales\$language.json" -Encoding UTF8 -Raw | ConvertFrom-Json
    Scenario "$language language can change in a retained Settings page" {
        UI @('invoke','NavSettings') | Out-Null
        Choose 'AppLanguage' $catalog.'Language.DisplayName'
        UI @('invoke','NavDashboard') | Out-Null
        Assert-Visible 'DashboardGlobalMute' $catalog.'audio.globalMute'
        Assert-Visible 'DashboardGlobalBypass' $catalog.'audio.globalBypass'
    }
    foreach ($mode in @('compact','expanded')) {
        Scenario "$language $mode pages remain reachable at $ExpectedDpi DPI" {
            UI @('invoke','NavSettings') | Out-Null
            Choose 'LayoutMode' $catalog.('settings.layout.'+$mode)
            foreach ($page in @('Dashboard','Audio','Plugins','Settings')) {
                UI @('invoke',('Nav'+$page)) | Out-Null
                if ($page -ne 'Plugins') {
                    $scroll=(UI @('get-property','ContentScrollViewer')).properties
                    if ($scroll.VerticallyScrollable -notin @('0x0','False',$null)) { UI @('scroll','ContentScrollViewer','--to','top') | Out-Null }
                }
                switch ($page) {
                    Dashboard { Assert-Visible 'DashboardGlobalMute'; Assert-Visible 'DashboardGlobalBypass' }
                    Audio { Assert-Visible 'AudioBackend' }
                    Plugins { Assert-Visible 'PluginsRunningTab'; Assert-Visible 'PluginsInstalledTab' }
                    Settings {
                        Assert-Visible 'StartWithWindows' $catalog.'settings.startWindows'
                        Assert-Visible 'CloseToTray' $catalog.'settings.closeToTray'
                        foreach ($control in @('EnableVst2','AudioPersistenceMode','AppTheme','AppLanguage','LayoutMode','BackdropMode','IconMode')) {
                            UI @('scroll-into-view',$control) | Out-Null
                            Assert-Visible $control
                        }
                        UI @('scroll','ContentScrollViewer','--to','top') | Out-Null
                    }
                }
                UI @('screenshot','-o',"$OutputDirectory/$language-$mode-$page.png") | Out-Null
            }
        }
    }
    Scenario "$language keyboard traverses global controls and Space changes the host" {
        UI @('invoke','NavDashboard') | Out-Null
        UI @('focus','DashboardGlobalMute') | Out-Null
        UI @('send-keys','tab','--via','send-input') | Out-Null
        $focus=UI @('get-focused')
        if (($focus | ConvertTo-Json -Depth 6) -notmatch 'DashboardGlobalBypass') { throw 'Tab did not focus the bypass control.' }
        $before=Send-HostRequest $PipeName 'snapshot'
        UI @('send-keys','space','--via','send-input') | Out-Null
        $expected=if ($before.globalBypassed) { 'Off' } else { 'On' }
        UI @('wait-for','DashboardGlobalBypass','--value',$expected,'-t','5000') | Out-Null
        $after=Send-HostRequest $PipeName 'snapshot'
        if ($after.globalBypassed -eq $before.globalBypassed) { throw 'Keyboard activation did not change the actual host.' }
        $focus=UI @('get-focused')
        if (($focus | ConvertTo-Json -Depth 6) -notmatch 'DashboardGlobalBypass') { throw ('Completing the command lost keyboard focus: '+($focus | ConvertTo-Json -Depth 6 -Compress)) }
        UI @('send-keys','space','--via','send-input') | Out-Null
        UI @('wait-for','DashboardGlobalBypass','--value',$(if ($before.globalBypassed) { 'On' } else { 'Off' }),'-t','5000') | Out-Null
    }
}
$english=Get-Content -LiteralPath "$PSScriptRoot\LightHost.WinUI\Locales\en-us.json" -Encoding UTF8 -Raw | ConvertFrom-Json
UI @('invoke','NavSettings') | Out-Null
Choose 'AppLanguage' $english.'Language.DisplayName'
Choose 'LayoutMode' $english.'settings.layout.expanded'
UI @('invoke','NavDashboard') | Out-Null
$results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
$results | Format-Table name,status,error -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
