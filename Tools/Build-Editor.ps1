[CmdletBinding()]
param(
    [string]$EngineRoot,
    [ValidateSet('Development', 'DebugGame')][string]$Configuration = 'Development'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Resolve-Engine.ps1')
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $taskProjectRoot 'FPS.uproject'
$resolvedEngine = Resolve-FPSEngineRoot -ProjectPath $projectPath -EngineRoot $EngineRoot
$taskProjectToken = [IO.Path]::GetFileName($projectPath)
$taskBlockingEditors = @(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" | Where-Object {
    # Another project's editor does not hold this module. If its command line
    # is unreadable / has no explicit project, retain the conservative guard.
    -not $_.CommandLine -or $_.CommandLine -notmatch '\.uproject' -or
        $_.CommandLine.IndexOf($taskProjectToken, [StringComparison]::OrdinalIgnoreCase) -ge 0
})
if ($taskBlockingEditors.Count -gt 0) {
    throw 'Close the FPS Unreal Editor before a normal build (or an editor whose project could not be identified). This script does not terminate editors or use Live Coding.'
}
$logs = Join-Path $taskProjectRoot 'Saved\BuildLogs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$stamp = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('yyyyMMdd-HHmmss-fff')
$logPath = Join-Path $logs "FPSEditor-$Configuration-$stamp.log"
$resultPath = Join-Path $logs "FPSEditor-$Configuration-$stamp.result.json"
$transcriptPath = Join-Path $logs "FPSEditor-$Configuration-$stamp.console.txt"
$started = [DateTimeOffset]::UtcNow
$exitCode = -1
$transcriptStarted = $false
try {
    Start-Transcript -LiteralPath $transcriptPath | Out-Null
    $transcriptStarted = $true
    Write-Host "Engine: $resolvedEngine (UE 5.5.4)"
    Write-Host "Target: FPSEditor / Win64 / $Configuration"
    Write-Host "UBT log: $logPath"
    & (Join-Path $resolvedEngine 'Engine\Build\BatchFiles\Build.bat') 'FPSEditor' 'Win64' $Configuration "-Project=$projectPath" '-WaitMutex' '-NoHotReloadFromIDE' "-Log=$logPath"
    $exitCode = $LASTEXITCODE
}
finally {
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
    $finished = [DateTimeOffset]::UtcNow
    $result = [ordered]@{
        target = 'FPSEditor'; platform = 'Win64'; configuration = $Configuration
        engine_version = '5.5.4'; engine_root = $resolvedEngine; project = $projectPath
        arguments = @('FPSEditor', 'Win64', $Configuration, "-Project=$projectPath", '-WaitMutex', '-NoHotReloadFromIDE', "-Log=$logPath")
        started_at = $started.ToOffset([TimeSpan]::FromHours(8)).ToString('o')
        finished_at = $finished.ToOffset([TimeSpan]::FromHours(8)).ToString('o')
        elapsed_seconds = [Math]::Round(($finished - $started).TotalSeconds, 3)
        exit_code = $exitCode; succeeded = ($exitCode -eq 0)
        ubt_log = $logPath; console_log = $transcriptPath
    }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Host "Build result: $resultPath"
}
exit $exitCode
