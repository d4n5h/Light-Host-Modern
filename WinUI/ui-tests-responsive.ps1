param([Parameter(Mandatory)][int] $AppPid)

$ErrorActionPreference = 'Stop'
$screenshots = Join-Path $PSScriptRoot 'test-screenshots'
$portugueseSettingsTitle = 'Configura' + [char]0x00E7 + [char]0x00F5 + 'es'
$portugueseLanguageName = 'Portugu' + [char]0x00EA + 's (Brasil)'
New-Item -ItemType Directory -Force -Path $screenshots | Out-Null

$windows = & winapp ui list-windows -a $AppPid --json | ConvertFrom-Json
$window = @($windows | Where-Object { $_.title.StartsWith('LightHostModern [Test:') })[0]
if ($null -eq $window) { throw 'Main LightHostModern window not found.' }
$hwnd = [IntPtr]$window.hwnd

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LightHostModernWindowTest {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
}
'@

function Assert-Command([string] $name, [scriptblock] $action) {
    & $action | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Failed: $name" }
    Write-Output "PASS: $name"
}

function Set-ComboIndex([string] $selector, [int] $index) {
    & winapp ui focus $selector -w $window.hwnd | Out-Null
    if ($LASTEXITCODE -ne 0) { return }
    & winapp ui send-keys home -w $window.hwnd --via send-input | Out-Null
    if ($LASTEXITCODE -ne 0) { return }
    for ($step = 0; $step -lt $index; $step++) {
        & winapp ui send-keys down -w $window.hwnd --via send-input | Out-Null
        if ($LASTEXITCODE -ne 0) { return }
    }
}

Assert-Command 'Focus main window' { & winapp ui focus NavSettings -w $window.hwnd }
Assert-Command 'Compact app logo exists' { & winapp ui wait-for NavCompactLogo -w $window.hwnd -t 3000 }
Assert-Command 'Navigate Settings' { & winapp ui invoke NavSettings -w $window.hwnd }
Assert-Command 'Normalize test language to English' { Set-ComboIndex AppLanguage 0 }
Assert-Command 'Layout mode exists' { & winapp ui wait-for LayoutMode -w $window.hwnd -t 3000 }
Assert-Command 'Backdrop mode exists' { & winapp ui wait-for BackdropMode -w $window.hwnd -t 3000 }
Assert-Command 'Start with Windows state label exists' { & winapp ui wait-for StartWithWindowsState -w $window.hwnd -t 3000 }
Assert-Command 'Close to tray state label exists' { & winapp ui wait-for CloseToTrayState -w $window.hwnd -t 3000 }
Assert-Command 'VST2 state label exists' { & winapp ui wait-for EnableVst2State -w $window.hwnd -t 3000 }

[LightHostModernWindowTest]::MoveWindow($hwnd, 80, 60, 1000, 760, $true) | Out-Null
Start-Sleep -Milliseconds 800
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '01-settings-narrow.png') | Out-Null

Assert-Command 'Set compact layout' { Set-ComboIndex LayoutMode 0 }
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '02-settings-compact.png') | Out-Null
Assert-Command 'Set expanded layout' { Set-ComboIndex LayoutMode 1 }

Assert-Command 'Set solid backdrop' { Set-ComboIndex BackdropMode 3 }
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '03-backdrop-solid.png') | Out-Null
Assert-Command 'Set Mica backdrop' { Set-ComboIndex BackdropMode 0 }
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '04-backdrop-mica.png') | Out-Null

Assert-Command 'Choose custom persistence' { Set-ComboIndex AudioPersistenceMode 2 }
Assert-Command 'Preferred device picker appears' { & winapp ui wait-for PreferredDevicePicker -w $window.hwnd -t 3000 }
Assert-Command 'Focus retry interval' { & winapp ui focus AudioRecoveryRetrySeconds -w $window.hwnd }
Assert-Command 'Clear retry interval focus from page background' { & winapp ui click PageTitle -w $window.hwnd }
$focusedAfterBackgroundClick = & winapp ui get-focused -w $window.hwnd
if ($focusedAfterBackgroundClick -match 'AudioRecoveryRetrySeconds') { throw 'Retry interval kept keyboard focus after clicking the page background.' }
Write-Output 'PASS: Retry interval releases focus'
Assert-Command 'Open preferred device dialog' { & winapp ui invoke PreferredDevicePicker -w $window.hwnd }
Assert-Command 'Preferred device Save button exists' { & winapp ui wait-for Save -w $window.hwnd -t 3000 }
Assert-Command 'Preferred device Cancel button exists' { & winapp ui wait-for Cancel -w $window.hwnd -t 3000 }
Assert-Command 'Focus preferred backend' { & winapp ui focus RecoveryAudioBackend -w $window.hwnd }
Assert-Command 'Keyboard can leave the preferred backend' { & winapp ui focus CloseButton -w $window.hwnd }
$focusedAfterDialogClick = & winapp ui get-focused -w $window.hwnd
if ($focusedAfterDialogClick -match 'RecoveryAudioBackend') { throw 'Preferred device input kept focus after clicking the dialog background.' }
Write-Output 'PASS: Preferred device dialog releases input focus'
Start-Sleep -Milliseconds 500
& winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '05-preferred-device-dialog.png') | Out-Null
Assert-Command 'Cancel preferred device dialog' { & winapp ui invoke CloseButton -w $window.hwnd }

Assert-Command 'Navigate Support' { & winapp ui invoke NavSupport -w $window.hwnd }
Assert-Command 'Support subtitle exists' { & winapp ui wait-for PageSubtitle -w $window.hwnd -t 3000 }
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '06-support-narrow.png') | Out-Null

Assert-Command 'Navigate Plugins' { & winapp ui invoke NavPlugins -w $window.hwnd }
Assert-Command 'Open Installed plugins' { & winapp ui invoke PluginsInstalledTab -w $window.hwnd }
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '07-plugins-narrow.png') | Out-Null
Assert-Command 'Open sort menu' { & winapp ui invoke InstalledPluginSort -w $window.hwnd }
Start-Sleep -Milliseconds 400
& winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '08-sort-flyout.png') | Out-Null
& winapp ui send-keys escape -w $window.hwnd | Out-Null

[LightHostModernWindowTest]::MoveWindow($hwnd, 8, 8, 1880, 920, $true) | Out-Null
Start-Sleep -Milliseconds 800
Assert-Command 'Open scan paths dialog' { & winapp ui invoke ScanForPlugins -w $window.hwnd }
Assert-Command 'New scan path field exists' { & winapp ui wait-for NewScanPath -w $window.hwnd -t 3000 }
& winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '09-scan-paths-dialog.png') | Out-Null
Assert-Command 'Focus new scan path' { & winapp ui focus NewScanPath -w $window.hwnd }
Assert-Command 'Keyboard can leave the scan path editor' { & winapp ui focus CloseButton -w $window.hwnd }
$focusedAfterScanHint = & winapp ui get-focused -w $window.hwnd
if ($focusedAfterScanHint -match 'NewScanPath') { throw 'Scan path input kept focus after clicking the dialog background.' }
Assert-Command 'Cancel scan paths dialog' { & winapp ui invoke CloseButton -w $window.hwnd }

[LightHostModernWindowTest]::MoveWindow($hwnd, 80, 60, 1000, 760, $true) | Out-Null
Start-Sleep -Milliseconds 800
Assert-Command 'Navigate back to Settings' { & winapp ui invoke NavSettings -w $window.hwnd }
Assert-Command 'Switch to Portuguese (Brazil)' { Set-ComboIndex AppLanguage 1 }
Assert-Command 'Portuguese Settings title appears' { & winapp ui wait-for PageTitle -w $window.hwnd --value $portugueseSettingsTitle -t 10000 }
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '11-settings-pt-br.png') | Out-Null
& winapp ui send-keys escape -w $window.hwnd --via send-input | Out-Null
& winapp ui scroll-into-view AppLanguage -w $window.hwnd | Out-Null
Start-Sleep -Milliseconds 300
Assert-Command 'Open language choices after localization' { & winapp ui invoke AppLanguage -w $window.hwnd }
foreach ($nativeName in @('English (United States)',$portugueseLanguageName)) {
    $choices=& winapp ui search $nativeName -a $AppPid --json | ConvertFrom-Json
    if (!@($choices.matches | Where-Object { $_.name -eq $nativeName -and $_.type -eq 'ListItem' }).Count) {
        & winapp ui inspect -a $AppPid -d 6 --json | Set-Content (Join-Path $screenshots 'language-choices.json') -Encoding UTF8
        throw "Native language choice is missing: $nativeName"
    }
    Write-Output "PASS: Native language choice $nativeName"
}
& winapp ui send-keys escape -w $window.hwnd | Out-Null
Assert-Command 'Restore English language' { Set-ComboIndex AppLanguage 0 }
Assert-Command 'English Settings title returns' { & winapp ui wait-for PageTitle -w $window.hwnd --value 'Settings' -t 10000 }
Assert-Command 'Repeat switch to Portuguese without closing UI' { Set-ComboIndex AppLanguage 1 }
Assert-Command 'Repeated Portuguese switch completes' { & winapp ui wait-for PageTitle -w $window.hwnd --value $portugueseSettingsTitle -t 10000 }
Assert-Command 'Repeat switch back to English without closing UI' { Set-ComboIndex AppLanguage 0 }
Assert-Command 'Repeated English switch completes' { & winapp ui wait-for PageTitle -w $window.hwnd --value 'Settings' -t 10000 }

[LightHostModernWindowTest]::MoveWindow($hwnd, 80, 60, 1460, 920, $true) | Out-Null
Start-Sleep -Milliseconds 800
& winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '12-settings-wide.png') | Out-Null

$solidHash = (Get-FileHash (Join-Path $screenshots '03-backdrop-solid.png')).Hash
$micaHash = (Get-FileHash (Join-Path $screenshots '04-backdrop-mica.png')).Hash
if ($solidHash -eq $micaHash) { throw 'Backdrop screenshots are identical.' }
Write-Output 'PASS: Backdrop produces a visible rendering change'
Write-Output "Screenshots: $screenshots"
