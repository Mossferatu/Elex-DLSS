[CmdletBinding()]
param([string]$Archive, [string]$Destination)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$runtime=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'streamline-runtime.json') -Raw | ConvertFrom-Json
if(-not $Archive){$Archive=Join-Path $root ('.deps\streamline-sdk-v'+$runtime.version+'.zip')}
if(-not $Destination){$Destination=Join-Path $root '.deps\StreamlineSDK'}
$Archive=[IO.Path]::GetFullPath($Archive)
$Destination=[IO.Path]::GetFullPath($Destination)
if(-not (Test-Path -LiteralPath $Archive)){
    New-Item -ItemType Directory -Path (Split-Path -Parent $Archive) -Force | Out-Null
    Invoke-WebRequest -Uri $runtime.url -OutFile $Archive
}
if((Get-FileHash -LiteralPath $Archive).Hash.ToLowerInvariant() -ne $runtime.archiveSha256){throw 'Streamline archive checksum mismatch'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($Archive)
try {
    $selected=@($runtime.licenses)+@($runtime.files.PSObject.Properties | ForEach-Object {'bin/x64/'+$_.Name})
    $entries=@($zip.Entries | Where-Object {$_.FullName -match '^include/[^/]+\.h$' -or $_.FullName -cin $selected})
    foreach($name in $selected){if(-not $zip.GetEntry($name)){throw "Missing Streamline file: $name"}}
    if(-not $zip.GetEntry('include/sl_dlss_g.h')){throw 'Missing Streamline headers'}
    foreach($entry in $entries){
        $target=[IO.Path]::GetFullPath((Join-Path $Destination $entry.FullName))
        if(-not $target.StartsWith($Destination.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe archive path'}
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$true)
    }
} finally {$zip.Dispose()}
foreach($file in $runtime.files.PSObject.Properties){
    if((Get-FileHash -LiteralPath (Join-Path $Destination ('bin\x64\'+$file.Name))).Hash.ToLowerInvariant() -ne $file.Value){throw "Streamline runtime checksum mismatch: $($file.Name)"}
}
Write-Output "Verified Streamline $($runtime.version) production SDK: $Destination"
