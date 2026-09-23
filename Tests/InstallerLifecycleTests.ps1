# Execute only inside the disposable Windows machine named by the caller.
# Without -Execute, this script inspects packages and writes the reviewable plan.
param([Parameter(Mandatory)][string]$CurrentMsi,
      [string]$PreviousMsi='',
      [string]$ExpectedVersion='1.4.1',
      [string]$OutputDirectory='out/msi-lifecycle',
      [switch]$Execute,
      [string]$DisposableComputerName='')
$ErrorActionPreference='Stop'
$expectedUpgrade='{8F28E61C-DC90-4927-B7B4-3E74E4B5960B}'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputRoot=(Resolve-Path -LiteralPath $OutputDirectory).Path
$installer=New-Object -ComObject WindowsInstaller.Installer
$results=[Collections.Generic.List[object]]::new()
$installedByTest=[Collections.Generic.List[string]]::new()
function Package([string]$Path) {
    $file=Get-Item -LiteralPath $Path
    if ($file.Extension -ne '.msi') { throw 'An MSI package is required.' }
    $database=$installer.OpenDatabase($file.FullName,0)
    try {
        $values=@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
        foreach ($key in @('ProductCode','ProductVersion','UpgradeCode','ALLUSERS')) {
            $view=$database.OpenView(('SELECT `Value` FROM `Property` WHERE `Property`='''+$key+''''))
            try { [void]$view.Execute(); $record=$view.Fetch(); $values[$key]=$record.StringData(1) }
            finally { [void]$view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
        }
        $summary=$database.SummaryInformation(0)
        try { $values.architecture=$summary.Property(7) } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary) }
        if ($values.UpgradeCode -ne $expectedUpgrade -or $values.ALLUSERS -ne '1' -or !$values.architecture.StartsWith('x64;')) { throw 'Package identity, scope or architecture does not match LightHostModern.' }
        [pscustomobject]$values
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
}
function Scenario([string]$Name,[scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name;status='passed'}); Write-Host "PASS: $Name" }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error=$_.Exception.Message}); throw }
}
function Msi([string]$Name,[string[]]$Arguments) {
    $log=Join-Path $outputRoot ($Name+'.log')
    $process=Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList ($Arguments+@('/qn','/norestart','/L*v',('"'+$log+'"'))) -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(180000)) { $script:installerStillRunning=$true; throw "Installer $Name (PID $($process.Id)) is still running; inspect $log before continuing. It was not forcibly stopped." }
    $script:installerOutcomes.Add([pscustomobject]@{stage=$Name;exitCode=$process.ExitCode;rebootRequired=($process.ExitCode -in @(1641,3010));log=$log})
    if ($process.ExitCode -notin @(0,1641,3010)) { throw "Installer $Name failed with $($process.ExitCode); see $log." }
    if ($process.ExitCode -eq 1641) { throw 'The installer initiated a restart despite /norestart; the remaining scenarios require a new run.' }
}
function InstalledRoot([string]$Code) {
    if ($installer.ProductState($Code) -ne 5) { throw "The product is not installed: $Code" }
    $path=[IO.Path]::GetFullPath($installer.ProductInfo($Code,'InstallLocation')).TrimEnd('\')
    $productFolder=if ([version]$installer.ProductInfo($Code,'VersionString') -lt [version]'1.4.0') { 'Light Host Modern' } else { 'LightHostModern' }
    $allowed=[IO.Path]::GetFullPath((Join-Path $env:ProgramFiles $productFolder)).TrimEnd('\')
    if (!$path.Equals($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw "Unexpected installation directory: $path" }
    $path
}
function Verify-Payload([string]$Code,[switch]$Current) {
    $directory=InstalledRoot $Code
    $modernNames=[version]$installer.ProductInfo($Code,'VersionString') -ge [version]'1.4.0'
    $payload=if ($modernNames) { @('LightHostModern.exe','WinUI\x64\Release\LightHostModern.WinUI\LightHostModernWinUI.exe') } else { @('Light Host Modern.exe','WinUI\x64\Release\LightHost.WinUI\LightHostWinUI.exe') }
    if ($modernNames) { $payload+=@('LightHostModernScanner.exe','LightHostModernUpdateHelper.exe') }
    foreach ($relative in $payload) {
        if (!(Test-Path -LiteralPath (Join-Path $directory $relative) -PathType Leaf)) { throw "Installed payload is missing $relative" }
    }
    if (!$Current) { return $directory }
    $shell=New-Object -ComObject WScript.Shell
    try {
        foreach ($relative in @(
            (Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'LightHostModern\LightHostModern.lnk'),
            (Join-Path ([Environment]::GetFolderPath('CommonDesktopDirectory')) 'LightHostModern.lnk'))) {
            if (!(Test-Path -LiteralPath $relative)) { throw "Installed shortcut is missing: $relative" }
            $shortcut=$shell.CreateShortcut($relative)
            try { if ($shortcut.TargetPath -ne (Join-Path $directory 'LightHostModern.exe')) { throw 'A shortcut targets an obsolete installation.' } }
            finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
        }
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
    $directory
}
$script:installerOutcomes=[Collections.Generic.List[object]]::new()
$script:installerStillRunning=$false
try {
    $current=Package $CurrentMsi
    $previous=if ($PreviousMsi) { Package $PreviousMsi } else { $null }
    if ($current.ProductVersion -ne $ExpectedVersion) { throw "Expected package version $ExpectedVersion." }
    if ($previous -and ([version]$previous.ProductVersion -ge [version]$current.ProductVersion -or $previous.ProductCode -eq $current.ProductCode)) { throw 'Upgrade testing requires an earlier genuine product version with a different ProductCode.' }
    $plan=[pscustomobject]@{current=$current;previous=$previous;executed=[bool]$Execute;computer=$env:COMPUTERNAME;expectedDisposableComputer=$DisposableComputerName;
        stages=@('install previous version','seed preference preservation fixture','upgrade to current MSI','verify registered version and shortcuts','repair a removed scanner payload','uninstall','verify preserved preferences')}
    $plan | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $outputRoot 'plan.json') -Encoding UTF8
    if (!$Execute) { $plan | ConvertTo-Json -Depth 8; return }
    if (!$previous -or !$DisposableComputerName -or $env:COMPUTERNAME -ne $DisposableComputerName) { throw 'Execution requires an earlier MSI and the exact disposable Windows computer name.' }
    $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run the test from an elevated terminal inside the disposable Windows machine.' }
    if (@($installer.RelatedProducts($expectedUpgrade)).Count) { throw 'Use a fresh disposable machine without a registered LightHostModern installation.' }
    $prefs=Join-Path $env:APPDATA 'LightHostModern\LightHostModern.settings'
    if (Test-Path -LiteralPath $prefs) { throw 'The disposable machine already contains application preferences.' }
    Scenario 'Install the previous MSI with its existing machine scope and shortcuts' {
        $installedByTest.Add($previous.ProductCode)
        Msi 'install-previous' @('/i',('"'+$previous.path+'"'),'ADDLOCAL=ALL')
        Verify-Payload $previous.ProductCode | Out-Null
    }
    $fixture='<PROPERTIES><VALUE name="installer-validation" val="'+[guid]::NewGuid().ToString('N')+'"/></PROPERTIES>'
    New-Item -ItemType Directory -Force -Path (Split-Path $prefs) | Out-Null
    [IO.File]::WriteAllText($prefs,$fixture,[Text.UTF8Encoding]::new($false))
    $prefsHash=(Get-FileHash -LiteralPath $prefs).Hash
    Scenario 'Upgrade registration, payload and shortcuts while preserving preferences' {
        $installedByTest.Add($current.ProductCode)
        Msi 'upgrade-current' @('/i',('"'+$current.path+'"'),'ADDLOCAL=ALL')
        $script:installRoot=Verify-Payload $current.ProductCode -Current
        if ($installer.ProductState($previous.ProductCode) -eq 5 -or $installer.ProductInfo($current.ProductCode,'VersionString') -ne $current.ProductVersion) { throw 'The upgrade left incorrect product registration.' }
        if ((Get-FileHash -LiteralPath $prefs).Hash -ne $prefsHash) { throw 'Upgrade changed user preferences.' }
    }
    Scenario 'Repair restores the exact current scanner payload' {
        $scanner=[IO.Path]::GetFullPath((Join-Path $script:installRoot 'LightHostModernScanner.exe'))
        $saved=[IO.Path]::GetFullPath((Join-Path $outputRoot ('repair-scanner-'+[guid]::NewGuid().ToString('N')+'.exe')))
        if (!$scanner.StartsWith($script:installRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !$saved.StartsWith($outputRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Repair fixture paths escaped their verified directories.' }
        $before=(Get-FileHash -LiteralPath $scanner).Hash
        Move-Item -LiteralPath $scanner -Destination $saved
        Msi 'repair-current' @('/fa',$current.ProductCode)
        if ((Get-FileHash -LiteralPath $scanner).Hash -ne $before) { throw 'Repair restored an incorrect scanner.' }
        Verify-Payload $current.ProductCode -Current | Out-Null
    }
    Scenario 'Uninstall removes registration and application files and preserves preferences' {
        Msi 'uninstall-current' @('/x',$current.ProductCode)
        if ($installer.ProductState($current.ProductCode) -eq 5 -or (Test-Path -LiteralPath (Join-Path $script:installRoot 'LightHostModern.exe'))) { throw 'Uninstall left an installed product or executable.' }
        if ((Get-FileHash -LiteralPath $prefs).Hash -ne $prefsHash) { throw 'Uninstall changed user preferences.' }
    }
} finally {
    if ($Execute -and !$script:installerStillRunning) {
        # Clean up only product identities installed by this test. Never enumerate
        # unrelated products through Win32_Product, which can trigger repairs.
        foreach ($code in $installedByTest) {
            if ($installer.ProductState($code) -eq 5) {
                try { Msi ('cleanup-'+$code.Trim('{}')) @('/x',$code) }
                catch { $results.Add([pscustomobject]@{name='cleanup';status='failed';error=$_.Exception.Message}) }
            }
        }
    }
    [pscustomobject]@{executed=[bool]$Execute;scenarios=@($results);installerOutcomes=@($script:installerOutcomes)} |
        ConvertTo-Json -Depth 8 | Set-Content (Join-Path $outputRoot 'results.json') -Encoding UTF8
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
