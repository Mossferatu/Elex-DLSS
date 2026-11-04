[CmdletBinding()]
param([string]$Zig='zig')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$compiler=(Get-Command $Zig -ErrorAction Stop).Source
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $root '.tools\zig-local-cache'
Push-Location $root
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror `
        tests/pipeline_state.c src/pipeline_state.c -ld3d11 -ldxguid -o build/pipeline-state-test.exe
    if($LASTEXITCODE -ne 0){throw 'Pipeline state test build failed'}
    & .\build\pipeline-state-test.exe
    if($LASTEXITCODE -ne 0){throw 'Pipeline state test failed'}
} finally {Pop-Location}
