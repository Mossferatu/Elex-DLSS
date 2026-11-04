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
    & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror -Wno-cast-function-type-mismatch `
        tests/indicator.c src/indicator.c third_party/minhook/src/buffer.c third_party/minhook/src/hook.c `
        third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c `
        -municode -ladvapi32 -o build/indicator-test.exe
    if($LASTEXITCODE -ne 0){throw 'Indicator test build failed'}
    & .\build\indicator-test.exe ('build\indicator-test-'+[guid]::NewGuid().ToString('N')+'.ini')
    if($LASTEXITCODE -ne 0){throw 'Indicator tests failed'}
} finally {Pop-Location}
