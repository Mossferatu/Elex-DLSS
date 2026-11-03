[CmdletBinding()]
param(
    [ValidateSet('Off','Dynamic')][string]$Mode,
    [ValidateRange(0,1000)][double]$TargetFPS,
    [ValidateSet('Default','A','B','C','Latest')][string]$Preset,
    [switch]$ShowPreset
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'settings.ps1')
. (Join-Path $PSScriptRoot 'nvidia-presets.ps1')
$root=Split-Path -Parent $PSScriptRoot
if($PSBoundParameters.ContainsKey('TargetFPS') -and ([double]::IsNaN($TargetFPS) -or [double]::IsInfinity($TargetFPS))){throw 'TargetFPS must be finite'}
$manifest=Get-ElexSettingsManifest $root
if(-not ($PSBoundParameters.ContainsKey('Mode') -or $PSBoundParameters.ContainsKey('TargetFPS') -or $PSBoundParameters.ContainsKey('Preset') -or $ShowPreset)){
    throw 'Specify -Mode, -TargetFPS, -Preset, and/or -ShowPreset.'
}
$exe=Join-Path $manifest.Game 'system\ELEX.exe'
if($PSBoundParameters.ContainsKey('Preset')){Set-ElexFgPreset -Root $root -Executable $exe -Preset $Preset}
if($PSBoundParameters.ContainsKey('Mode')){Set-ElexIniValue $manifest.Ini 'FrameGeneration' 'Mode' $Mode}
if($PSBoundParameters.ContainsKey('TargetFPS')){
    Set-ElexIniValue $manifest.Ini 'FrameGeneration' 'TargetFPS' $TargetFPS.ToString('0.######',[Globalization.CultureInfo]::InvariantCulture)
}
if($PSBoundParameters.ContainsKey('Mode') -or $PSBoundParameters.ContainsKey('TargetFPS')){
    Save-ElexSettingsManifest $root $manifest
    Write-Output "Frame-generation settings written to $($manifest.Ini). Restart ELEX. A custom target requires VSync off."
}
if($ShowPreset){Get-ElexFgPreset $exe}
