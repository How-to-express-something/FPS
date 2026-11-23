Set-StrictMode -Version Latest

function Resolve-FPSEngineRoot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [string]$EngineRoot
    )

    $projectFile = Get-Item -LiteralPath $ProjectPath -ErrorAction Stop
    $projectData = Get-Content -LiteralPath $projectFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $candidate = $EngineRoot
    if (-not $candidate -and $env:FPS_UE_ROOT) { $candidate = $env:FPS_UE_ROOT }
    if (-not $candidate) {
        $builds = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
        if ($builds) {
            $entry = $builds.PSObject.Properties[$projectData.EngineAssociation]
            if ($entry) { $candidate = [string]$entry.Value }
        }
    }
    if (-not $candidate) {
        throw 'Cannot resolve the source engine. Pass -EngineRoot or set FPS_UE_ROOT; otherwise register the source engine and associate FPS.uproject.'
    }
    $resolvedEngine = (Resolve-Path -LiteralPath $candidate -ErrorAction Stop).Path
    $versionFile = Join-Path $resolvedEngine 'Engine\Build\Build.version'
    $version = Get-Content -LiteralPath $versionFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 5 -or $version.PatchVersion -ne 4) {
        throw "Expected UE 5.5.4; resolved $($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion) at $resolvedEngine."
    }
    $buildScript = Join-Path $resolvedEngine 'Engine\Build\BatchFiles\Build.bat'
    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) { throw "Missing $buildScript" }
    return $resolvedEngine
}
