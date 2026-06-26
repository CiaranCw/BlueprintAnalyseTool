<#
.SYNOPSIS
    全工程批量解析。
#>
param(
    [string] $UnrealEditorCmd = $env:UNREAL_EDITOR_CMD,
    [Parameter(Mandatory = $true)] [string] $Project,
    [Parameter(Mandatory = $true)] [string] $OutputDir,
    [string[]] $ClassFilter = @("Blueprint", "WidgetBlueprint", "AnimBlueprint"),
    [string] $Overwrite = "skip"
)

$ErrorActionPreference = "Stop"

if (-not $UnrealEditorCmd -or -not (Test-Path $UnrealEditorCmd)) {
    Write-Error "UnrealEditor-Cmd not found."
    exit 30
}
if (-not (Test-Path $Project)) {
    Write-Error "Project not found."
    exit 30
}

$projectDir = Split-Path -Parent (Resolve-Path $Project)
$outDirAbs  = (Resolve-Path -LiteralPath (New-Item -ItemType Directory -Force -Path $OutputDir)).Path
if ($outDirAbs.StartsWith($projectDir, [StringComparison]::OrdinalIgnoreCase)) {
    Write-Error "OutputDir must be outside ProjectPath."
    exit 30
}

$cmdArgs = @(
    $Project,
    "-run=BPATDump",
    "-ProjectScan=1",
    "-ClassFilter=$($ClassFilter -join ',')",
    "-OutputDir=$outDirAbs",
    "-OverwritePolicy=$Overwrite",
    "-StrictReadOnly=1"
)

Write-Host "Running: $UnrealEditorCmd $($cmdArgs -join ' ')"
& $UnrealEditorCmd @cmdArgs
exit $LASTEXITCODE
