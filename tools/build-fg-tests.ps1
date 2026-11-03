[CmdletBinding()]
param([string]$Zig='zig')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$compiler=(Get-Command $Zig -ErrorAction Stop).Source
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $root '.tools\zig-local-cache'
Push-Location $root
try {
    New-Item -ItemType Directory -Path 'build','build\fg-logs' -Force | Out-Null
    if(-not (Test-Path -LiteralPath 'build\version.h')){throw 'Run build.ps1 first to generate the version header'}
    & $compiler c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -Wall -Wextra -Werror `
        -Wno-cast-function-type-mismatch -O2 -I .deps/StreamlineSDK/include `
        tests/fg_capability.cpp src/streamline_backend.cpp -o build/fg-capability.exe `
        -municode -ld3d12 -ldxgi -lwintrust -lcrypt32 -ldxguid
    if($LASTEXITCODE -ne 0){throw 'Frame-generation capability test build failed'}
    $objects=@()
    foreach($file in @('buffer','hook','trampoline','hde\hde64')) {
        $output='build\fg-mh-'+[IO.Path]::GetFileName($file)+'.o'
        & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -c ("third_party\minhook\src\$file.c") -o $output
        if($LASTEXITCODE -ne 0){throw 'MinHook test build failed'}
        $objects+=$output
    }
    & $compiler c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -Wall -Wextra -Werror `
        -Wno-cast-function-type-mismatch -O2 -I .deps/StreamlineSDK/include `
        tests/fg_bridge_smoke.cpp src/frame_generation.cpp src/streamline_backend.cpp @objects `
        -o build/fg-bridge-smoke.exe -municode -ld3d11 -ld3d12 -ldxgi -ld3dcompiler_47 -lwintrust -lcrypt32 -ldxguid
    if($LASTEXITCODE -ne 0){throw 'Frame-generation bridge test build failed'}
} finally {Pop-Location}
