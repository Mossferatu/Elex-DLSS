[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'backups\installed.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (Get-Process ELEX -ErrorAction SilentlyContinue) { throw 'Close ELEX before rollback.' }
$system = [IO.Path]::GetFullPath((Join-Path $manifest.Game 'system'))
foreach ($entry in $manifest.Files) {
    $path = [IO.Path]::GetFullPath($entry.Target)
    if ([IO.Path]::GetDirectoryName($path) -ne $system) { throw 'Manifest target outside game system directory.' }
    if ((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.Sha256) {
        throw "Installed file changed independently: $path"
    }
}
$proxyBackup = Join-Path $manifest.Backup 'd3d11.dll'
if ((Get-FileHash -LiteralPath $proxyBackup).Hash.ToLowerInvariant() -ne $manifest.ProxyBeforeSha256) { throw 'Backup hash mismatch' }
$configBackup = Join-Path $manifest.Backup 'ConfigUser.xml'
if ((Get-FileHash -LiteralPath $configBackup).Hash.ToLowerInvariant() -ne $manifest.ConfigBeforeSha256) { throw 'Configuration backup hash mismatch' }
$ini = [IO.Path]::GetFullPath($manifest.Ini)
if ([IO.Path]::GetDirectoryName($ini) -ne $system) { throw 'INI path outside game system directory' }
$runtime = Join-Path $system 'nvngx_dlss.dll'
if (@($manifest.Files | Where-Object { [IO.Path]::GetFullPath($_.Target) -eq $runtime }).Count -ne 1) {
    throw 'NGX runtime not present in the verified manifest'
}
Write-Output 'Restore original cloud/water DLL; remove only the installed NGX runtime and mod INI; restore prior AA configuration.'
if (-not $Apply) { Write-Output 'Dry run. Use -Apply after review.'; return }
Copy-Item -LiteralPath $proxyBackup -Destination (Join-Path $system 'd3d11.dll')
Remove-Item -LiteralPath $runtime
if ((Get-FileHash -LiteralPath $ini).Hash.ToLowerInvariant() -eq $manifest.IniSha256) { Remove-Item -LiteralPath $ini }
if ((Get-FileHash -LiteralPath $manifest.Config).Hash.ToLowerInvariant() -eq $manifest.ConfigAfterSha256) {
    Copy-Item -LiteralPath (Join-Path $manifest.Backup 'ConfigUser.xml') -Destination $manifest.Config
} else { Write-Output 'Game configuration changed after installation; retained current configuration.' }
Rename-Item -LiteralPath $manifestPath -NewName ('rolled-back-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
Write-Output 'Original graphics DLL restored and installation rolled back.'
