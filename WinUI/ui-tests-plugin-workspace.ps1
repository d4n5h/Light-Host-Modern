# Focused UI regression. Requires the isolated Tests/UiRedesignFakeHost.py profile.
param([Parameter(Mandatory)][string]$ProfileInfo,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [string]$ScenarioPattern='.*')
$ErrorActionPreference='Stop'
$info=Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$appProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.uiPid)"
if (!$appProcess -or !$appProcess.CommandLine.Contains('--test-profile='+$info.name)) { throw 'An isolated test UI is required.' }
$AppPid=[int]$info.uiPid
if (-not ('LightHostTestForeground' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LightHostTestForeground {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results=[Collections.Generic.List[object]]::new()
$fixtureDirectory=Split-Path -Parent $ProfileInfo
function UI([string[]]$Arguments) {
    if ($Arguments[0] -eq 'set-value' -and $Arguments[2] -eq '') {
        UI @('focus',$Arguments[1]) | Out-Null
        UI @('send-keys','ctrl+a','--via','send-input') | Out-Null
        UI @('send-keys','backspace','--via','send-input') | Out-Null
        return
    }
    if ($window -and $Arguments[0] -in @('click','hover','send-keys','drag')) {
        [LightHostTestForeground]::SetForegroundWindow([IntPtr][long]$window) | Out-Null
        Start-Sleep -Milliseconds 100
    }
    $output=& winapp ui @Arguments -a $AppPid --json
    if ($Arguments[0] -in @('scroll','scroll-into-view')) { Start-Sleep -Milliseconds 300 }
    $commandExit=$LASTEXITCODE
    $parsed=$output | ConvertFrom-Json
    if (@($parsed | Where-Object { $_.error }).Count -gt 0 -or ($commandExit -ne 0 -and !($Arguments[0] -eq 'search' -and $null -ne $parsed.matchCount -and $parsed.matchCount -eq 0))) {
        throw ("winapp ui " + ($Arguments -join ' ') + " failed: " + ($output -join "`n"))
    }
    return $parsed
}
$window=(UI @('list-windows') | Where-Object { $_.title.StartsWith('LightHostModern [Test:') } | Select-Object -First 1).hwnd
if (!$window) { throw 'Test window missing.' }
function Page([string]$Name) {
    UI @('focus',('Nav'+$Name)) | Out-Null
    UI @('send-keys','escape','--via','send-input') | Out-Null
    Start-Sleep -Milliseconds 200
    UI @('invoke',('Nav'+$Name)) | Out-Null
    Start-Sleep -Milliseconds 200
    if ((Properties ('Nav'+$Name)).IsSelected -ne 'True') { UI @('invoke',('Nav'+$Name)) | Out-Null }
}
function Invoke([string]$Id) {
    UI @('invoke',$Id) | Out-Null
    # Wait for flyout/dialog close animations before the next simulated action.
    Start-Sleep -Milliseconds 200
}
function Capture([string]$Name) { UI @('screenshot','-w',"$window",'-o',"$OutputDirectory/$Name.png") | Out-Null }
function Properties([string]$Id) { (UI @('get-property',$Id)).properties }
function Bounds([string]$Id) { @((Properties $Id).BoundingRectangle.Split(',') | ForEach-Object { [double]$_ }) }
function Visible([string]$Id) { @((UI @('search',$Id)).matches | Where-Object { $_.automationId -eq $Id -and !$_.isOffscreen }).Count -gt 0 }
function Require([bool]$Value,[string]$Message) { if (!$Value) { throw $Message } }
function Fixture([hashtable]$Values) {
    $Values | ConvertTo-Json | Set-Content -LiteralPath "$fixtureDirectory/control.json" -Encoding UTF8
    Start-Sleep -Milliseconds 650
}
function Choose([string]$Combo,[string]$Text) {
    UI @('scroll-into-view',$Combo) | Out-Null
    Invoke $Combo
    $item=(UI @('search',$Text)).matches | Where-Object { $_.type -eq 'ListItem' -and $_.name -eq $Text -and !$_.isOffscreen } | Select-Object -First 1
    Require ($null -ne $item) "Missing option $Text"
    Invoke $item.selector
}
function Scenario([string]$Name,[scriptblock]$Body) {
    if ($Name -notmatch $ScenarioPattern) { return }
    try { & $Body; $results.Add([pscustomobject]@{name=$Name;status='passed'}); Write-Output "PASS $Name" }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error=$_.Exception.Message}); Write-Output "FAIL $Name : $($_.Exception.Message)" }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$OutputDirectory/results.json" -Encoding UTF8
}
Scenario 'Support tab can be hidden before its lazy page is created' {
    Page Settings
    UI @('scroll-into-view','HideSupportTab') | Out-Null
    if ((Properties HideSupportTab).ToggleState -eq 'On') { Invoke HideSupportTab }
    Invoke HideSupportTab
    Require (!(Visible NavSupport)) 'Support navigation is still visible.'
    Require ((Properties HideSupportTab).ToggleState -eq 'On') 'The UI did not survive hiding the unopened page.'
    Invoke HideSupportTab
    Page Support
    Page Settings
    UI @('scroll-into-view','HideSupportTab') | Out-Null
    Invoke HideSupportTab
    Require (!(Visible NavSupport)) 'Support navigation is still visible after opening and hiding it.'
    $settings=Get-Content -LiteralPath (Join-Path $info.profile 'ui-settings.ini') -Raw
    Require ($settings -match 'HideSupportTab=1') 'The hidden preference was not saved.'
    Capture 'support-hidden'
}
Scenario 'Settings maintenance works before Plugins loads and Settings follows Support' {
    Page Settings
    Require (!(Visible NavDatabase)) 'Database should no longer be a sidebar page.'
    UI @('scroll-into-view','HideSupportTab') | Out-Null
    if ((Properties HideSupportTab).ToggleState -eq 'On') { Invoke HideSupportTab }
    Require (Visible NavSupport) 'Support navigation is missing.'
    Require ((Bounds NavSettings)[1] -gt (Bounds NavSupport)[1]) 'Settings is not below Support.'
    UI @('scroll','ContentScrollViewer','--to','top') | Out-Null
    UI @('hover','PageTitle','--dwell-time','150') | Out-Null
    Capture 'settings-plugin-database'
    Require ((Properties RemoveMissingPlugins).IsEnabled -eq 'True') 'Maintenance depends on opening Plugins first.'
    Require ((Properties ClearPluginDatabase).IsEnabled -eq 'True') 'Clear database is unavailable in Settings.'
    Invoke RemoveMissingPlugins
    UI @('scroll-into-view','ClearPluginDatabase') | Out-Null
    Invoke ClearPluginDatabase
    Require (@((UI @('search','Clear plugin database?')).matches).Count -gt 0) 'Clear confirmation did not open.'
    Capture 'settings-clear-confirmation'
    Invoke CloseButton
    Require (Visible ClearPluginDatabase) 'Settings did not remain visible after cancelling.'
    Require ((Properties ClearPluginDatabase).HasKeyboardFocus -eq 'True') 'Focus did not return to the Settings action.'
}
Scenario 'Scan flow separates path settings from compact progress and results' {
    Fixture @{}
    Page Plugins; Invoke PluginsInstalledTab; Invoke ScanForPlugins
    UI @('wait-for','NewScanPath','-t','4000') | Out-Null
    # ContentDialog's UIA rectangle covers the whole modal overlay. Compare
    # the content controls, which report the actual panel widths.
    $pathWidth=(Bounds NewScanPath)[2]
    $newPath='C:\Test Plugins\'+[char]0xC1+'udio'
    UI @('set-value','NewScanPath',$newPath) | Out-Null; Invoke SaveNewScanPath
    $settings=Get-Content -LiteralPath (Join-Path $info.profile 'ui-settings.ini') -Raw
    Require ($settings.Contains($newPath.Replace('\','\\'))) 'Unicode path was not persisted.'
    UI @('set-value','NewScanPath',$newPath) | Out-Null; Invoke SaveNewScanPath
    Require (Visible ScanPathMessage) 'Duplicate path was not reported.'
    UI @('set-value','NewScanPath','') | Out-Null
    Capture 'scan-path-cards'
    Invoke PrimaryButton
    UI @('wait-for','ScanProgressDialog','-t','5000') | Out-Null
    UI @('wait-for','PluginScanProgress','-t','5000') | Out-Null
    Require (!(Visible NewScanPath)) 'Path controls are still in the progress modal.'
    Require ((Bounds PluginScanStatus)[2] -lt $pathWidth) 'Progress modal is not smaller than path settings.'
    Capture 'scan-progress-small'
    Invoke CloseButton; Invoke ScanForPlugins
    UI @('wait-for','PluginScanProgress','-t','4000') | Out-Null
    Require (!(Visible NewScanPath)) 'An active scan did not reopen its progress view.'
    Invoke PrimaryButton
    UI @('wait-for','PluginScanProgress','--gone','-t','5000') | Out-Null
    Require ((Properties PluginScanStatus).Name -eq 'The scan was stopped. Plugins found so far were kept.') 'Cancelling the scan incorrectly reports success.'
    Capture 'scan-cancelled'
    Fixture @{scanActive=$false;scanFailures=14;scanCancelled=$false}
    UI @('wait-for','PluginScanFailureCount','--value','14','-t','4000') | Out-Null
    Capture 'scan-results-small'
    Invoke SecondaryButton
    UI @('wait-for','ScanFailureList','-t','4000') | Out-Null
    Require (!(Visible PreviousFailurePage) -and !(Visible NextFailurePage)) 'Failure list still has pagination buttons.'
    Require (!(Visible ScanFailureDetails)) 'An empty detail textbox is still displayed.'
    Require ((Properties ScanFailureReason0).Name -eq 'Path not found') 'The path and its readable error are not separate.'
    Require ((Bounds ScanFailureReason0)[1] -ge (Bounds ScanFailurePath0)[1]+(Bounds ScanFailurePath0)[3]) 'Failure reason overlaps its path.'
    Capture 'scan-failures-separated'
    UI @('click','ScanFailurePath0') | Out-Null
    Require ((Properties PrimaryButton).IsEnabled -eq 'True') 'Selecting a failure did not enable retry.'
    Invoke CloseButton
    UI @('wait-for','ScanProgressDialog','-t','4000') | Out-Null
    Invoke CloseButton; Invoke ScanForPlugins
    UI @('wait-for','NewScanPath','-t','4000') | Out-Null
    Require (!(Visible PluginScanStatus)) 'Old results still clutter the path settings.'
    Invoke SecondaryButton
    UI @('wait-for','ScanProgressDialog','-t','4000') | Out-Null
    Require ((Properties PluginScanFailureCount).Name -eq '14') 'Previous scan results were lost.'
    Invoke CloseButton
}
Scenario 'Failure list loads remaining rows during scrolling and retries stable IDs' {
    Fixture @{scanActive=$false;scanFailures=205;scanCancelled=$false}
    Page Plugins; Invoke PluginsInstalledTab; Invoke ScanForPlugins; Invoke SecondaryButton; Invoke SecondaryButton
    UI @('wait-for','ScanFailurePath0','-t','4000') | Out-Null
    UI @('click','ScanFailurePath0') | Out-Null
    for($i=0;$i -lt 5;$i++) {
        UI @('scroll','ScanFailureList','--to','bottom') | Out-Null
        Start-Sleep -Milliseconds 250
        if (Visible ScanFailurePath204) { break }
    }
    Require (Visible ScanFailurePath204) 'Rows after the 100-record page were not loaded automatically.'
    UI @('click','ScanFailurePath204') | Out-Null
    Capture 'scan-failures-final-rows'
    Invoke PrimaryButton
    UI @('wait-for','ScanProgressDialog','-t','4000') | Out-Null
    Fixture @{scanActive=$true}
    UI @('wait-for','PluginScanProgress','-t','4000') | Out-Null
    Invoke PrimaryButton; Fixture @{scanActive=$false}
    UI @('wait-for','PluginScanProgress','--gone','-t','4000') | Out-Null
    Invoke CloseButton
    $requests=Get-Content "$fixtureDirectory/requests.jsonl" | ForEach-Object { $_ | ConvertFrom-Json }
    $selection=$requests | Where-Object command -eq 'retry-plugin-scan-selection' | Select-Object -Last 1
    Require ($selection.args[0].ids -contains 'failure-0' -and $selection.args[0].ids -contains 'failure-204') 'Retry lost IDs or selection across incremental loading.'
    $pages=@($requests | Where-Object command -eq 'plugin-scan-failures' | ForEach-Object { $_.args[0].offset })
    Require ($pages -contains 100 -and $pages -contains 200) 'Remaining failure pages were not requested.'
    Fixture @{}
}
Scenario 'Dashboard has the logarithmic dBFS meters and no global controls' {
    Page Dashboard; Fixture @{}
    Require (!(Visible 'DashboardGlobalMute') -and !(Visible 'DashboardGlobalBypass')) 'Dashboard still has global controls.'
    Require ((Properties InputMeter).HelpText -eq '-12.0 dBFS' -and (Properties OutputMeter).HelpText -eq '-6.0 dBFS') 'Fixture peaks did not reach the meters.'
    Capture 'dashboard-original-meters'
    Fixture @{inputPeak=0;outputPeak=0.8}
    Start-Sleep -Milliseconds 2000
    Require ((Properties InputMeter).HelpText -eq ([string][char]0x2212+[char]0x221e+' dBFS') -and (Properties OutputMeter).HelpText -eq '-1.9 dBFS') 'Meter dBFS scale is incorrect.'
    Capture 'meters-silence-warning'
    Fixture @{inputPeak=0.9;outputPeak=1.1}
    Require ((Properties OutputMeter).HelpText -eq '0.8 dBFS') 'Numeric meter should expose clipping above full scale.'
    Capture 'meters-peak'
    Fixture @{}
}
Scenario 'Running toolbar owns mute, bypass and sorting' {
    Page Plugins; Invoke PluginsRunningTab
    $toolbar=Bounds RunningPluginToolbar
    $previousRight=$toolbar[0]
    foreach ($id in @('RunningPluginSearchInput','RunningGlobalMute','RunningGlobalBypass','RunningPluginSort')) {
        $b=Bounds $id
        Require ($b[1] -ge $toolbar[1] -and $b[1]+$b[3] -le $toolbar[1]+$toolbar[3]+1) "$id is outside its toolbar."
        Require ($b[0] -ge $previousRight) "$id is not in left-to-right toolbar order."
        $previousRight=$b[0]+$b[2]
    }
    Require ((Bounds RunningPluginSearchInput)[2] -ge 0.36*$toolbar[2]) 'Running search did not grow with the toolbar.'
    Invoke RunningGlobalMute; Start-Sleep -Milliseconds 300
    Page Dashboard; Start-Sleep -Milliseconds 2500
    Require ((Properties OutputMeter).HelpText -eq ([string][char]0x2212+[char]0x221e+' dBFS')) 'Toolbar mute did not silence the output meter.'
    Page Plugins; Invoke RunningGlobalMute; Invoke RunningGlobalBypass
    Start-Sleep -Milliseconds 250
    Capture 'running-toolbar-bypass'
    Invoke RunningGlobalBypass
    Invoke RunningPluginSort
    Capture 'running-sort'
    UI @('send-keys','escape','--via','send-input') | Out-Null
    Fixture @{bypassFirst=$true}; Capture 'running-bypassed-badge'
    Require (@((UI @('search','Bypassed')).matches | Where-Object { !$_.isOffscreen }).Count -gt 0) 'Bypassed state did not update.'
    Fixture @{errorFirst=$true}; Capture 'running-error-badge'
    Require (@((UI @('search','Error')).matches | Where-Object { !$_.isOffscreen }).Count -gt 0) 'Error state did not update.'
    Fixture @{}; Capture 'running-toolbar'
}
Scenario 'Installed toolbar and distinct manufacturer cards preserve search' {
    Page Plugins
    Invoke PluginsInstalledTab
    Require (!(Visible 'ScanPluginsButton') -and !(Visible 'ScanPathsButton')) 'Redundant scan actions are still on the page.'
    Require (!(Visible 'InstalledGroupByManufacturer')) 'Grouping should be inside the sort menu.'
    $toolbar=Bounds InstalledPluginToolbar
    $previousRight=$toolbar[0]
    foreach ($id in @('InstalledPluginSearchInput','ScanForPlugins','InstalledPluginSort')) {
        Require (Visible $id) "$id is missing from the toolbar."
        $b=Bounds $id
        Require ($b[0] -ge $previousRight -and $b[1] -ge $toolbar[1] -and $b[1]+$b[3] -le $toolbar[1]+$toolbar[3]+1) "$id has incorrect toolbar bounds."
        $previousRight=$b[0]+$b[2]
    }
    $installedWidth=(Bounds InstalledPluginSearchInput)[2]
    Invoke PluginsRunningTab
    $runningWidth=(Bounds RunningPluginSearchInput)[2]
    Require ([Math]::Abs($installedWidth-$runningWidth) -le 2) 'The two search fields have different widths.'
    Invoke PluginsInstalledTab
    Capture 'installed-flat'
    Invoke InstalledPluginSort
    $group=Bounds InstalledGroupByManufacturer
    $sortOptions=(UI @('search','Plugin name (A-Z)')).matches | Where-Object { !$_.isOffscreen }
    Require ($sortOptions.Count -gt 0 -and (Bounds $sortOptions[0].selector)[1] -gt $group[1]+$group[3]) 'Grouping is not the first sort option.'
    Capture 'installed-sort-grouping'
    Invoke InstalledGroupByManufacturer
    Require (Visible 'manufacturer-cockos') 'Manufacturer has no separate card.'
    $header=Bounds 'manufacturer-cockos'
    $items=(UI @('search','installed-')).matches | Where-Object { $_.automationId -and $_.automationId.StartsWith('installed-') -and !$_.isOffscreen }
    Require ($items.Count -gt 0) 'Grouped plugin cards are missing.'
    $manufacturerTitle=(UI @('search','Cockos')).matches | Where-Object { $_.type -eq 'Text' -and $_.name -eq 'Cockos' -and !$_.isOffscreen } | Select-Object -First 1
    Require ($null -ne $manufacturerTitle) 'Visible manufacturer heading is missing.'
    UI @('focus','InstalledPluginSort') | Out-Null
    UI @('hover',$manufacturerTitle.selector,'--dwell-time','150') | Out-Null
    Capture 'installed-manufacturer-hover'
    UI @('focus','InstalledPluginSort') | Out-Null
    UI @('hover',$items[0].automationId,'--dwell-time','150') | Out-Null
    Capture 'installed-plugin-hover'
    UI @('set-value','InstalledPluginSearchInput','OldSkool') | Out-Null
    Start-Sleep -Milliseconds 400
    Require (Visible 'manufacturer-voxengo') 'Search did not retain the matching manufacturer header.'
    Capture 'installed-grouped-filtered'
    $actions=(UI @('search','Actions for')).matches | Where-Object { $_.type -eq 'Button' -and !$_.isOffscreen } | Select-Object -First 1
    $status=(UI @('search','Running')).matches | Where-Object { $_.type -eq 'Text' -and $_.name -eq 'Running' -and !$_.isOffscreen } | Select-Object -First 1
    Require ($null -ne $actions -and $null -ne $status) 'Status or action menu is missing.'
    $actionBounds=Bounds $actions.selector; $statusBounds=Bounds $status.selector
    Require ($statusBounds[0]+$statusBounds[2] -lt $actionBounds[0] -and [Math]::Abs(($statusBounds[1]+$statusBounds[3]/2)-($actionBounds[1]+$actionBounds[3]/2)) -lt 12) 'Status is not immediately left of the action menu.'
    UI @('set-value','InstalledPluginSearchInput','') | Out-Null
}
Scenario 'Diagnostics readings are below each card heading' {
    Page Diagnostics; Start-Sleep -Milliseconds 1100
    foreach ($pair in @(@('performance','dspLoadPercent'),@('reliability','xRunCount'),@('format','sampleRate'),@('latency','chainLatencySamples'))) {
        UI @('scroll-into-view',('DiagnosticsTitle-'+$pair[0])) | Out-Null
        # Bringing the heading into view can place it at the bottom edge.
        # Scroll until its first reading is also inside the viewport.
        for($attempt=0;$attempt -lt 8;$attempt++) {
            $value=Bounds ('Diagnostic-'+$pair[1])
            if($value[2] -gt 0 -and $value[3] -gt 0){break}
            UI @('scroll','ContentScrollViewer','--direction','down') | Out-Null
        }
        $title=Bounds ('DiagnosticsTitle-'+$pair[0]); $value=Bounds ('Diagnostic-'+$pair[1])
        Require ($value[1] -gt $title[1]+$title[3]) ($pair[0]+" readings are beside the title: title=$title value=$value.")
    }
    UI @('scroll','ContentScrollViewer','--to','top') | Out-Null
    Capture 'diagnostics-stacked'
}
Scenario 'Portuguese and light theme keep Settings and scan modal accessible' {
    Fixture @{}
    Page Settings; Choose AppLanguage 'English (United States)'; Choose AppTheme Light; Choose AppLanguage ('Portugu'+[char]0xEA+'s (Brasil)')
    Page Plugins; Invoke PluginsInstalledTab
    Require (!(Visible NavDatabase)) 'Database navigation should be absent in Portuguese too.'
    Require ((Properties PluginsInstalledTab).Name -match '^Instalados') 'Installed tab was not localized.'
    Capture 'installed-portuguese-light'
    Invoke ScanForPlugins
    UI @('wait-for','NewScanPath','-t','4000') | Out-Null
    Require ((Properties NewScanPath).Name -eq 'Adicionar novo caminho') 'Persistent scan-path editor was not localized.'
    Require ((Properties PrimaryButton).Name -eq 'Iniciar busca') 'Start scan was not localized.'
    Capture 'scan-dialog-portuguese-light'
    Invoke PrimaryButton
    UI @('wait-for','PluginScanProgress','-t','4000') | Out-Null
    Capture 'scan-progress-portuguese-light'
    Fixture @{scanActive=$false;scanFailures=14;scanCancelled=$false}
    UI @('wait-for','PluginScanFailureCount','--value','14','-t','4000') | Out-Null
    Capture 'scan-results-portuguese-light'
    Invoke SecondaryButton
    UI @('wait-for','ScanFailurePath0','-t','4000') | Out-Null
    Require ((Properties ScanFailureReason0).Name -eq ('Caminho n'+[char]0xE3+'o encontrado')) 'Failure reason was not localized.'
    Capture 'scan-failures-portuguese-light'
    Invoke CloseButton
    UI @('wait-for','ScanProgressDialog','-t','4000') | Out-Null
    Invoke CloseButton
    Page Settings; UI @('scroll','ContentScrollViewer','--to','top') | Out-Null
    Require ((Properties RemoveMissingPlugins).Name -eq 'Remover ausentes') 'Maintenance was not localized.'
    Capture 'settings-database-portuguese-light'
    Page Settings; Choose AppLanguage 'English (United States)'; Choose AppTheme Dark
    Page Plugins; Capture 'final-plugins'
}
Scenario 'Preferred device stays bounded with long driver names in Compact and Expanded' {
    Page Settings; Choose AppLanguage 'English (United States)'; Choose LayoutMode Compact
    Choose AudioPersistenceMode 'Custom device'
    UI @('scroll-into-view','PreferredDevicePicker') | Out-Null
    $heading=(UI @('search','Preferred device')).matches | Where-Object { $_.type -eq 'Text' -and $_.name -eq 'Preferred device' } | Select-Object -First 1
    Require ($null -ne $heading) 'Preferred device heading is missing.'
    UI @('scroll-into-view',$heading.selector) | Out-Null
    $title=Bounds $heading.selector; $picker=Bounds PreferredDevicePicker
    Require ($picker[0] -ge $title[0]+$title[2] -and [Math]::Abs($picker[1]-$title[1]) -lt 80) 'Preferred device picker is not alongside its heading in Compact.'
    UI @('hover','PageTitle','--dwell-time','150') | Out-Null
    Capture 'preferred-device-compact-long-name'
    Choose LayoutMode Expanded
    UI @('scroll-into-view','PreferredDevicePicker') | Out-Null
    UI @('scroll-into-view',$heading.selector) | Out-Null
    $title=Bounds $heading.selector; $picker=Bounds PreferredDevicePicker
    Require ([Math]::Abs($picker[1]-$title[1]) -lt 200) 'Preferred device card grows in Expanded.'
    UI @('hover','PageTitle','--dwell-time','150') | Out-Null
    Capture 'preferred-device-expanded-long-name'
    Choose LayoutMode Compact
}
$failed=@($results | Where-Object status -eq 'failed')
Write-Output ("Passed: "+($results.Count-$failed.Count)+"; failed: "+$failed.Count)
if ($failed.Count) { exit 1 }
