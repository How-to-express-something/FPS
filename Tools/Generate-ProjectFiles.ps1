[CmdletBinding()]
param([string]$EngineRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Resolve-Engine.ps1')
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $taskProjectRoot 'FPS.uproject'
$resolvedEngine = Resolve-FPSEngineRoot -ProjectPath $projectPath -EngineRoot $EngineRoot
$generator = Join-Path $resolvedEngine 'Engine\Build\BatchFiles\GenerateProjectFiles.bat'
if (-not (Test-Path -LiteralPath $generator -PathType Leaf)) { throw "Missing $generator" }
& $generator "-project=$projectPath" '-game' '-engine'
exit $LASTEXITCODE
