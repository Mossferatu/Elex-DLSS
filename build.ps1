[CmdletBinding()]
param(
    [string]$Zig = 'zig',
    [string]$Sdk,
    [string]$StreamlineSdk,
    [string]$Version = '0.1.0',
    [switch]$ReplayTest
)
$ErrorActionPreference = 'Stop'
$Version = & (Join-Path $PSScriptRoot 'tools\resolve-version.ps1') -Version $Version
if (-not $Sdk) { $Sdk = Join-Path $PSScriptRoot '.deps\DLSS' }
if (-not $StreamlineSdk) { $StreamlineSdk = Join-Path $PSScriptRoot '.deps\StreamlineSDK' }
$compiler = (Get-Command $Zig -ErrorAction Stop).Source
$sdkPath = (Resolve-Path -LiteralPath $Sdk).Path
$streamlinePath = (Resolve-Path -LiteralPath $StreamlineSdk).Path
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot '.tools\zig-local-cache'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build','build\ngx-data' -Force | Out-Null
    ('#define ELEX_DLSS_VERSION "' + $Version + '"') | Set-Content -LiteralPath 'build\version.h' -Encoding ASCII
    & $compiler c++ -target x86_64-windows-msvc -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-command-line-argument `
        -fno-exceptions -fno-rtti -fno-stack-protector -nostdlib `
        -I (Join-Path $sdkPath 'include') -c src\ngx_parameters.cpp -o build\ngx-parameters.obj
    if ($LASTEXITCODE -ne 0) { throw 'NGX ABI adapter build failed' }
    & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror `
        -Wno-cast-function-type-mismatch -I (Join-Path $sdkPath 'include') `
        src\ngx_probe.c build\ngx-parameters.obj `
        -o build\ngx-probe.exe -municode -ld3d11 -ldxgi -ladvapi32
    if ($LASTEXITCODE -ne 0) { throw 'NGX probe build failed' }
    python tools\embed_shaders.py
    if ($LASTEXITCODE -ne 0) { throw 'Shader embedding failed' }
    $frameGenerationObjects=@()
    foreach($source in @('streamline_backend','frame_generation','graphics_hooks')) {
        $object="build\$source.o"
        & $compiler c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -Wall -Wextra -Werror `
            -Wno-cast-function-type-mismatch -I (Join-Path $streamlinePath 'include') -c "src\$source.cpp" -o $object
        if($LASTEXITCODE -ne 0){throw "Frame-generation build failed: $source"}
        $frameGenerationObjects+=$object
    }
    $rendererArgs = @()
    $rendererOutput = 'build\d3d11.dll'
    if ($ReplayTest) { $rendererArgs += '-DELEX_REPLAY_TEST'; $rendererOutput = 'build\elex-dlss-replay.dll' }
    & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror `
        -Wno-cast-function-type-mismatch -I (Join-Path $sdkPath 'include') -I third_party\cloud-water `
        @rendererArgs `
        -shared src\d3d11.c src\renderer.c src\engine_hooks.c src\ngx_backend.c src\indicator.c src\pipeline_state.c build\ngx-parameters.obj @frameGenerationObjects src\d3d11.def `
        third_party\cloud-water\patch.c third_party\cloud-water\water.c `
        third_party\minhook\src\buffer.c third_party\minhook\src\hook.c `
        third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c `
        -o $rendererOutput -lbcrypt -luser32 -ladvapi32 -ldxguid -ld3d12 -ldxgi -lwintrust -lcrypt32 -lc++
    if ($LASTEXITCODE -ne 0) { throw 'DLSS renderer build failed' }
    $versionFile = if ($ReplayTest) { 'build\replay-version.txt' } else { 'build\version.txt' }
    $Version | Set-Content -LiteralPath $versionFile -Encoding ASCII
} finally { Pop-Location }
