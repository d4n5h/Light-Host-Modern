param([Parameter(Mandatory)][string]$Metadata)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$info=Get-Content -LiteralPath $Metadata -Raw | ConvertFrom-Json
$testRoot=[IO.Path]::GetFullPath((Join-Path $repo 'out\test-profiles'))+'\'
if (!$info.name -or ![IO.Path]::GetFullPath($info.profile).StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $info.profile -Leaf) -ne $info.name) { throw 'An isolated workspace test profile is required.' }
foreach ($role in @('ui','host')) {
    $id=$info.($role+'Pid')
    if (!$id) { continue }
    $process=Get-CimInstance Win32_Process -Filter "ProcessId=$id"
    if (!$process) { continue }
    if (!$process.ExecutablePath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or !$process.CommandLine.Contains('--test-profile='+$info.name)) { throw "PID $id no longer belongs to this test profile." }
    if ($role -eq 'ui') {
        & winapp ui invoke Close -a $id --json
        if ($LASTEXITCODE -ne 0) { throw 'The test UI did not accept Close.' }
    } else {
        $hello=Send-HostRequest $info.pipe 'hello'
        Send-HostRequest $info.pipe 'quit-host' -Session $hello.hostSession | Out-Null
    }
    Get-Process -Id $id -ErrorAction SilentlyContinue | Wait-Process -Timeout 15
}
