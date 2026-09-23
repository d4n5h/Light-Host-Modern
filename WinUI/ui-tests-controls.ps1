param([Parameter(Mandatory)][string]$ProfileInfo,[Parameter(Mandatory)][string]$OutputDirectory,[string]$TestPattern='.*')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/ui-tests-plugin-workspace.ps1" -ProfileInfo $ProfileInfo -OutputDirectory $OutputDirectory -ScenarioPattern '^$' | Out-Null
$ScenarioPattern=$TestPattern
. "$PSScriptRoot/../Tests/HostProtocol.ps1"
function Snapshot { Send-HostRequest -PipeName $info.pipe -Command snapshot }
function Command([string]$Name,[array]$Arguments) {
    $snapshot=Snapshot
    Wait-HostOperation -PipeName $info.pipe -Accepted (Send-HostRequest -PipeName $info.pipe -Command $Name -Arguments $Arguments -Session $snapshot.hostSession)
}
Scenario 'Compact page keeps the scrollbar at the window edge and bounds the preferred picker' {
    Page Settings; Choose LayoutMode Compact
    Choose AudioPersistenceMode 'Custom device'
    UI @('scroll-into-view','PreferredDevicePicker') | Out-Null
    $picker=Bounds PreferredDevicePicker; $title=Bounds PageTitle; $scroll=Bounds ContentScrollViewer
    Require ($picker[2] -lt $title[2]*0.55) 'Compact picker is stretched across the card.'
    Require ($scroll[0]+$scroll[2] -gt $title[0]+$title[2]+40) 'Scrollbar is still inside the compact card column.'
    UI @('hover','PageTitle','--dwell-time','100') | Out-Null
    Capture 'compact-preferred-device'
    Choose LayoutMode Expanded; UI @('scroll-into-view','PreferredDevicePicker') | Out-Null
    Require ([Math]::Abs((Bounds PreferredDevicePicker)[2]-$picker[2]) -le 3) 'Picker width differs between layouts.'
    Capture 'expanded-preferred-device'
    Choose LayoutMode Compact
}
Scenario 'Diagnostics confirmation cancels safely and disabling stops UI telemetry' {
    Page Settings; UI @('scroll-into-view','DiagnosticsEnabled') | Out-Null
    Require ((Properties DiagnosticsEnabled).ToggleState -eq 'On') 'Diagnostics did not default to enabled.'
    Invoke DiagnosticsEnabled
    UI @('wait-for','DisableDiagnosticsDialog','-t','3000') | Out-Null
    Capture 'disable-diagnostics-confirmation'
    Invoke CloseButton
    Require ((Properties DiagnosticsEnabled).ToggleState -eq 'On') 'Cancel did not restore the toggle.'
    Require ((Snapshot).diagnosticsEnabled) 'Cancel changed the host preference.'
    Invoke DiagnosticsEnabled; Invoke PrimaryButton
    UI @('wait-for','NavDiagnostics','--gone','-t','5000') | Out-Null
    Require (!(Snapshot).diagnosticsEnabled) 'Disabling only hid the sidebar.'
    Page Dashboard
    Start-Sleep -Milliseconds 1000
    $before=@(Get-Content "$fixtureDirectory/requests.jsonl" | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object command -eq telemetry).Count
    Start-Sleep -Milliseconds 1200
    $after=@(Get-Content "$fixtureDirectory/requests.jsonl" | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object command -eq telemetry).Count
    Require ($before -eq $after) 'Disabled diagnostics still polled telemetry.'
    Capture 'dashboard-diagnostics-off'
    Page Settings; UI @('scroll-into-view','DiagnosticsEnabled') | Out-Null
    Invoke DiagnosticsEnabled; UI @('wait-for','NavDiagnostics','-t','5000') | Out-Null
    Page Diagnostics; Capture 'diagnostics-resumed'
}
Scenario 'Installed aliases restore independently and seed new Running instances' {
    Page Plugins; Invoke PluginsInstalledTab
    $known=(Snapshot).knownPluginList[0]; $id=$known.knownId
    Command rename-known-plugin @($id,'') | Out-Null
    Start-Sleep -Milliseconds 500
    UI @('scroll','InstalledPluginsList','--to','top') | Out-Null
    Invoke $id
    Require ((Properties PluginAction-restore).IsEnabled -eq 'False') 'Restore was enabled for the original name.'
    Invoke PluginAction-rename
    UI @('wait-for','InstanceName','-t','3000') | Out-Null
    UI @('set-value','InstanceName','Studio Reverb') | Out-Null; Invoke PrimaryButton
    Start-Sleep -Milliseconds 500
    UI @('wait-for',('installed-'+$id),'-t','2000') | Out-Null
    $renamed=(Snapshot).knownPluginList | Where-Object knownId -eq $id
    Require ($renamed.name -eq 'Studio Reverb' -and $renamed.originalName -eq $known.originalName) 'Installed alias altered the original metadata.'
    Invoke $id; Capture 'installed-alias-menu'
    Require ((Properties PluginAction-restore).IsEnabled -eq 'True') 'Restore did not enable after rename.'
    UI @('send-keys','escape','--via','send-input') | Out-Null
    Command add-known-plugin @($id) | Out-Null
    Start-Sleep -Milliseconds 500
    $added=(Snapshot).activePlugins | Select-Object -Last 1
    Require ($added.name -eq 'Studio Reverb' -and $added.customName -eq 'Studio Reverb') 'New instance did not inherit the catalogue alias.'
    Invoke $id; Invoke PluginAction-restore
    Start-Sleep -Milliseconds 350
    Require (((Snapshot).knownPluginList | Where-Object knownId -eq $id).name -eq $known.originalName) 'Installed restore failed.'
    Require (((Snapshot).activePlugins | Where-Object instanceId -eq $added.instanceId).name -eq 'Studio Reverb') 'Installed restore changed an existing instance.'
    Invoke PluginsRunningTab
    Invoke $added.instanceId; Capture 'running-grouped-menu'
    Require ((Properties PluginAction-restore).IsEnabled -eq 'True') 'Inherited alias cannot be restored.'
    Invoke PluginAction-restore; Start-Sleep -Milliseconds 350
    Invoke $added.instanceId
    Require ((Properties PluginAction-restore).IsEnabled -eq 'False') 'Running restore remained enabled after restoring.'
    Invoke PluginAction-details
    UI @('wait-for','PluginDetailsText','-t','3000') | Out-Null
    Require ((Bounds CloseButton)[2] -ge (Bounds PluginDetailsText)[2]*0.95) 'Details close button does not span its footer.'
    Capture 'plugin-details-full-close'; Invoke CloseButton
    Require (!(Visible RunningOrderHint)) 'The drag instruction is still visible.'
    Command remove-plugin @($added.instanceId) | Out-Null
}
Scenario 'Installed and Running search widths match and list scrollbar stays outside the cards' {
    Page Plugins; Invoke PluginsRunningTab
    $running=Bounds RunningPluginSearchInput
    Invoke PluginsInstalledTab
    $installed=Bounds InstalledPluginSearchInput; $list=Bounds InstalledPluginsList; $sort=Bounds InstalledPluginSort
    Require ([Math]::Abs($installed[2]-$running[2]) -le 2) 'Search widths differ.'
    Require ($list[0]+$list[2] -gt $sort[0]+$sort[2]+40) 'Plugin scrollbar is inside compact toolbar width.'
    $first=(Snapshot).knownPluginList[0].knownId
    $card=Bounds ('installed-'+$first); $title=Bounds PageTitle
    Require ([Math]::Abs($card[0]-$title[0]) -le 3 -and [Math]::Abs($card[2]-$title[2]) -le 3) "Plugin cards do not align with the compact header: card=$card title=$title."
    UI @('hover','PageTitle','--dwell-time','100') | Out-Null
    Capture 'installed-toolbar-scrollbar'
}
Scenario 'Enabled devices scroll includes the backend card' {
    Page Settings; UI @('scroll-into-view','ManageEnabledAudioDevices') | Out-Null
    Invoke ManageEnabledAudioDevices
    UI @('wait-for','EnabledAudioBackendToggle','-t','3000') | Out-Null
    Capture 'enabled-devices-top'
    $top=(Bounds EnabledAudioBackendToggle)[1]
    UI @('scroll','EnabledDevicesScroll','--to','bottom') | Out-Null
    Require (!(Visible EnabledAudioBackendToggle) -or (Bounds EnabledAudioBackendToggle)[1] -lt $top-20) 'The backend section remained sticky.'
    Capture 'enabled-devices-scrolled'; Invoke CloseButton
}
Scenario 'Failure checkboxes retain contrast when selected' {
    Fixture @{scanFailures=14;scanActive=$false}
    Page Plugins; Invoke PluginsInstalledTab; Invoke ScanForPlugins
    Invoke PrimaryButton
    UI @('wait-for','ScanProgressDialog','-t','4000') | Out-Null
    UI @('wait-for','PluginScanFailureCount','--value','14','-t','4000') | Out-Null
    Invoke SecondaryButton
    UI @('wait-for','ScanFailurePath0','-t','4000') | Out-Null
    UI @('click','ScanFailurePath0') | Out-Null
    Require ((Properties PrimaryButton).IsEnabled -eq 'True') 'Checkbox selection no longer enables retry.'
    UI @('hover','ScanFailureSummary','--dwell-time','100') | Out-Null
    Capture 'failure-checkbox-selected-dark'
    Invoke CloseButton; UI @('wait-for','ScanProgressDialog','-t','4000') | Out-Null; Invoke CloseButton
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$OutputDirectory/results.json" -Encoding UTF8
if (@($results | Where-Object status -eq failed).Count) { exit 1 }
