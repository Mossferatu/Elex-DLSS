[CmdletBinding()]
param(
    [ValidateSet('DLAA','Quality','Balanced','Performance')][string]$Mode,
    [ValidateSet('Default','E','F','J','K','L','M')][string]$Preset
)
$ErrorActionPreference = 'Stop'
if(-not $PSBoundParameters.Count){throw 'Specify -Mode and/or -Preset.'}
. (Join-Path $PSScriptRoot 'settings.ps1')
$root = Split-Path -Parent $PSScriptRoot
$manifest = Get-ElexSettingsManifest $root
$ini=$manifest.Ini
if($PSBoundParameters.ContainsKey('Mode')){
    Set-ElexIniValue $ini 'DLSS' 'Mode' $Mode
    $manifest.Mode = $Mode
}
if($PSBoundParameters.ContainsKey('Preset')){Set-ElexIniValue $ini 'DLSS' 'Preset' $Preset}
Save-ElexSettingsManifest $root $manifest
Write-Output "DLSS settings written to $ini. Restart ELEX. Model presets are hints; NVIDIA overrides may take precedence."
