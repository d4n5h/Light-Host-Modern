# Focused visual/interaction validation against Tests/UiRedesignFakeHost.py.
# The fixture supplies known meter levels without opening any audio device.
param([Parameter(Mandatory)][string]$ProfileInfo,
      [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$info=Get-Content -LiteralPath $ProfileInfo -Raw | ConvertFrom-Json
$appProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$($info.uiPid)"
if (!$appProcess -or !$appProcess.CommandLine.Contains('--test-profile='+$info.name)) { throw 'An isolated test UI is required.' }
$AppPid=[int]$info.uiPid
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results=[Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    if ($Arguments[0] -eq 'set-value' -and $Arguments[2] -eq '') {
        UI @('focus',$Arguments[1]) | Out-Null
        UI @('send-keys','ctrl+a','--via','send-input') | Out-Null
        UI @('send-keys','backspace','--via','send-input') | Out-Null
        return
    }
    $output=rtk proxy winapp ui @Arguments -a $AppPid --json
    $commandExit=$LASTEXITCODE
    $parsed=$output | ConvertFrom-Json
    if ($parsed.error -or ($commandExit -ne 0 -and !($Arguments[0] -eq 'search' -and $null -ne $parsed.matchCount -and $parsed.matchCount -eq 0))) {
        throw ("winapp ui " + ($Arguments -join ' ') + " failed: " + ($output -join "`n"))
    }
    return $parsed
}
$window=(UI @('list-windows') | Where-Object { $_.title.StartsWith('LightHostModern [Test:') } | Select-Object -First 1).hwnd
if (!$window) { throw 'Test window missing.' }
function Page([string]$Name) {
    UI @('click',('Nav'+$Name)) | Out-Null
    Start-Sleep -Milliseconds 150
}
function Choose([string]$Combo,[string]$Text) {
    UI @('scroll-into-view',$Combo) | Out-Null
    UI @('invoke',$Combo) | Out-Null
    $item=(UI @('search',$Text)).matches | Where-Object { $_.type -eq 'ListItem' -and $_.name -eq $Text -and !$_.isOffscreen } | Select-Object -First 1
    if (!$item) { throw "Missing option $Text in $Combo" }
    UI @('invoke',$item.selector) | Out-Null
    Start-Sleep -Milliseconds 200
}
function Capture([string]$Name) {
    UI @('click','PageTitle') | Out-Null
    UI @('screenshot','-w',"$window",'-o',"$OutputDirectory/$Name.png") | Out-Null
}
function Bounds([string]$Id) {
    $p=(UI @('get-property',$Id)).properties
    @($p.BoundingRectangle.Split(',') | ForEach-Object { [double]$_ })
}
function WithinContent([string]$Id) {
    $viewport=Bounds 'ContentScrollViewer'; $bounds=Bounds $Id
    if ($bounds[2] -le 0 -or $bounds[0] -lt $viewport[0]-1 -or $bounds[0]+$bounds[2] -gt $viewport[0]+$viewport[2]+1) { throw "$Id exceeds the page width." }
}
function Scenario([string]$Name,[scriptblock]$Body) {
    try { & $Body; $results.Add([pscustomobject]@{name=$Name;status='passed'}); Write-Output "PASS $Name" }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error=$_.Exception.Message}); Write-Output "FAIL $Name : $($_.Exception.Message)" }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$OutputDirectory/results.json" -Encoding UTF8
}
Scenario 'Light and dark themes; all four window materials' {
    Page Settings
    Choose 'AppTheme' 'Light'
    Choose 'LayoutMode' 'Compact'
    Page Dashboard; Capture 'light-dashboard'
    Page Settings; Choose 'AppTheme' 'Dark'
    foreach ($material in @('Mica','Mica Alt','Acrylic','Solid')) {
        Choose 'BackdropMode' $material
        Page Dashboard; Capture ('material-'+$material.Replace(' ','-'))
        Page Settings
    }
    Choose 'BackdropMode' 'Mica Alt'
    UI @('scroll-into-view','BackdropMode') | Out-Null
    Capture 'appearance'
}
Scenario 'Compact title alignment and separate channel cards' {
    Page Audio
    $header=Bounds 'PageTitle'; $viewport=Bounds 'ContentScrollViewer'
    if ([Math]::Abs($header[0]-$viewport[0]) -gt 1 -or $viewport[2] -gt 1561) { throw 'Compact page alignment or width is incorrect at 200% DPI.' }
    WithinContent 'AudioBackend'
    Capture 'audio-compact'
}
Scenario 'Diagnostics is a separate page with values inside the cards' {
    Page Diagnostics
    Start-Sleep -Milliseconds 1200
    foreach ($key in @('dspLoadPercent','hostCpuPercent','uiCpuPercent','sampleRate','bufferSize','chainLatencySamples')) { WithinContent ('Diagnostic-'+$key) }
    if ((UI @('search','DiagnosticsExpander')).matchCount -ne 0) { throw 'The old Diagnostics expander is still present.' }
    Capture 'diagnostics'
}
Scenario 'About uses the same repository button dimensions as Support' {
    Page Settings
    UI @('scroll','ContentScrollViewer','--to','bottom') | Out-Null
    WithinContent 'ModernRepository'; WithinContent 'OriginalRepository'
    $modern=Bounds 'ModernRepository'
    Capture 'about'
    Page Support
    UI @('scroll-into-view','SupportRepositoryButton') | Out-Null
    $reference=Bounds 'SupportRepositoryButton'
    if ($modern[2] -ne $reference[2] -or $modern[3] -ne $reference[3]) { throw 'Repository buttons have different dimensions.' }
    Capture 'support'
}
Scenario 'Enabled devices dialog is narrow and the backend stays accessible' {
    Page Settings
    UI @('scroll-into-view','ManageEnabledAudioDevices') | Out-Null
    UI @('invoke','ManageEnabledAudioDevices') | Out-Null
    $backend=Bounds 'EnabledAudioBackend'
    if ($backend[2] -gt 1184) { throw 'Enabled devices dialog is too wide.' }
    UI @('screenshot','-w',"$window",'-o',"$OutputDirectory/enabled-devices.png") | Out-Null
    UI @('invoke','CloseButton') | Out-Null
}
Scenario 'Unicode scan paths persist between openings and can be removed' {
    Page Plugins
    UI @('invoke','PluginsInstalledTab') | Out-Null
    UI @('invoke','PluginDatabaseActions') | Out-Null
    UI @('invoke','DatabasePathsTab') | Out-Null
    $saved=(UI @('search','C:\Test Plugins\Áudio')).matches | Where-Object { $_.type -eq 'Text' } | Select-Object -First 1
    if (!$saved) {
        UI @('set-value','NewScanPath','C:\Test Plugins\Áudio') | Out-Null
        UI @('invoke','SaveNewScanPath') | Out-Null
        UI @('invoke','CloseButton') | Out-Null
        UI @('invoke','PluginDatabaseActions') | Out-Null
    UI @('invoke','DatabasePathsTab') | Out-Null
        $saved=(UI @('search','C:\Test Plugins\Áudio')).matches | Where-Object { $_.type -eq 'Text' } | Select-Object -First 1
    }
    if (!$saved) { throw 'Saved Unicode path did not survive restart.' }
    $index=$saved.automationId.Replace('SavedScanPath','')
    UI @('scroll-into-view',('RemoveScanPath'+$index)) | Out-Null
    UI @('invoke',('RemoveScanPath'+$index)) | Out-Null
    if ((UI @('search','C:\Test Plugins\Áudio')).matchCount) { throw 'Removed path is still listed.' }
    UI @('set-value','NewScanPath','C:\Program Files\Common Files\VST3') | Out-Null
    UI @('invoke','SaveNewScanPath') | Out-Null
    if ((UI @('get-value','ScanPathMessage')).text -notmatch 'already') { throw 'Duplicate path was not rejected.' }
    UI @('set-value','NewScanPath','') | Out-Null
    UI @('scroll','ScanPathsScroll','--to','top') | Out-Null
    UI @('screenshot','-w',"$window",'-o',"$OutputDirectory/scan-paths.png") | Out-Null
    UI @('invoke','CloseButton') | Out-Null
}
Scenario 'Plugin sections, search, grouping and empty states' {
    Page Plugins
    UI @('invoke','PluginsInstalledTab') | Out-Null
    Capture 'installed'
    UI @('invoke','InstalledPluginSort') | Out-Null
    UI @('invoke','InstalledGroupByManufacturer') | Out-Null
    Capture 'installed-grouped'
    UI @('invoke','InstalledPluginSort') | Out-Null
    UI @('invoke','InstalledGroupByManufacturer') | Out-Null
    UI @('invoke','PluginsRunningTab') | Out-Null
    Capture 'running'
    UI @('set-value','RunningPluginSearchInput','no-such-plugin-redesign') | Out-Null
    UI @('wait-for','No plugins running','--property','IsOffscreen','--value','False','-t','3000') | Out-Null
    Capture 'running-empty-search'
    UI @('set-value','RunningPluginSearchInput','') | Out-Null
}
Scenario 'Expanded audio and PT-BR pages remain usable' {
    Page Settings; Choose 'LayoutMode' 'Expanded'
    Page Audio; Capture 'audio-expanded'
    Page Settings
    Choose 'AppLanguage' 'Português (Brasil)'
    Choose 'LayoutMode' 'Compacto'
    Page Diagnostics; Capture 'pt-br-diagnostics'
    Page Plugins
    $runningLabel=(UI @('get-property','PluginsRunningTab','--property','Name')).properties.Name
    $installedLabel=(UI @('get-property','PluginsInstalledTab','--property','Name')).properties.Name
    if ($runningLabel -notmatch '^Em execução \(' -or $installedLabel -notmatch '^Instalados \(') { throw 'Plugin section labels did not update with the language.' }
    Capture 'pt-br-running'
    Page Settings
    Choose 'AppLanguage' 'English (United States)'
    Choose 'LayoutMode' 'Compact'
}
$results | Format-Table name,status -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
