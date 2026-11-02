[CmdletBinding()]
param(
    [ValidateSet('DLAA','Quality','Balanced','Performance')][string]$Mode = 'Quality',
    [string]$Game = 'C:\Program Files (x86)\Steam\steamapps\common\ELEX',
    [string]$ConfigPath,
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$system = Join-Path $Game 'system'
$config = if ($ConfigPath) { [IO.Path]::GetFullPath($ConfigPath) } else { Join-Path $env:LOCALAPPDATA 'ELEX\Config\ConfigUser.xml' }
$exe = Join-Path $system 'ELEX.exe'
if (Get-Process ELEX -ErrorAction SilentlyContinue) { throw 'Close ELEX before installing.' }
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
if ($exeHash -ne '7fe4f4c5a463f7b428ef74d8fe07445f734ff0dc0ad1005ed50cab34bc7f0fd5') {
    throw 'Unsupported ELEX executable. No files changed.'
}
$source = Join-Path $root 'build\d3d11.dll'
$runtime = Join-Path $root '.deps\DLSS\lib\Windows_x86_64\rel\nvngx_dlss.dll'
if (Test-Path -LiteralPath (Join-Path $root 'system\d3d11.dll')) {
    $source = Join-Path $root 'system\d3d11.dll'
    $runtime = Join-Path $root 'system\nvngx_dlss.dll'
}
$proxy = Join-Path $system 'd3d11.dll'
$ini = Join-Path $system 'elex-dlss.ini'
$installedRuntime = Join-Path $system 'nvngx_dlss.dll'
$manifestFile = Join-Path $root 'backups\installed.json'
if (Test-Path -LiteralPath $manifestFile) { throw 'An installation manifest exists; use update or rollback first.' }
$proxyExisted = Test-Path -LiteralPath $proxy
$oldHash = $null
if ($proxyExisted) {
    $oldHash = (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($oldHash -ne '33b9b0a50c5bf34f1a8566acbd8f38210b2a64176627270c7a088ca749c48eb6') {
        throw 'An unknown graphics wrapper is installed. Resolve that conflict before installing DLSS. No files changed.'
    }
}
foreach ($target in @($ini,$installedRuntime)) {
    if (Test-Path -LiteralPath $target) { throw "Existing file would be replaced: $target" }
}
foreach ($file in @($source,$runtime,$config)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file" }
}
$originalConfig = [IO.File]::ReadAllBytes($config)
$encoding = [Text.Encoding]::GetEncoding(28591)
$text = $encoding.GetString($originalConfig)
if ($text -match 'AntiAliasing="[^"]+"') {
    $updatedText = [regex]::Replace($text,'AntiAliasing="[^"]+"','AntiAliasing="TSMAA"')
} else {
    # A freshly generated user config can omit default graphics settings.
    $document = New-Object System.Xml.XmlDocument
    $document.XmlResolver = $null
    $document.PreserveWhitespace = $true
    $document.LoadXml($text)
    if ($document.DocumentElement.Name -ne 'options') { throw 'Unexpected game configuration format.' }
    $parent = $document.DocumentElement
    foreach ($name in @('Engine','Render','PostProcessing')) {
        $child = $parent.SelectSingleNode($name)
        if (-not $child) { $child = $parent.AppendChild($document.CreateElement($name)) }
        $parent = $child
    }
    $parent.SetAttribute('AntiAliasing','TSMAA')
    $updatedText = $document.OuterXml
}
$files = @(
    @{ Source=$source; Target=$proxy; Sha256=(Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() },
    @{ Source=$runtime; Target=$installedRuntime; Sha256=(Get-FileHash -LiteralPath $runtime).Hash.ToLowerInvariant() }
)
$files | ForEach-Object { Write-Output ("Install {0} -> {1} SHA256={2}" -f $_.Source,$_.Target,$_.Sha256) }
Write-Output "Mode=$Mode; in-game AA=TSMAA; existing supported graphics DLL and configuration will be preserved."
if (-not $Apply) { Write-Output 'Dry run only. Use -Apply after review.'; return }
$backup = Join-Path $root ('backups\install-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $backup -ErrorAction Stop | Out-Null
if ($proxyExisted) { Copy-Item -LiteralPath $proxy -Destination (Join-Path $backup 'd3d11.dll') }
Copy-Item -LiteralPath $config -Destination (Join-Path $backup 'ConfigUser.xml')
$manifest = @{
    Game=$Game; Backup=$backup; Config=$config; ConfigBeforeSha256=(Get-FileHash -LiteralPath $config).Hash.ToLowerInvariant()
    ProxyExisted=$proxyExisted; ProxyBeforeSha256=$oldHash; Files=$files; Ini=$ini; Mode=$Mode; Complete=$false
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestFile -Encoding UTF8
$iniWritten = $false
try {
    foreach ($file in $files) { Copy-Item -LiteralPath $file.Source -Destination $file.Target }
    "[DLSS]`r`nMode=$Mode`r`n" | Set-Content -LiteralPath $ini -Encoding ASCII
    $iniWritten = $true
    $manifest.IniSha256 = (Get-FileHash -LiteralPath $ini).Hash.ToLowerInvariant()
    [IO.File]::WriteAllBytes($config,$encoding.GetBytes($updatedText))
    foreach ($file in $files) {
        if ((Get-FileHash -LiteralPath $file.Target).Hash.ToLowerInvariant() -ne $file.Sha256) { throw 'Installed hash mismatch' }
    }
    $manifest.ConfigAfterSha256 = (Get-FileHash -LiteralPath $config).Hash.ToLowerInvariant()
    $manifest.Complete = $true
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestFile -Encoding UTF8
    Write-Output "Installed and verified. Rollback: tools\rollback.ps1 -Apply. Backup: $backup"
} catch {
    $failure = $_
    if ($proxyExisted) {
        Copy-Item -LiteralPath (Join-Path $backup 'd3d11.dll') -Destination $proxy
    } elseif ((Test-Path -LiteralPath $proxy) -and (Get-FileHash -LiteralPath $proxy).Hash.ToLowerInvariant() -eq $files[0].Sha256) {
        Remove-Item -LiteralPath $proxy
    }
    if ((Test-Path -LiteralPath $installedRuntime) -and (Get-FileHash -LiteralPath $installedRuntime).Hash.ToLowerInvariant() -eq $files[1].Sha256) {
        Remove-Item -LiteralPath $installedRuntime
    }
    if ($iniWritten -and (Test-Path -LiteralPath $ini) -and (Get-FileHash -LiteralPath $ini).Hash.ToLowerInvariant() -eq $manifest.IniSha256) {
        Remove-Item -LiteralPath $ini
    }
    Copy-Item -LiteralPath (Join-Path $backup 'ConfigUser.xml') -Destination $config
    Rename-Item -LiteralPath $manifestFile -NewName ('failed-install-' + [guid]::NewGuid().ToString('N') + '.json')
    throw $failure
}
