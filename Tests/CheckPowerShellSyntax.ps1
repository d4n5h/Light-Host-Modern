$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$checked=0
$failures=[Collections.Generic.List[object]]::new()
foreach ($directory in @('Tests','WinUI','Utilities')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo $directory) -File -Filter '*.ps1') {
        $tokens=$null; $parseErrors=$null
        [Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$parseErrors) | Out-Null
        foreach ($error in $parseErrors) {
            $failures.Add([pscustomobject]@{file=$file.FullName;line=$error.Extent.StartLineNumber;message=$error.Message})
        }
        ++$checked
    }
}
if ($failures.Count) { $failures | ConvertTo-Json -Depth 4; exit 1 }
Write-Output "PowerShell syntax passed: $checked scripts."
