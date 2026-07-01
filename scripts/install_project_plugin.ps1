<#
  install_project_plugin.ps1 - copy the read-only analysis plugin (source form) into a target
  UE project's Plugins/ folder. Source-form distribution so it compiles against the target's
  (possibly custom/source) engine. Non-destructive to existing assets; only adds a plugin folder.

  Usage:
    .\install_project_plugin.ps1 -ProjectUProject "<...>.uproject" [-PluginSource "<repo>\...\BPParserTestGen"]

  To remove later: delete <project>/Plugins/<PluginName> and regenerate project files.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $PluginSource = ""
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 30 }
if ([string]::IsNullOrWhiteSpace($PluginSource)) {
  $PluginSource = Join-Path (Split-Path $PSScriptRoot -Parent) 'bpparser_testgen\Plugins\BPParserTestGen'
}
if (-not (Test-Path $PluginSource)) { Write-Error "Plugin source not found: $PluginSource"; exit 30 }

$leaf = Split-Path $PluginSource -Leaf
$projDir = Split-Path $ProjectUProject -Parent
$dest = Join-Path $projDir "Plugins\$leaf"
Write-Host "Installing plugin '$leaf' -> $dest" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
# copy source only (exclude any local build artifacts)
robocopy $PluginSource $dest /E /XD Binaries Intermediate /NFL /NDL /NJH /NJS /NP | Out-Null
if (Test-Path (Join-Path $dest "$leaf.uplugin")) { Write-Host "Installed." -ForegroundColor Green; exit 0 }
Write-Error "Install appears to have failed (no .uplugin at destination)."; exit 20
