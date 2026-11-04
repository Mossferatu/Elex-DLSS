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
    $objects=@()
    foreach($file in @('buffer','hook','trampoline','hde\hde64')){
        $object='build\hook-test-'+[IO.Path]::GetFileName($file)+'.o'
        & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -c "third_party\minhook\src\$file.c" -o $object
        if($LASTEXITCODE -ne 0){throw 'Graphics hook test dependency failed'}
        $objects+=$object
    }
    & $compiler c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -Wall -Wextra -Werror `
        -Wno-cast-function-type-mismatch tests/graphics_hooks.cpp src/graphics_hooks.cpp @objects -o build/graphics-hooks-test.exe
    if($LASTEXITCODE -ne 0){throw 'Graphics hook test build failed'}
    & .\build\graphics-hooks-test.exe
    if($LASTEXITCODE -ne 0){throw 'Graphics hook tests failed'}
} finally {Pop-Location}
