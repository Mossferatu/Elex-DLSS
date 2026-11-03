# Filesystem integration tests. Everything written stays in generated fixtures.
# The two game identities are mocked; no proprietary game files or GPU are needed.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $root ('build\release-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$testState = @{FailCopyTo=''}

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $didThrow = $false
    try { & $Action | Out-Null } catch { $didThrow = $true }
    Assert-True $didThrow $Message
}
function Get-Process {
    [CmdletBinding()]
    param([string]$Name)
    if ($Name -ne 'ELEX') { throw 'Tests may only query the mocked game process.' }
}
function Get-FileHash {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$Algorithm = 'SHA256')
    $item = Get-Item -LiteralPath $LiteralPath
    if ($item.Length -lt 64) {
        $marker = [IO.File]::ReadAllText($item.FullName)
        if ($marker -eq 'fixture-elex-executable') {
            return [pscustomobject]@{ Hash='7fe4f4c5a463f7b428ef74d8fe07445f734ff0dc0ad1005ed50cab34bc7f0fd5' }
        }
        if ($marker -eq 'fixture-cloud-fix') {
            return [pscustomobject]@{ Hash='33b9b0a50c5bf34f1a8566acbd8f38210b2a64176627270c7a088ca749c48eb6' }
        }
    }
    Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
}
function Copy-Item {
    [CmdletBinding()]
    param([string[]]$LiteralPath, [string]$Destination, [switch]$Recurse)
    if ($testState.FailCopyTo -and $Destination -eq $testState.FailCopyTo) { throw 'Injected runtime-copy failure.' }
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
}
function New-Fixture([string]$Name, [string]$Proxy = '') {
    $base = Join-Path $testRoot $Name
    $package = Join-Path $base 'package'
    $game = Join-Path $base 'game'
    $system = Join-Path $game 'system'
    New-Item -ItemType Directory -Path (Join-Path $package 'system'),(Join-Path $package 'tools'),$system -Force | Out-Null
    foreach ($tool in @('install.ps1','rollback.ps1','set-mode.ps1','set-frame-generation.ps1','settings.ps1','streamline-runtime.json','nvidia-presets.ps1','nvidia-profile.cs')) {
        Copy-Item -LiteralPath (Join-Path $root "tools\$tool") -Destination (Join-Path $package 'tools')
    }
    [IO.File]::WriteAllText((Join-Path $system 'ELEX.exe'),'fixture-elex-executable')
    [IO.File]::WriteAllText((Join-Path $system 'untouched.dat'),'keep-me')
    if ($Proxy) { [IO.File]::WriteAllText((Join-Path $system 'd3d11.dll'),$Proxy) }
    [IO.File]::WriteAllText((Join-Path $package 'system\d3d11.dll'),'fixture-dlss-proxy')
    [IO.File]::WriteAllText((Join-Path $package 'system\nvngx_dlss.dll'),'fixture-ngx-runtime')
    Copy-Item -LiteralPath (Join-Path $root 'elex-dlss.ini') -Destination (Join-Path $package 'system')
    $fgSpec=Get-Content -LiteralPath (Join-Path $root 'tools\streamline-runtime.json') -Raw | ConvertFrom-Json
    foreach($name in $fgSpec.files.PSObject.Properties.Name){[IO.File]::WriteAllText((Join-Path $package "system\$name"),"fixture-$name")}
    $config = Join-Path $base 'ConfigUser.xml'
    [IO.File]::WriteAllText($config,'<options><Engine><Render><PostProcessing AntiAliasing="Off" /></Render></Engine></options>')
    @{Base=$base;Package=$package;Game=$game;System=$system;Config=$config;Original=[IO.File]::ReadAllText($config)}
}
function Install-Fixture($Fixture, [switch]$Apply) {
    & (Join-Path $Fixture.Package 'tools\install.ps1') -Game $Fixture.Game -ConfigPath $Fixture.Config -Apply:$Apply | Out-Null
}
function Rollback-Fixture($Fixture) {
    & (Join-Path $Fixture.Package 'tools\rollback.ps1') -Apply | Out-Null
}

$resolve = Join-Path $root 'tools\resolve-version.ps1'
foreach ($pair in @(@('v0.1.1','0.1.1'),@('1.2.3','1.2.3'),@('v2.0.0-beta.1','2.0.0-beta.1'))) {
    Assert-True ((& $resolve -Version $pair[0]) -ceq $pair[1]) "Valid version $($pair[0])"
}
foreach ($bad in @('latest','v1.2','01.2.3','1.2.3/extra','1.2.3;echo bad',"1.2.3`n")) {
    Assert-Throws { & $resolve -Version $bad } "Reject version $bad"
}
Write-Output 'PASS: release tags and prerelease versions; invalid inputs rejected'

$clean = New-Fixture 'clean'
Install-Fixture $clean
Assert-True (-not (Test-Path -LiteralPath (Join-Path $clean.System 'd3d11.dll'))) 'Dry run added no proxy'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $clean.Package 'backups'))) 'Dry run wrote no backups'
Install-Fixture $clean -Apply
$manifestPath = Join-Path $clean.Package 'backups\installed.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-True ($manifest.Complete -and -not $manifest.ProxyExisted) 'Clean install manifest'
Assert-True ([IO.File]::ReadAllText($clean.Config).Contains('AntiAliasing="TSMAA"')) 'AA selection updated'
& (Join-Path $clean.Package 'tools\set-mode.ps1') -Mode DLAA | Out-Null
Assert-True ([IO.File]::ReadAllText((Join-Path $clean.System 'elex-dlss.ini')).Contains('Mode=DLAA')) 'Mode helper works'
$iniPath=Join-Path $clean.System 'elex-dlss.ini'
Assert-True ([IO.File]::ReadAllText($iniPath).Contains('Mode=Dynamic')) 'SR mode edit preserves FG mode'
& (Join-Path $clean.Package 'tools\set-mode.ps1') -Preset M | Out-Null
$presetText=[IO.File]::ReadAllText($iniPath)
Assert-True ($presetText.Contains('Preset=M') -and $presetText.Contains('Mode=DLAA') -and $presetText.Contains('Mode=Dynamic')) 'Preset-only edit preserves both modes'
& (Join-Path $clean.Package 'tools\set-mode.ps1') -Mode Quality | Out-Null
Assert-True ([IO.File]::ReadAllText($iniPath).Contains('Preset=M')) 'Quality edit preserves preset'
& (Join-Path $clean.Package 'tools\set-mode.ps1') -Mode DLAA | Out-Null
Assert-Throws { & (Join-Path $clean.Package 'tools\set-mode.ps1') -Preset G } 'Reject reserved SR preset'
& (Join-Path $clean.Package 'tools\set-frame-generation.ps1') -Mode Dynamic -TargetFPS 122.5 | Out-Null
$iniText=[IO.File]::ReadAllText($iniPath)
Assert-True ($iniText.Contains('TargetFPS=122.5') -and $iniText.Contains('Mode=DLAA')) 'FG target edit preserves SR mode'
& (Join-Path $clean.Package 'tools\set-frame-generation.ps1') -Mode Off | Out-Null
Assert-True ([IO.File]::ReadAllText($iniPath).Contains('TargetFPS=122.5')) 'FG toggle preserves target'
Assert-Throws { & (Join-Path $clean.Package 'tools\set-frame-generation.ps1') -Mode Dynamic -TargetFPS ([double]::NaN) } 'Reject invalid target'
Rollback-Fixture $clean
Assert-True (-not (Test-Path -LiteralPath (Join-Path $clean.System 'd3d11.dll'))) 'Clean rollback removes proxy'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $clean.System 'nvngx_dlss.dll'))) 'Clean rollback removes runtime'
Assert-True (@(Get-ChildItem -LiteralPath $clean.System -Filter '*.dll').Count -eq 0) 'Rollback removes every installed FG runtime'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $clean.System 'elex-dlss.ini'))) 'Clean rollback removes INI'
Assert-True ([IO.File]::ReadAllText($clean.Config) -ceq $clean.Original) 'Original config restored'
Assert-True ([IO.File]::ReadAllText((Join-Path $clean.System 'untouched.dat')) -ceq 'keep-me') 'Unrelated file preserved'
Write-Output 'PASS: clean installation, dry run, mode change and complete rollback'

$defaults = New-Fixture 'default-settings'
[IO.File]::WriteAllText($defaults.Config,'<options><Engine /></options>')
Install-Fixture $defaults -Apply
$defaultXml = [xml][IO.File]::ReadAllText($defaults.Config)
Assert-True ($defaultXml.options.Engine.Render.PostProcessing.AntiAliasing -eq 'TSMAA') 'Missing default AA setting added'
Rollback-Fixture $defaults
Assert-True ([IO.File]::ReadAllText($defaults.Config) -ceq '<options><Engine /></options>') 'Default-only config restored exactly'
Write-Output 'PASS: fresh configuration with omitted AA settings'

$existing = New-Fixture 'existing-cloud' 'fixture-cloud-fix'
Install-Fixture $existing -Apply
$legacyPath = Join-Path $existing.Package 'backups\installed.json'
$legacy = Get-Content -LiteralPath $legacyPath -Raw | ConvertFrom-Json
$legacy.PSObject.Properties.Remove('ProxyExisted')
$legacy | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $legacyPath -Encoding UTF8
Rollback-Fixture $existing
Assert-True ([IO.File]::ReadAllText((Join-Path $existing.System 'd3d11.dll')) -ceq 'fixture-cloud-fix') 'Prior graphics fix restored'
Write-Output 'PASS: supported existing graphics fix restored, including legacy manifests'

$conflict = New-Fixture 'unknown-proxy' 'unrelated-wrapper'
Assert-Throws { Install-Fixture $conflict -Apply } 'Unknown wrapper rejected'
Assert-True ([IO.File]::ReadAllText((Join-Path $conflict.System 'd3d11.dll')) -ceq 'unrelated-wrapper') 'Unknown wrapper untouched'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $conflict.Package 'backups'))) 'Conflict rejected before mutation'

$failed = New-Fixture 'copy-failure'
$testState.FailCopyTo = Join-Path $failed.System 'nvngx_dlss.dll'
Assert-Throws { Install-Fixture $failed -Apply } 'Runtime-copy failure propagated'
$testState.FailCopyTo = ''
Assert-True (-not (Test-Path -LiteralPath (Join-Path $failed.System 'd3d11.dll'))) 'Failed clean install undid proxy copy'
Assert-True ([IO.File]::ReadAllText($failed.Config) -ceq $failed.Original) 'Failed install preserved config'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $failed.Package 'backups\installed.json'))) 'Failed install can be retried'
Write-Output 'PASS: conflicting wrappers and partial installation failures handled'

$fgFailed = New-Fixture 'fg-copy-failure'
$testState.FailCopyTo = Join-Path $fgFailed.System 'sl.reflex.dll'
Assert-Throws { Install-Fixture $fgFailed -Apply } 'Later FG-runtime failure propagated'
$testState.FailCopyTo = ''
Assert-True (@(Get-ChildItem -LiteralPath $fgFailed.System -Filter '*.dll').Count -eq 0) 'Failed FG install removes all copied DLLs'
Assert-True ([IO.File]::ReadAllText($fgFailed.Config) -ceq $fgFailed.Original) 'Failed FG install restores config'
Write-Output 'PASS: native Dynamic settings persist independently; FG install failure rolls back all runtime files'

$changed = New-Fixture 'independent-config-edit'
Install-Fixture $changed -Apply
[IO.File]::AppendAllText($changed.Config,'<!-- independent user edit -->')
$modified = [IO.File]::ReadAllText($changed.Config)
Rollback-Fixture $changed
Assert-True ([IO.File]::ReadAllText($changed.Config) -ceq $modified) 'Independent configuration edit preserved'

$tampered = New-Fixture 'changed-runtime'
Install-Fixture $tampered -Apply
[IO.File]::AppendAllText((Join-Path $tampered.System 'nvngx_dlss.dll'),' changed')
Assert-Throws { Rollback-Fixture $tampered } 'Rollback rejects modified installed DLL'
Assert-True (Test-Path -LiteralPath (Join-Path $tampered.System 'd3d11.dll')) 'Rollback stopped before changing proxy'
Write-Output 'PASS: later user edits protected and modified binaries rejected before rollback'

$retained = New-Fixture 'retained-runtime'
Install-Fixture $retained -Apply
$retainedManifest=Join-Path $retained.Package 'backups\installed.json'
$retainedData=Get-Content -LiteralPath $retainedManifest -Raw | ConvertFrom-Json
$retainedEntry=$retainedData.Files | Where-Object {[IO.Path]::GetFileName($_.Target) -eq 'nvngx_dlssg.dll'}
$retainedEntry | Add-Member -NotePropertyName RetainOnRollback -NotePropertyValue $true
$retainedData | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $retainedManifest -Encoding UTF8
Rollback-Fixture $retained
Assert-True (Test-Path -LiteralPath (Join-Path $retained.System 'nvngx_dlssg.dll')) 'Pre-existing runtime retained on rollback'
Assert-True (@(Get-ChildItem -LiteralPath $retained.System -Filter '*.dll').Count -eq 1) 'Only the retained runtime remains'
Write-Output 'PASS: explicitly retained pre-existing runtime survives rollback'

$presetInstall=New-Fixture 'sr-preset-install'
& (Join-Path $presetInstall.Package 'tools\install.ps1') -Game $presetInstall.Game -ConfigPath $presetInstall.Config -Preset K -Apply | Out-Null
Assert-True ([IO.File]::ReadAllText((Join-Path $presetInstall.System 'elex-dlss.ini')).Contains('Preset=K')) 'Installer applies requested SR preset'
Rollback-Fixture $presetInstall
Write-Output 'PASS: installer accepts independent SR model preset'
