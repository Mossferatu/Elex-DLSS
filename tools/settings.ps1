# Shared INI helpers. Section-aware edits preserve unrelated options and comments.
function Set-ElexIniValue([string]$Path,[string]$Section,[string]$Key,[string]$Value) {
    $text=if(Test-Path -LiteralPath $Path){[IO.File]::ReadAllText($Path)}else{''}
    $lines=New-Object 'System.Collections.Generic.List[string]'
    $lines.AddRange([string[]]($text -split '\r?\n'))
    $sectionStart=-1; $sectionEnd=$lines.Count
    for($i=0;$i -lt $lines.Count;$i++){
        if($lines[$i] -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$'){
            if($sectionStart -ge 0){$sectionEnd=$i;break}
            if($Matches[1] -eq $Section){$sectionStart=$i}
        }
    }
    if($sectionStart -lt 0){$lines.Add("[$Section]");$lines.Add("$Key=$Value")}
    else {
        $matched=$false
        for($i=$sectionStart+1;$i -lt $sectionEnd;$i++){
            if($lines[$i] -match ('^\s*'+[regex]::Escape($Key)+'\s*=')){$lines[$i]="$Key=$Value";$matched=$true}
        }
        if(-not $matched){$lines.Insert($sectionEnd,"$Key=$Value")}
    }
    # GetPrivateProfileString supports UTF-16, preserving non-ASCII user comments.
    [IO.File]::WriteAllText($Path,($lines -join "`r`n"),[Text.Encoding]::Unicode)
}

function Get-ElexSettingsManifest([string]$Root) {
    if(Get-Process ELEX -ErrorAction SilentlyContinue){throw 'Close ELEX before changing settings.'}
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'backups\installed.json') -Raw | ConvertFrom-Json
    $system=[IO.Path]::GetFullPath((Join-Path $manifest.Game 'system'))
    if([IO.Path]::GetFullPath($manifest.Ini) -ne (Join-Path $system 'elex-dlss.ini')){throw 'Unexpected configuration path'}
    # A user-updated NVIDIA runtime does not prevent editing the mod's INI.
    $proxy=@($manifest.Files | Where-Object {$_.Target -eq (Join-Path $system 'd3d11.dll')})
    if($proxy.Count -ne 1 -or (Get-FileHash -LiteralPath $proxy[0].Target).Hash.ToLowerInvariant() -ne $proxy[0].Sha256){throw 'Installed mod differs from the installation manifest'}
    return $manifest
}

function Save-ElexSettingsManifest([string]$Root,$Manifest) {
    $Manifest.IniSha256=(Get-FileHash -LiteralPath $Manifest.Ini).Hash.ToLowerInvariant()
    $Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root 'backups\installed.json') -Encoding UTF8
}
