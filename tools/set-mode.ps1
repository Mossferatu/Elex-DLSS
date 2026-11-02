[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('DLAA','Quality','Balanced','Performance')][string]$Mode)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'backups\installed.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (Get-Process ELEX -ErrorAction SilentlyContinue) { throw 'Close ELEX before changing mode.' }
$system = [IO.Path]::GetFullPath((Join-Path $manifest.Game 'system'))
$ini = [IO.Path]::GetFullPath($manifest.Ini)
if ($ini -ne (Join-Path $system 'elex-dlss.ini')) { throw 'Unexpected installed configuration path.' }
foreach ($entry in $manifest.Files) {
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($entry.Target)) -ne $system) { throw 'Unexpected installation path.' }
    if ((Get-FileHash -LiteralPath $entry.Target).Hash.ToLowerInvariant() -ne $entry.Sha256) { throw 'Installed DLL differs from the installation manifest.' }
}
"[DLSS]`r`nMode=$Mode`r`n" | Set-Content -LiteralPath $ini -Encoding ASCII
$manifest.Mode = $Mode
$manifest.IniSha256 = (Get-FileHash -LiteralPath $ini).Hash.ToLowerInvariant()
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output "Mode=$Mode written to $ini. Start ELEX normally through Steam."
