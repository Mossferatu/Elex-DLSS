[CmdletBinding()]
param([ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version='0.1.0')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root ('build\package-' + $Version + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$sourceStage = $stage + '-source'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Path $stage,$sourceStage,$dist,(Join-Path $stage 'system'),(Join-Path $stage 'tools'),(Join-Path $stage 'third_party\cloud-water'),(Join-Path $stage 'third_party\minhook') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'build\d3d11.dll') -Destination (Join-Path $stage 'system\d3d11.dll')
$runtime = Join-Path $root '.deps\DLSS\lib\Windows_x86_64\rel\nvngx_dlss.dll'
if ((Get-FileHash -LiteralPath $runtime).Hash.ToLowerInvariant() -ne 'd00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e') {
    throw 'Unexpected NVIDIA runtime; verify SDK provenance before packaging.'
}
Copy-Item -LiteralPath $runtime -Destination (Join-Path $stage 'system\nvngx_dlss.dll')
Copy-Item -LiteralPath (Join-Path $root 'elex-dlss.ini') -Destination (Join-Path $stage 'system\elex-dlss.ini')
foreach ($file in @('README.md','CHANGELOG.md','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $stage
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $sourceStage
}
foreach ($file in @('install.ps1','rollback.ps1','set-mode.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $stage 'tools')
}
Copy-Item -LiteralPath (Join-Path $root 'third_party\cloud-water\LICENSE.txt') -Destination (Join-Path $stage 'third_party\cloud-water')
Copy-Item -LiteralPath (Join-Path $root 'third_party\minhook\LICENSE.txt') -Destination (Join-Path $stage 'third_party\minhook')
Copy-Item -LiteralPath (Join-Path $root '.deps\DLSS\LICENSE.txt') -Destination (Join-Path $stage 'NVIDIA-LICENSE.txt')
foreach ($folder in @('src','third_party','tools','docs')) {
    Copy-Item -LiteralPath (Join-Path $root $folder) -Destination $sourceStage -Recurse
}
foreach ($file in @('build.ps1','elex-dlss.ini','.gitignore')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $sourceStage
}
$binaryZip = Join-Path $dist "ELEX-DLSS-$Version-Windows-x64.zip"
$sourceZip = Join-Path $dist "ELEX-DLSS-$Version-Source.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $binaryZip -Force
Compress-Archive -Path (Join-Path $sourceStage '*') -DestinationPath $sourceZip -Force
$hashes = foreach ($item in @($binaryZip,$sourceZip,(Join-Path $stage 'system\d3d11.dll'),(Join-Path $stage 'system\nvngx_dlss.dll'))) {
    (Get-FileHash -LiteralPath $item).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($item)
}
$hashes | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ASCII
@{BinaryStage=$stage;SourceStage=$sourceStage;BinaryZip=$binaryZip;SourceZip=$sourceZip} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'build\package-paths.json') -Encoding UTF8
Write-Output $binaryZip
Write-Output $sourceZip
