[CmdletBinding()]
param([string]$Zig='zig')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$compiler=(Get-Command $Zig -ErrorAction Stop).Source
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root '.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $root '.tools\zig-local-cache'
Push-Location $root
try {
    $lab=Join-Path $root ('build\reentry-test-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $lab | Out-Null
    $objects=@()
    foreach($file in @('buffer','hook','trampoline','hde\hde64')){
        $object=Join-Path $lab ([IO.Path]::GetFileName($file)+'.o')
        & $compiler cc -target x86_64-windows-gnu -std=c11 -O2 -c "third_party\minhook\src\$file.c" -o $object
        if($LASTEXITCODE -ne 0){throw 'Reentry test dependency failed'}
        $objects+=$object
    }
    $exe=Join-Path $lab 'proxy-reentry.exe'
    & $compiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type-mismatch `
        tests/proxy_reentry.cpp @objects -municode -o $exe
    if($LASTEXITCODE -ne 0){throw 'Reentry test build failed'}
    Copy-Item -LiteralPath build/d3d11.dll -Destination $lab
    "[DLSS]`r`nIndicator=Driver`r`n" | Set-Content -LiteralPath (Join-Path $lab 'elex-dlss.ini') -Encoding ASCII
    $output=Join-Path $lab 'result.txt'
    $process=Start-Process -FilePath $exe -ArgumentList ('"'+(Join-Path $lab 'd3d11.dll')+'"') -WorkingDirectory $lab -WindowStyle Hidden -RedirectStandardOutput $output -PassThru
    if(-not $process.WaitForExit(30000)){
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        throw 'Proxy reentry test timed out (possible initialization deadlock)'
    }
    $text=Get-Content -LiteralPath $output -Raw
    if($text -notmatch 'PASS:') {throw "Proxy reentry failed: $text"}
    $match=[regex]::Match($text,'Expected renderer device=([0-9A-Fa-f]+)')
    $log=Get-Content -LiteralPath (Join-Path $lab 'elex-dlss.log') -Raw
    if(-not $match.Success -or $log -notmatch ('DLSS device candidate: '+$match.Groups[1].Value)) {throw 'Renderer attached to an overlay probe instead of the outer device'}
    if(@([regex]::Matches($log,'DLSS device candidate:')).Count -ne 1){throw 'Multiple renderer candidates during nested device creation'}
    Write-Output $text.Trim()
} finally {Pop-Location}
