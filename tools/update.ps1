[CmdletBinding()]
param([ValidateSet('','DLAA','Quality','Balanced','Performance')][string]$Mode = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'backups\installed.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (Get-Process ELEX -ErrorAction SilentlyContinue) { throw 'Close ELEX before updating.' }
$system = [IO.Path]::GetFullPath((Join-Path $manifest.Game 'system'))
$proxy = Join-Path $system 'd3d11.dll'
$record = @($manifest.Files | Where-Object { $_.Target -eq $proxy })
if ($record.Count -ne 1) { throw 'Invalid installation manifest' }
if ((Get-FileHash -LiteralPath $proxy).Hash.ToLowerInvariant() -ne $record[0].Sha256) { throw 'Installed DLL changed independently' }
$archive = Join-Path $root ('backups\tested-d3d11-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.dll')
Copy-Item -LiteralPath $proxy -Destination $archive
$source = Join-Path $root 'build\d3d11.dll'
Copy-Item -LiteralPath $source -Destination $proxy
$record[0].Sha256 = (Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant()
if ((Get-FileHash -LiteralPath $proxy).Hash.ToLowerInvariant() -ne $record[0].Sha256) { throw 'Update hash mismatch' }
if ($Mode) {
    $ini = Join-Path $system 'elex-dlss.ini'
    "[DLSS]`r`nMode=$Mode`r`n" | Set-Content -LiteralPath $ini -Encoding ASCII
    $manifest.Mode = $Mode
    $manifest.IniSha256 = (Get-FileHash -LiteralPath $ini).Hash.ToLowerInvariant()
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Output ("Updated DLSS DLL: " + $record[0].Sha256 + '; mode=' + $manifest.Mode)
