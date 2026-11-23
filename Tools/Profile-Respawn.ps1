[CmdletBinding()]
param(
    [string]$EngineRoot,
    [ValidateSet('Host','Client')][string]$Role = 'Host',
    [switch]$Memory,
    [switch]$Launch
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Resolve-Engine.ps1')
$profileProjectRoot = Split-Path -Parent $PSScriptRoot
$profileProject = Join-Path $profileProjectRoot 'FPS.uproject'
$profileEngine = Resolve-FPSEngineRoot -ProjectPath $profileProject -EngineRoot $EngineRoot
$profileExe = Join-Path $profileEngine 'Engine\Binaries\Win64\UnrealEditor.exe'
$profileStamp = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('yyyyMMdd-HHmmss-fff')
$profileOutput = Join-Path $profileProjectRoot "Saved\Profiling\Respawn\$profileStamp-$Role"
$profileChannels = 'default,loadtime,file'
if ($Memory) { $profileChannels += ',memory' }
$profileArgs = @($profileProject, '/Game/FPS/Maps/MainMenu_Map', '-game', '-windowed', '-ResX=1280', '-ResY=720',
    '-NoSplash', '-DisablePlugins=Fab', '-statnamedevents', "-trace=$profileChannels",
    "-tracefile=$profileOutput\capture.utrace", "-abslog=$profileOutput\game.log")
$profilePlan = [ordered]@{
    mode = $(if ($Launch) { 'Launch' } else { 'PreviewOnly' })
    role_label = $Role
    note = 'Role is a file label only. Use the menu to Create/Find/Join; no session or gameplay is automated.'
    executable = $profileExe
    arguments = $profileArgs
    output = $profileOutput
    limitations = 'Uncooked editor-game, real RHI. Not packaged performance evidence. Stop tracing with Trace.Stop or exit normally.'
}
$profilePlan | ConvertTo-Json -Depth 4 | Write-Output
if (-not $Launch) { return }
if (-not (Test-Path -LiteralPath $profileExe -PathType Leaf)) { throw "Missing $profileExe" }
New-Item -ItemType Directory -Path $profileOutput -Force | Out-Null
$profilePlan | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $profileOutput 'capture-plan.json') -Encoding UTF8
# Intentional interactive game process. Never terminate the user's editor.
& $profileExe @profileArgs
exit $LASTEXITCODE
