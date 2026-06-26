<#
  ensure_code_project.ps1 — make a Blueprint-only UE project buildable for C++
  plugins, generically (derives the module name from the .uproject; no hardcoding).

  - Creates a minimal primary game module + Target.cs files if Source/ has none.
  - Ensures the .uproject declares that module and enables the requested plugins.
  - Idempotent; backs up the .uproject once (.bak) before first modification.
  - Never deletes or overwrites existing Source files / Content.

  Usage:
    .\ensure_code_project.ps1 -ProjectUProject "<...>.uproject" [-PluginNames BPParserTestGen,BlueprintAgentTools]

  Exit codes: 0 ok, 30 bad args.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string[]] $PluginNames = @('BPParserTestGen')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 30 }

$ProjectDir = Split-Path $ProjectUProject -Parent
$Mod = [IO.Path]::GetFileNameWithoutExtension($ProjectUProject)
$SourceDir = Join-Path $ProjectDir 'Source'
$ModDir = Join-Path $SourceDir $Mod

$hasTarget = @(Get-ChildItem (Join-Path $SourceDir '*.Target.cs') -ErrorAction SilentlyContinue).Count -gt 0
if (-not $hasTarget) {
  Write-Host "Converting '$Mod' to a code project (adding minimal module)..." -ForegroundColor Cyan
  New-Item -ItemType Directory -Force -Path $ModDir | Out-Null

@"
using UnrealBuildTool;
using System.Collections.Generic;
public class ${Mod}Target : TargetRules
{
    public ${Mod}Target(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("$Mod");
    }
}
"@ | Out-File (Join-Path $SourceDir "$Mod.Target.cs") -Encoding utf8

@"
using UnrealBuildTool;
using System.Collections.Generic;
public class ${Mod}EditorTarget : TargetRules
{
    public ${Mod}EditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("$Mod");
    }
}
"@ | Out-File (Join-Path $SourceDir "${Mod}Editor.Target.cs") -Encoding utf8

@"
using UnrealBuildTool;
public class $Mod : ModuleRules
{
    public $Mod(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
    }
}
"@ | Out-File (Join-Path $ModDir "$Mod.Build.cs") -Encoding utf8

  "#pragma once`r`n#include `"CoreMinimal.h`"`r`n" | Out-File (Join-Path $ModDir "$Mod.h") -Encoding utf8
  "#include `"$Mod.h`"`r`n#include `"Modules/ModuleManager.h`"`r`nIMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $Mod, `"$Mod`");`r`n" | Out-File (Join-Path $ModDir "$Mod.cpp") -Encoding utf8
} else {
  Write-Host "Already a code project (Source/*.Target.cs present) - leaving Source untouched." -ForegroundColor Green
}

# --- ensure .uproject declares the module + enables plugins (idempotent) ---
$bak = "$ProjectUProject.bak"
if (-not (Test-Path $bak)) { Copy-Item $ProjectUProject $bak -Force }

$proj = Get-Content $ProjectUProject -Raw | ConvertFrom-Json
$changed = $false

$modules = @(); if ($proj.PSObject.Properties.Name -contains 'Modules' -and $proj.Modules) { $modules = @($proj.Modules) }
if (-not ($modules | Where-Object { $_.Name -eq $Mod })) {
  $modules += [pscustomobject]@{ Name=$Mod; Type='Runtime'; LoadingPhase='Default' }
  $changed = $true
}
$proj | Add-Member -NotePropertyName Modules -NotePropertyValue $modules -Force

$plugins = @(); if ($proj.PSObject.Properties.Name -contains 'Plugins' -and $proj.Plugins) { $plugins = @($proj.Plugins) }
foreach ($pn in $PluginNames) {
  if (-not ($plugins | Where-Object { $_.Name -eq $pn })) {
    $plugins += [pscustomobject]@{ Name=$pn; Enabled=$true }
    $changed = $true
  }
}
$proj | Add-Member -NotePropertyName Plugins -NotePropertyValue $plugins -Force

if ($changed -or -not $hasTarget) {
  ($proj | ConvertTo-Json -Depth 10) | Out-File $ProjectUProject -Encoding utf8
  Write-Host ".uproject updated (module + plugins). Backup: $bak" -ForegroundColor Yellow
} else {
  Write-Host ".uproject already declares module + plugins (no change)." -ForegroundColor Green
}
exit 0
