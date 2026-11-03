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
# Older manifests always represented an existing cloud/water DLL.
$proxyExisted = if ($null -ne $manifest.ProxyExisted) { [bool]$manifest.ProxyExisted } else { [bool]$manifest.ProxyBeforeSha256 }
if ($proxyExisted -and (Get-FileHash -LiteralPath $proxyBackup).Hash.ToLowerInvariant() -ne $manifest.ProxyBeforeSha256) { throw 'Backup hash mismatch' }
$configBackup = Join-Path $manifest.Backup 'ConfigUser.xml'
if ((Get-FileHash -LiteralPath $configBackup).Hash.ToLowerInvariant() -ne $manifest.ConfigBeforeSha256) { throw 'Configuration backup hash mismatch' }
$ini = [IO.Path]::GetFullPath($manifest.Ini)
if ([IO.Path]::GetDirectoryName($ini) -ne $system) { throw 'INI path outside game system directory' }
$runtime = Join-Path $system 'nvngx_dlss.dll'
if (@($manifest.Files | Where-Object { [IO.Path]::GetFullPath($_.Target) -eq $runtime }).Count -ne 1) {
    throw 'NGX runtime not present in the verified manifest'
}
Write-Output 'Restore the prior graphics DLL when present, otherwise remove the mod proxy; remove mod-owned NVIDIA/Streamline runtimes and unchanged INI; restore prior AA configuration.'
if (-not $Apply) { Write-Output 'Dry run. Use -Apply after review.'; return }
if(Test-Path -LiteralPath (Join-Path $root 'backups\nvidia-fg-preset.json')){
    . (Join-Path $PSScriptRoot 'nvidia-presets.ps1')
    Set-ElexFgPreset -Root $root -Executable (Join-Path $system 'ELEX.exe') -Preset Default
}
if ($proxyExisted) {
    Copy-Item -LiteralPath $proxyBackup -Destination (Join-Path $system 'd3d11.dll')
} else {
    Remove-Item -LiteralPath (Join-Path $system 'd3d11.dll')
}
foreach ($entry in $manifest.Files) {
    if ([IO.Path]::GetFullPath($entry.Target) -eq (Join-Path $system 'd3d11.dll')) { continue }
    if ($entry.RetainOnRollback) { Write-Output "Retained pre-existing user runtime: $($entry.Target)" }
    else { Remove-Item -LiteralPath $entry.Target }
}
if ((Get-FileHash -LiteralPath $ini).Hash.ToLowerInvariant() -eq $manifest.IniSha256) { Remove-Item -LiteralPath $ini }
if ((Get-FileHash -LiteralPath $manifest.Config).Hash.ToLowerInvariant() -eq $manifest.ConfigAfterSha256) {
    Copy-Item -LiteralPath (Join-Path $manifest.Backup 'ConfigUser.xml') -Destination $manifest.Config
} else { Write-Output 'Game configuration changed after installation; retained current configuration.' }
Rename-Item -LiteralPath $manifestPath -NewName ('rolled-back-' + [guid]::NewGuid().ToString('N') + '.json')
Write-Output 'Prior graphics installation restored.'
