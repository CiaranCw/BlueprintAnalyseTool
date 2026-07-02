<#
  install_project_plugin.ps1 - copy the read-only analysis plugin (source form) into a target
  UE project's Plugins/ folder AND enable it in the .uproject. Source-form distribution so it
  compiles against the target's (possibly custom/source) engine. Non-destructive to blueprint assets;
  it does modify the .uproject (adds/enables the plugin entry) after backing it up to .uproject.bak.

  Usage:
    .\install_project_plugin.ps1 -ProjectUProject "<...>.uproject" [-PluginSource "<...>\BPParserTestGen"]

  Default -PluginSource resolves to whichever exists (single source of truth):
    <repo>\plugin\BPParserTestGen           (distributed / Tools\BlueprintAgent layout)
    <repo>\bpparser_testgen\Plugins\BPParserTestGen  (dev repo layout)

  To remove later: delete <project>/Plugins/<PluginName>, remove its .uproject Plugins entry, regen files.
  Exit codes: 0 ok, 20 install failed, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $PluginSource = ""
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 30 }

# --- resolve default plugin source across layouts (distributed first, then dev repo) ---
if ([string]::IsNullOrWhiteSpace($PluginSource)) {
  $repo = Split-Path $PSScriptRoot -Parent
  foreach ($cand in @((Join-Path $repo 'plugin\BPParserTestGen'), (Join-Path $repo 'bpparser_testgen\Plugins\BPParserTestGen'))) {
    if (Test-Path (Join-Path $cand 'BPParserTestGen.uplugin')) { $PluginSource = $cand; break }
  }
}
if ([string]::IsNullOrWhiteSpace($PluginSource) -or -not (Test-Path (Join-Path $PluginSource '*.uplugin'))) {
  Write-Error "Plugin source not found/invalid: '$PluginSource'. Pass -PluginSource explicitly."; exit 30
}

$leaf = Split-Path $PluginSource -Leaf
$projDir = Split-Path $ProjectUProject -Parent
$dest = Join-Path $projDir "Plugins\$leaf"
Write-Host "Installing plugin '$leaf' -> $dest" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
robocopy $PluginSource $dest /E /XD Binaries Intermediate /NFL /NDL /NJH /NJS /NP | Out-Null
if (-not (Test-Path (Join-Path $dest "$leaf.uplugin"))) { Write-Error "Copy failed (no .uplugin at destination)."; exit 20 }

# --- enable the plugin in the .uproject (idempotent; the decisive fix: a copied-but-not-enabled
#     project plugin is NOT compiled, so no module DLL is produced) ---
try {
  $j = Get-Content $ProjectUProject -Raw | ConvertFrom-Json
  $plugins = @(); if ($j.PSObject.Properties.Name -contains 'Plugins' -and $j.Plugins) { $plugins = @($j.Plugins) }
  $entry = $plugins | Where-Object { $_.Name -eq $leaf } | Select-Object -First 1
  $changed = $false
  if ($entry) { if (-not $entry.Enabled) { $entry.Enabled = $true; $changed = $true } }
  else {
    $new = [pscustomobject]@{ Name = $leaf; Enabled = $true; TargetAllowList = @('Editor') }
    $plugins = $plugins + $new
    if ($j.PSObject.Properties.Name -contains 'Plugins') { $j.Plugins = $plugins } else { $j | Add-Member -NotePropertyName Plugins -NotePropertyValue $plugins -Force }
    $changed = $true
  }
  if ($changed) {
    Copy-Item $ProjectUProject "$ProjectUProject.bak" -Force
    ($j | ConvertTo-Json -Depth 40) | Set-Content -Encoding UTF8 $ProjectUProject
    Write-Host "Enabled '$leaf' in .uproject (backup: $ProjectUProject.bak)" -ForegroundColor Green
  } else {
    Write-Host "'$leaf' already enabled in .uproject." -ForegroundColor Green
  }
} catch {
  Write-Warning "Could not update .uproject Plugins ($($_.Exception.Message)). The plugin now sets EnabledByDefault=true, so it should still compile; verify the module DLL after build."
}

Write-Host "Installed." -ForegroundColor Green
exit 0
