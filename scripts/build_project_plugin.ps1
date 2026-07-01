<#
  build_project_plugin.ps1 - incrementally build a target project's Editor target with a given
  UE (works for Epic launcher builds AND custom/source engines). Used to compile the read-only
  analysis plugin into a target project. Non-destructive to blueprint assets.

  Usage:
    .\build_project_plugin.ps1 -UERoot "<engine root>" -ProjectUProject "<...>.uproject"

  Exit codes: 0 build OK, 20 build failed, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $UERoot,
  [Parameter(Mandatory=$true)] [string] $ProjectUProject
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 30 }
$build = Join-Path $UERoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $build)) { Write-Error "Build.bat not found under UERoot: $build"; exit 30 }

$projName = [IO.Path]::GetFileNameWithoutExtension($ProjectUProject)
$target = "${projName}Editor"
Write-Host "Building $target (Win64 Development) with $UERoot ..." -ForegroundColor Cyan
& $build $target Win64 Development -Project="$ProjectUProject" -WaitMutex -FromMsBuild
$code = $LASTEXITCODE
if ($code -ne 0) { Write-Error "Build FAILED (exit $code)."; exit 20 }
Write-Host "Build OK." -ForegroundColor Green
exit 0
