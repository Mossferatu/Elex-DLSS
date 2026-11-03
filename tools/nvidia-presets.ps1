# FG model selection uses NVIDIA's public per-application DRS API. It does not
# change the native Dynamic scheduler, VSync, FPS limits, or any global setting.
function Open-ElexFgProfile([string]$Executable,[bool]$Create) {
    if(-not ('ElexDlss.NvidiaFgProfile' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'nvidia-profile.cs')}
    [ElexDlss.NvidiaFgProfile]::Open($Executable,$Create)
}

function Test-ElexDriverState($Actual,$Expected) {
    if(@($Actual).Count -ne 2 -or @($Expected).Count -ne 2){return $false}
    for($i=0;$i -lt 2;$i++){
        if($Actual[$i].Id -ne $Expected[$i].Id -or $Actual[$i].HasOverride -ne $Expected[$i].HasOverride){return $false}
        if($Actual[$i].HasOverride -and $Actual[$i].Value -ne $Expected[$i].Value){return $false}
    }
    return $true
}

function Get-ElexFgPreset([string]$Executable) {
    $api=Open-ElexFgProfile $Executable $false
    try {
        $rows=@($api.Snapshot())
        $labels=@{[uint32]0='Application';[uint32]1='A';[uint32]2='B';[uint32]3='C';[uint32]16777214='Default';[uint32]16777215='Latest'}
        $name=$labels[[uint32]$rows[1].Value]
        if(-not $name){$name='Driver value '+$rows[1].Value}
        [pscustomobject]@{Profile=$api.ProfileName;OverrideEnabled=($rows[0].Value -eq 1);Preset=$name;UserOverride=$rows[1].HasOverride}
    } finally {$api.Dispose()}
}

function Set-ElexFgPreset {
    param([string]$Root,[string]$Executable,[ValidateSet('Default','A','B','C','Latest')][string]$Preset)
    if(Get-Process ELEX -ErrorAction SilentlyContinue){throw 'Close ELEX before changing NVIDIA model presets.'}
    $statePath=Join-Path $Root 'backups\nvidia-fg-preset.json'
    $saved=if(Test-Path -LiteralPath $statePath){Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json}else{$null}
    if($Preset -eq 'Default' -and -not $saved){Write-Output 'No mod-owned FG preset override to restore; existing NVIDIA settings retained.';return}
    $api=Open-ElexFgProfile $Executable ($Preset -ne 'Default')
    try {
        $before=@($api.Snapshot())
        if($saved){
            if($saved.Executable -ne [IO.Path]::GetFullPath($Executable) -or $saved.Profile -cne $api.ProfileName){throw 'FG preset backup belongs to a different application/profile.'}
            # Either side of the prior atomic SaveSettings is recoverable if a
            # process exited after writing its journal but before committing.
            if(-not (Test-ElexDriverState $before $saved.After) -and -not (Test-ElexDriverState $before $saved.Before)){
                throw 'The NVIDIA FG preset was changed independently. Existing driver settings and backup have been retained.'
            }
        }
        $original=if($saved){@($saved.Original)}else{$before}
        if($Preset -eq 'Default'){$after=$original}
        else {
            $values=@{A=1;B=2;C=3;Latest=16777215}
            $after=@(
                [pscustomobject]@{Id=[uint32]0x10e41e03;HasOverride=$true;Value=[uint32]1},
                [pscustomobject]@{Id=[uint32]0x10e41df1;HasOverride=$true;Value=[uint32]$values[$Preset]}
            )
        }
        $journal=@{Executable=[IO.Path]::GetFullPath($Executable);Profile=$api.ProfileName;Original=$original;Before=$before;After=$after;Requested=$Preset}
        New-Item -ItemType Directory -Path (Split-Path -Parent $statePath) -Force | Out-Null
        $journal | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statePath -Encoding UTF8
        $typed=@($after | ForEach-Object {
            $row=New-Object ElexDlss.DriverSetting
            $row.Id=$_.Id;$row.HasOverride=$_.HasOverride;$row.Value=$_.Value
            $row
        })
        $api.Apply([ElexDlss.DriverSetting[]]$typed)
    } finally {$api.Dispose()}
    $verify=Open-ElexFgProfile $Executable $false
    try {
        if(-not (Test-ElexDriverState @($verify.Snapshot()) $after)){throw 'NVIDIA FG preset readback mismatch. The restoration journal has been retained.'}
    } finally {$verify.Dispose()}
    if($Preset -eq 'Default'){
        Rename-Item -LiteralPath $statePath -NewName ('nvidia-fg-preset-restored-'+[guid]::NewGuid().ToString('N')+'.json')
        Write-Output 'Previous ELEX-only NVIDIA FG preset settings restored.'
    } else {Write-Output "NVIDIA FG preset=$Preset applied to ELEX only. Previous values saved in $statePath. Restart ELEX."}
}
