<#
  cleanup_test_assets.ps1 - remove ONLY the agent's own generated smoke/test Widget Blueprints.

  Scope is deliberately narrow and safe: it matches ONLY /Game/Generated/WBP_Agent_*  (i.e. files named
  WBP_Agent_*.uasset directly under <Project>/Content/Generated). It NEVER touches user assets, other
  /Game paths, or anything outside Content/Generated. Dry-run by default: pass -Execute to actually delete.

  Usage:
    .\cleanup_test_assets.ps1 -ProjectUProject "<...>.uproject" [-Prefix WBP_Agent_] [-Execute]

  Exit codes: 0 ok (listed or deleted), 30 bad input. The UE editor should be closed before -Execute.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $Prefix = "WBP_Agent_",   # only assets named /Game/Generated/<Prefix>*
  [switch] $Execute                  # without this, dry-run (list only)
)
$ErrorActionPreference='Stop'
function Fail($m,$c){ Write-Error $m; exit $c }
if (-not (Test-Path $ProjectUProject)) { Fail "Project not found: $ProjectUProject" 30 }
# Guard the scope: refuse anything that could widen the match beyond the agent's own test assets.
if ([string]::IsNullOrWhiteSpace($Prefix) -or -not ($Prefix -match '^WBP_Agent_')) {
  Fail "Refusing: -Prefix must start with 'WBP_Agent_' (this script only cleans the agent's own /Game/Generated test assets)." 30
}
$ProjectDir = Split-Path $ProjectUProject -Parent
$GenDir = Join-Path $ProjectDir 'Content\Generated'
if (-not (Test-Path $GenDir)) { Write-Host "No Content\Generated dir; nothing to clean." -ForegroundColor Green; exit 0 }

if (Get-Process -Name UnrealEditor -EA SilentlyContinue) {
  Write-Host "[warn] UE editor appears to be running; close it before -Execute to avoid file locks." -ForegroundColor Yellow
}

$matched = @(Get-ChildItem -Path $GenDir -Filter "$Prefix*.uasset" -File -EA SilentlyContinue)
if ($matched.Count -eq 0) { Write-Host "No assets matching /Game/Generated/$Prefix* found." -ForegroundColor Green; exit 0 }

Write-Host ("Matched {0} test asset(s) under /Game/Generated (prefix '{1}'):" -f $matched.Count,$Prefix) -ForegroundColor Cyan
foreach ($f in $matched) { Write-Host ("  /Game/Generated/{0}" -f ($f.BaseName)) }

if (-not $Execute) {
  Write-Host "`nDRY-RUN (nothing deleted). Re-run with -Execute to delete the files above." -ForegroundColor Yellow
  exit 0
}

$deleted = 0
foreach ($f in $matched) {
  try { Remove-Item -LiteralPath $f.FullName -Force; $deleted++; Write-Host ("  deleted {0}" -f $f.Name) -ForegroundColor DarkGray }
  catch { Write-Host ("  FAILED to delete {0}: {1}" -f $f.Name, $_.Exception.Message) -ForegroundColor Red }
}
Write-Host ("Deleted {0}/{1} test asset(s)." -f $deleted,$matched.Count) -ForegroundColor Green
exit 0
