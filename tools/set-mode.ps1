[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('DLAA','Quality','Balanced','Performance')][string]$Mode)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'settings.ps1')
$root = Split-Path -Parent $PSScriptRoot
$manifest = Get-ElexSettingsManifest $root
$ini=$manifest.Ini
Set-ElexIniValue $ini 'DLSS' 'Mode' $Mode
$manifest.Mode = $Mode
Save-ElexSettingsManifest $root $manifest
Write-Output "Mode=$Mode written to $ini. Start ELEX normally through Steam."
