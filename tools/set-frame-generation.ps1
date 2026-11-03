[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Off','Dynamic')][string]$Mode,
    [ValidateRange(0,1000)][double]$TargetFPS
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'settings.ps1')
$root=Split-Path -Parent $PSScriptRoot
if($PSBoundParameters.ContainsKey('TargetFPS') -and ([double]::IsNaN($TargetFPS) -or [double]::IsInfinity($TargetFPS))){throw 'TargetFPS must be finite'}
$manifest=Get-ElexSettingsManifest $root
Set-ElexIniValue $manifest.Ini 'FrameGeneration' 'Mode' $Mode
if($PSBoundParameters.ContainsKey('TargetFPS')){
    Set-ElexIniValue $manifest.Ini 'FrameGeneration' 'TargetFPS' $TargetFPS.ToString('0.######',[Globalization.CultureInfo]::InvariantCulture)
}
Save-ElexSettingsManifest $root $manifest
Write-Output "FrameGeneration=$Mode written to $($manifest.Ini). Restart ELEX. A custom target requires VSync off."
