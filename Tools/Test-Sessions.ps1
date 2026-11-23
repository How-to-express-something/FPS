[CmdletBinding()]
param([string]$EngineRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Resolve-Engine.ps1')
$sessionProjectRoot = Split-Path -Parent $PSScriptRoot
$sessionProject = Join-Path $sessionProjectRoot 'FPS.uproject'
$sessionEngine = Resolve-FPSEngineRoot -ProjectPath $sessionProject -EngineRoot $EngineRoot
$sessionExe = Join-Path $sessionEngine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$sessionStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$sessionLogDir = Join-Path $sessionProjectRoot "Saved\SessionTests\$sessionStamp"
New-Item -ItemType Directory -Path $sessionLogDir -Force | Out-Null
$sessionRoom = "FPS_SMOKE_$sessionStamp"
$sessionCommon = @(('"' + $sessionProject + '"'), '/Game/FPS/Maps/MainMenu_Map', '-game', '-unattended', '-nop4', '-nosplash', '-NullRHI', '-nosound', '-DisablePlugins=Fab', '-NoLoadingScreen', '-LogCmds="LogOnlineSession VeryVerbose"', "-FPSSmokeRoom=$sessionRoom")
$sessionProcesses = @()
try {
    $sessionHostLog = Join-Path $sessionLogDir 'host.log'
    $sessionClientLog = Join-Path $sessionLogDir 'client.log'
    $sessionProcesses += Start-Process -FilePath $sessionExe -WindowStyle Hidden -PassThru -ArgumentList ($sessionCommon + @('-FPSSessionSmoke=Host', '-port=7787', ('-abslog="' + $sessionHostLog + '"')))
    Start-Sleep -Seconds 3
    $sessionProcesses += Start-Process -FilePath $sessionExe -WindowStyle Hidden -PassThru -ArgumentList ($sessionCommon + @('-FPSSessionSmoke=Client', ('-abslog="' + $sessionClientLog + '"')))
    $sessionDeadline = [DateTime]::UtcNow.AddSeconds(150)
    while (($sessionProcesses | Where-Object { -not $_.HasExited }) -and [DateTime]::UtcNow -lt $sessionDeadline) { Start-Sleep -Milliseconds 500 }
    $sessionRows = foreach ($sessionProc in $sessionProcesses) {
        [ordered]@{ pid = $sessionProc.Id; exited = $sessionProc.HasExited; exit_code = $(if ($sessionProc.HasExited) { $sessionProc.ExitCode } else { $null }) }
    }
    $sessionPassed = (Test-Path -LiteralPath $sessionHostLog) -and (Test-Path -LiteralPath $sessionClientLog)
    foreach ($sessionLog in @($sessionHostLog, $sessionClientLog)) {
        if (-not (Test-Path -LiteralPath $sessionLog) -or -not (Select-String -LiteralPath $sessionLog -SimpleMatch '[FPSSmoke] PASS' -Quiet)) { $sessionPassed = $false }
        if ((Test-Path -LiteralPath $sessionLog) -and (Select-String -LiteralPath $sessionLog -Pattern 'Fatal error:|Assertion failed:|\[FPSSmoke\] FAIL' -Quiet)) { $sessionPassed = $false }
    }
    if ($sessionProcesses | Where-Object { -not $_.HasExited -or $_.ExitCode -ne 0 }) { $sessionPassed = $false }
    [ordered]@{ succeeded = $sessionPassed; room = $sessionRoom; topology = 'Two independent uncooked editor-game processes, Null LAN listen server'; processes = @($sessionRows); host_log = $sessionHostLog; client_log = $sessionClientLog } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $sessionLogDir 'result.json') -Encoding UTF8
    Write-Host "Session smoke result: $sessionLogDir"
    if (-not $sessionPassed) { throw 'Session smoke failed; inspect host.log and client.log.' }
}
finally {
    # Only processes launched by THIS invocation; never terminate the user editor.
    foreach ($sessionProc in $sessionProcesses) { if (-not $sessionProc.HasExited) { Stop-Process -Id $sessionProc.Id -Force } }
}
