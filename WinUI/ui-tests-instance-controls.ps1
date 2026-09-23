param([Parameter(Mandatory)][int]$AppPid, [Parameter(Mandatory)][string]$PipeName,
      [string]$OutputDirectory = 'out/ui-instance-controls')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results = [Collections.Generic.List[object]]::new()
function UI([string[]]$Arguments) {
    $output = & winapp ui @Arguments -a $AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "$output" }
    return $output | ConvertFrom-Json
}
function Snapshot { Send-HostRequest $PipeName 'snapshot' }
function Test-Scenario([string]$Name, [scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name;status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error="$($_.Exception.Message)"}) }
}
function Open-Action([string]$Action) {
    UI @('invoke', $script:instanceId) | Out-Null
    UI @('invoke', ('PluginAction-' + $Action)) | Out-Null
}
UI @('invoke','NavPlugins') | Out-Null
UI @('invoke','PluginsRunningTab') | Out-Null
UI @('send-keys','ctrl+a','--target','RunningPluginSearchInput','--via','send-input') | Out-Null
UI @('send-keys','backspace','--via','send-input') | Out-Null
$initial = Snapshot
if ($initial.activePlugins.Count -lt 2) { throw 'This test requires two isolated fixture instances.' }
$script:instanceId = $initial.activePlugins[0].instanceId
$unicodeName = 'Voz: ' + [char]0x65e5 + [char]0x672c + [char]0x8a9e + ' ' + [char]::ConvertFromUtf32(0x1f3b5)
Test-Scenario 'Unicode rename applies only to the selected UUID' {
    Open-Action 'rename'
    UI @('wait-for','InstanceName','-t','3000') | Out-Null
    UI @('set-value','InstanceName',('  '+$unicodeName+'  ')) | Out-Null
    UI @('invoke','PrimaryButton') | Out-Null
    UI @('wait-for',('running-'+$script:instanceId),'--value',('1 '+$unicodeName+', Error'),'-t','4000') | Out-Null
    $state = Snapshot
    $row = @($state.activePlugins | Where-Object instanceId -eq $script:instanceId)[0]
    if ($row.name -ne $unicodeName -or $row.originalName -ne $initial.activePlugins[0].originalName) { throw 'Rename lost original name or Unicode.' }
}
Test-Scenario 'Swap selector uses UUID and actual position' {
    Open-Action 'swap'
    $tree = UI @('inspect','SwapInstanceList','-d','2')
    $target = $tree.windows[0].elements[0].children[0].selector
    UI @('invoke',$target) | Out-Null
    UI @('invoke','PrimaryButton') | Out-Null
    UI @('wait-for',('running-'+$script:instanceId),'--value',('2 '+$unicodeName+', Error'),'-t','4000') | Out-Null
    if ((Snapshot).activePlugins[1].instanceId -ne $script:instanceId) { throw 'Swap changed the wrong position.' }
}
Test-Scenario 'Details preserve original identity and are selectable' {
    Open-Action 'details'
    $details = UI @('get-value','PluginDetailsText')
    $serialized = $details | ConvertTo-Json -Depth 5
    if ($serialized -notmatch $script:instanceId -or $serialized -notmatch 'Effect') { throw 'Details omit instance or original name.' }
    UI @('screenshot','-o',"$OutputDirectory/details.png") | Out-Null
    UI @('send-keys','escape','--via','send-input') | Out-Null
}
Test-Scenario 'Restore original name and return focus' {
    Open-Action 'restore'
    UI @('wait-for',('running-'+$script:instanceId),'--value',('2 '+$initial.activePlugins[0].originalName+', Error'),'-t','4000') | Out-Null
    if (@((Snapshot).activePlugins | Where-Object instanceId -eq $script:instanceId)[0].customName -ne '') { throw 'Custom name was not reset.' }
    $focus = UI @('get-focused')
    if (($focus | ConvertTo-Json -Depth 6) -notmatch $script:instanceId) { throw 'Focus did not return to the row action.' }
}
Test-Scenario 'Installed grouping remains selected across navigation' {
    UI @('invoke','PluginsInstalledTab') | Out-Null
    UI @('invoke','InstalledPluginSort') | Out-Null
        UI @('invoke','InstalledGroupByManufacturer') | Out-Null
    UI @('invoke','InstalledPluginSort') | Out-Null
        UI @('wait-for','InstalledGroupByManufacturer','-p','ToggleState','--value','On','-t','2000') | Out-Null
    UI @('send-keys','escape','--via','send-input') | Out-Null
    UI @('invoke','NavDashboard') | Out-Null
    UI @('invoke','NavPlugins') | Out-Null
    UI @('invoke','PluginsInstalledTab') | Out-Null
    UI @('invoke','InstalledPluginSort') | Out-Null
        UI @('wait-for','InstalledGroupByManufacturer','-p','ToggleState','--value','On','-t','2000') | Out-Null
    UI @('send-keys','escape','--via','send-input') | Out-Null
    UI @('screenshot','-o',"$OutputDirectory/grouping.png") | Out-Null
    UI @('invoke','InstalledPluginSort') | Out-Null
        UI @('invoke','InstalledGroupByManufacturer') | Out-Null
}
Test-Scenario 'Dashboard exposes the two live level bars' {
    UI @('invoke','NavDashboard') | Out-Null
    foreach ($id in @('InputMeter','OutputMeter')) {
        $properties=(UI @('get-property',$id)).properties
        if ($properties.IsOffscreen -ne 'False' -or !$properties.HelpText) { throw "The level bar or its accessible reading is missing: $id" }
    }
    UI @('screenshot','-o',"$OutputDirectory/meters.png") | Out-Null
}
$results | ConvertTo-Json -Depth 6 | Set-Content "$OutputDirectory/results.json" -Encoding UTF8
$results | Format-Table -AutoSize
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
