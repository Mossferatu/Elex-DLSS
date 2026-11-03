# Compile the real ABI bindings, but keep every driver write inside a fake
# ELEX profile. CI needs no NVIDIA hardware and never alters driver settings.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Add-Type -Path (Join-Path $root 'tools\nvidia-profile.cs')
. (Join-Path $root 'tools\nvidia-presets.ps1')
$fixture=Join-Path $root ('build\preset-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$exe=Join-Path $fixture 'ELEX.exe'
$fakeDriver=@{Rows=@(
    [pscustomobject]@{Id=[uint32]0x10e41e03;HasOverride=$true;Value=[uint32]1},
    [pscustomobject]@{Id=[uint32]0x10e41df1;HasOverride=$true;Value=[uint32]2}
);Fail=$false;Writes=0;VSync=1}
function Get-Process {param([string]$Name)}
function Assert-True($Value,[string]$Message){if(-not $Value){throw "FAIL: $Message"}}
function Assert-Throws([scriptblock]$Action,[string]$Message){
    $didThrow=$false;try{& $Action | Out-Null}catch{$didThrow=$true}
    Assert-True $didThrow $Message
}
function Open-ElexFgProfile([string]$Executable,[bool]$Create){
    $api=[pscustomobject]@{Store=$fakeDriver;ProfileName='ELEX'}
    $api | Add-Member -MemberType ScriptMethod -Name Snapshot -Value {
        @($this.Store.Rows | ForEach-Object {[pscustomobject]@{Id=$_.Id;HasOverride=$_.HasOverride;Value=$_.Value}})
    }
    $api | Add-Member -MemberType ScriptMethod -Name Apply -Value {
        param([ElexDlss.DriverSetting[]]$Rows)
        if($this.Store.Fail){throw 'Injected NVIDIA SaveSettings failure'}
        $this.Store.Rows=@($Rows | ForEach-Object {[pscustomobject]@{Id=$_.Id;HasOverride=$_.HasOverride;Value=$_.Value}})
        $this.Store.Writes++
    }
    $api | Add-Member -MemberType ScriptMethod -Name Dispose -Value {}
    $api
}
$baseline=@($fakeDriver.Rows | ForEach-Object {[pscustomobject]@{Id=$_.Id;HasOverride=$_.HasOverride;Value=$_.Value}})
foreach($pair in @(@('A',1),@('B',2),@('C',3),@('Latest',16777215))){
    Set-ElexFgPreset -Root $fixture -Executable $exe -Preset $pair[0] | Out-Null
    Assert-True ($fakeDriver.Rows[0].Value -eq 1 -and $fakeDriver.Rows[1].Value -eq $pair[1]) "FG preset $($pair[0]) mapping"
    Assert-True ($fakeDriver.VSync -eq 1) 'Unrelated settings unchanged'
}
Set-ElexFgPreset -Root $fixture -Executable $exe -Preset Default | Out-Null
Assert-True (Test-ElexDriverState $fakeDriver.Rows $baseline) 'Default restores previous user overrides'
Assert-True (-not (Test-Path (Join-Path $fixture 'backups\nvidia-fg-preset.json'))) 'Restoration journal archived'
$writes=$fakeDriver.Writes
Set-ElexFgPreset -Root $fixture -Executable $exe -Preset Default | Out-Null
Assert-True ($fakeDriver.Writes -eq $writes) 'Default without owned override does not write driver settings'

$fakeDriver.Rows[0].HasOverride=$false;$fakeDriver.Rows[1].HasOverride=$false
$baseline=@($fakeDriver.Rows | ForEach-Object {[pscustomobject]@{Id=$_.Id;HasOverride=$_.HasOverride;Value=$_.Value}})
Set-ElexFgPreset -Root $fixture -Executable $exe -Preset A | Out-Null
$fakeDriver.Fail=$true
Assert-Throws {Set-ElexFgPreset -Root $fixture -Executable $exe -Preset C} 'Driver save failure propagated'
Assert-True ($fakeDriver.Rows[1].Value -eq 1) 'Failed driver save preserves current preset'
$fakeDriver.Fail=$false
Set-ElexFgPreset -Root $fixture -Executable $exe -Preset C | Out-Null
$fakeDriver.Rows[1].Value=2
Assert-Throws {Set-ElexFgPreset -Root $fixture -Executable $exe -Preset Default} 'Independent preset edit protected'
Assert-True ($fakeDriver.Rows[1].Value -eq 2) 'Independent preset edit untouched'
$fakeDriver.Rows[1].Value=3
Set-ElexFgPreset -Root $fixture -Executable $exe -Preset Default | Out-Null
Assert-True (Test-ElexDriverState $fakeDriver.Rows $baseline) 'Inherited settings restored without creating explicit overrides'
Write-Output 'PASS: NVIDIA ABI compiles; FG preset mapping, saved overrides, inherited defaults, failed-save recovery and independent edits verified without driver writes'
