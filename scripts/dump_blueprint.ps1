<#
.SYNOPSIS
    单蓝图解析脚本。
.PARAMETER UnrealEditorCmd
    UnrealEditor-Cmd.exe 绝对路径。可由环境变量 UNREAL_EDITOR_CMD 覆盖。
.PARAMETER Project
    .uproject 绝对路径。
.PARAMETER AssetPath
    /Game/... 形式的蓝图资产路径。
.PARAMETER OutputDir
    输出目录。必须在 Project 之外。
.PARAMETER Overwrite
    skip | overwrite | fail。默认 skip。
.PARAMETER DryRun
    1 = 只跑解析不落盘。
#>
param(
    [string] $UnrealEditorCmd = $env:UNREAL_EDITOR_CMD,
    [Parameter(Mandatory = $true)] [string] $Project,
    [Parameter(Mandatory = $true)] [string] $AssetPath,
    [Parameter(Mandatory = $true)] [string] $OutputDir,
    [string] $Overwrite = "skip",
    [int]    $DryRun = 0
)

$ErrorActionPreference = "Stop"

if (-not $UnrealEditorCmd -or -not (Test-Path $UnrealEditorCmd)) {
    Write-Error "UnrealEditor-Cmd not found. Set -UnrealEditorCmd or UNREAL_EDITOR_CMD env var."
    exit 30
}
if (-not (Test-Path $Project)) {
    Write-Error "Project not found: $Project"
    exit 30
}

$projectDir = Split-Path -Parent (Resolve-Path $Project)
$outDirAbs  = (Resolve-Path -LiteralPath (New-Item -ItemType Directory -Force -Path $OutputDir)).Path
if ($outDirAbs.StartsWith($projectDir, [StringComparison]::OrdinalIgnoreCase)) {
    Write-Error "OutputDir must be outside ProjectPath. OutputDir=$outDirAbs Project=$projectDir"
    exit 30
}

$cmdArgs = @(
    $Project,
    "-run=BPATDump",
    "-AssetPath=$AssetPath",
    "-OutputDir=$outDirAbs",
    "-OverwritePolicy=$Overwrite",
    "-DryRun=$DryRun",
    "-StrictReadOnly=1"
)

Write-Host "Running: $UnrealEditorCmd $($cmdArgs -join ' ')"
& $UnrealEditorCmd @cmdArgs
exit $LASTEXITCODE
