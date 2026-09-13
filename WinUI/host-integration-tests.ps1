param([Parameter(Mandatory)][string] $HostExecutable)
# Integration checks use an isolated profile. Startup registration changes and
# MSI lifecycle checks belong in the disposable installation-test environment.
& "$PSScriptRoot\..\Tests\ProfileIntegrationTests.ps1" -HostExecutable $HostExecutable
exit $LASTEXITCODE
