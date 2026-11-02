[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
$normalized = $Version -creplace '^v', ''
if ($normalized -cnotmatch '\A(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?\z') {
    throw 'Version must be major.minor.patch with an optional prerelease suffix, such as v0.2.0-beta.1.'
}
Write-Output $normalized
