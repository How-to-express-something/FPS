[CmdletBinding()]
param(
    [ValidateSet('Menu', 'Test')][string]$Mode = 'Menu',
    [ValidateSet('Host', 'Client')][string]$Role = 'Host',
    [string]$EngineRoot,
    [switch]$Diagnostics,
    [ValidateRange(1024, 65535)][int]$Port = 7787,
    [ValidatePattern('^[A-Za-z0-9_-]{1,64}$')][string]$RunName = ('demo-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$TestMap = '/Game/FPS/Maps/Test',
    [ValidateRange(640, 7680)][int]$Width = 1280,
    [ValidateRange(360, 4320)][int]$Height = 720
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Resolve-Engine.ps1')
$captureProjectRoot = Split-Path -Parent $PSScriptRoot
$captureProject = Join-Path $captureProjectRoot 'FPS.uproject'
$captureEngine = Resolve-FPSEngineRoot -ProjectPath $captureProject -EngineRoot $EngineRoot
$captureExe = Join-Path $captureEngine 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $captureExe -PathType Leaf)) { throw "Missing $captureExe" }
if ($TestMap -notmatch '^/Game/[A-Za-z0-9_/-]+$') { throw 'TestMap must be a /Game package path without URL options.' }

$captureDirectory = Join-Path $captureProjectRoot "Saved\DemoCaptures\$RunName"
$captureLogs = Join-Path $captureDirectory 'logs'
New-Item -ItemType Directory -Path $captureLogs -Force | Out-Null
$captureStem = "$Mode-$Role"
$captureLog = Join-Path $captureLogs "$captureStem.log"
$captureManifest = Join-Path $captureDirectory "$captureStem-launch.json"
if ((Test-Path -LiteralPath $captureLog) -or (Test-Path -LiteralPath $captureManifest)) {
    throw "This take already has $captureStem output. Choose a new -RunName to retain previous evidence."
}

$captureURL = if ($Mode -eq 'Menu') { '/Game/FPS/Maps/MainMenu_Map' }
    elseif ($Role -eq 'Host') { "$TestMap`?listen" }
    else { "127.0.0.1:$Port" }
$captureExecCommands = 't.MaxFPS 60,t.IdleWhenNotForeground 0,fps.Net.Trace 1,fps.Net.Phase 1'
if ($Diagnostics) {
    $captureExecCommands += ',fps.Net.Panel 1'
    if ($Mode -eq 'Test') {
        $captureExecCommands += if ($Role -eq 'Host') { ',fps.Net.DrawShots 1' } else { ',fps.Net.DrawPredicted 1' }
    }
}
$captureArguments = @(
    ('"' + $captureProject + '"'), $captureURL, '-game', '-windowed', '-nosplash', '-nop4',
    "-ResX=$Width", "-ResY=$Height", ('-abslog="' + $captureLog + '"'),
    ('-ExecCmds="' + $captureExecCommands + '"')
)
if ($Role -eq 'Host') { $captureArguments += "-port=$Port" }

# Interactive windows are intentional: the user records and controls these games.
# No unattended, NullRHI, automatic smoke driver, or process termination is used.
$captureProcess = Start-Process -FilePath $captureExe -ArgumentList $captureArguments -WindowStyle Normal -PassThru
[ordered]@{
    mode = $Mode; intended_role = $Role; pid = $captureProcess.Id
    diagnostics = [bool]$Diagnostics
    started_at = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('o')
    project = $captureProject; executable = $captureExe; url = $captureURL; port = $Port
    log = $captureLog; arguments = $captureArguments
    note = 'Menu mode starts two menu instances; actual network roles begin after manual Create/Join. Test client should launch only after host is listening.'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $captureManifest -Encoding UTF8
Write-Host "Started $Mode / $Role (PID $($captureProcess.Id))."
Write-Host "Log: $captureLog"
Write-Host 'Background idle disabled (t.IdleWhenNotForeground=0); both windows retain the same 60 FPS cap.'
if ($Mode -eq 'Test' -and $Role -eq 'Host') { Write-Host 'Wait until the host map is ready, then run this script with -Role Client and the same RunName/Port.' }
if ($Mode -eq 'Menu') { Write-Host 'Use the menus manually. This script does not create or join an online session.' }
