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
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $PluginName = "BPParserTestGen"   # if set, assert this plugin's Editor module DLL was produced
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

# Assert the plugin module actually compiled. A "Build OK" with the plugin disabled produces NO module
# DLL -> that is a false success. Fail loudly so warmup does not report ready when it isn't.
if ($PluginName) {
  $projDir = Split-Path $ProjectUProject -Parent
  $dll = Join-Path $projDir "Plugins\$PluginName\Binaries\Win64\UnrealEditor-$PluginName.dll"
  if (-not (Test-Path $dll)) {
    Write-Error "Build reported OK but plugin module DLL is missing: $dll`nThe plugin is likely NOT enabled in the .uproject (run install_project_plugin.ps1, which enables it), or EnabledByDefault is false."
    exit 20
  }
  Write-Host "Build OK. Verified module DLL: $dll" -ForegroundColor Green
} else {
  Write-Host "Build OK." -ForegroundColor Green
}
exit 0
