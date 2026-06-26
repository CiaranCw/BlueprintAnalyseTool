<#
  validate_outputs.ps1 — static validation of deliverables (no UE needed).
  Validates expected_ir JSON, edge referential integrity, viz presence, and
  emits machine-readable summaries for other Agents.

  Outputs (into -OutputDir):
    coverage_summary.json, failed_items.json, manual_check_required.json

  Exit codes: 0 = ok, 1 = hard validation failure (invalid JSON / dangling edges).

  Usage:
    .\validate_outputs.ps1 -OutputDir "E:\BPTestProject\BPTest\Saved\BPParserTestReports"
#>
[CmdletBinding()]
param(
  [string]$ExpectedIrDir = "",
  [string]$VizDir = "",
  [string]$GenerationLog = "",
  [Parameter(Mandatory=$true)] [string]$OutputDir
)
$ErrorActionPreference = 'Stop'

$repoDeliver = Join-Path $PSScriptRoot '..\bpparser_testgen\deliverables'
if ([string]::IsNullOrWhiteSpace($ExpectedIrDir)) { $ExpectedIrDir = Join-Path $repoDeliver 'expected_ir' }
if ([string]::IsNullOrWhiteSpace($VizDir))        { $VizDir        = Join-Path $repoDeliver 'viz' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$failed = New-Object System.Collections.ArrayList
$manual = New-Object System.Collections.ArrayList
$allTags = New-Object System.Collections.Generic.HashSet[string]
$req = 'asset_name','asset_path','blueprint_type','graphs','variables','functions','macros','event_dispatchers','interfaces','coverage_tags'
$irCount = 0; $refViolations = 0

if (Test-Path $ExpectedIrDir) {
  Get-ChildItem (Join-Path $ExpectedIrDir '*.json') | ForEach-Object {
    $irCount++
    $name = $_.Name
    try { $o = Get-Content $_.FullName -Raw | ConvertFrom-Json }
    catch { [void]$failed.Add(@{ item=$name; kind='invalid_json'; detail="$($_.Exception.Message)" }); return }

    $missing = @($req | Where-Object { -not ($o.PSObject.Properties.Name -contains $_) })
    if ($missing.Count -gt 0) { [void]$failed.Add(@{ item=$name; kind='missing_fields'; detail=($missing -join ',') }) }

    if ($o.coverage_tags) { foreach ($t in $o.coverage_tags) { [void]$allTags.Add([string]$t) } }

    # edge referential integrity
    if ($o.graphs) {
      foreach ($g in $o.graphs) {
        $ids = @(); if ($g.nodes) { $ids = @($g.nodes | ForEach-Object { $_.node_id }) }
        if ($g.edges) {
          foreach ($e in $g.edges) {
            foreach ($ref in @($e.from_node, $e.to_node)) {
              if ($ref -and ($ids -notcontains $ref)) {
                $refViolations++
                [void]$failed.Add(@{ item=$name; kind='dangling_edge'; detail="graph=$($g.graph_name) edge=$($e.edge_id) ref=$ref" })
              }
            }
          }
        }
      }
    }

    foreach ($key in 'needs_confirm','not_covered','manual_confirm','manual_optional') {
      if ($o.PSObject.Properties.Name -contains $key -and $o.$key) {
        foreach ($m in $o.$key) { [void]$manual.Add(@{ asset=$o.asset_name; source=$key; note=$m }) }
      }
    }
  }
}

$dot = @(Get-ChildItem (Join-Path $VizDir '*.dot') -ErrorAction SilentlyContinue).Count
$mmd = @(Get-ChildItem (Join-Path $VizDir '*.mmd') -ErrorAction SilentlyContinue).Count
$png = @(Get-ChildItem (Join-Path $VizDir '*.png') -ErrorAction SilentlyContinue).Count
$svg = @(Get-ChildItem (Join-Path $VizDir '*.svg') -ErrorAction SilentlyContinue).Count

# baseline manual-check items (always true for this suite)
@(
  @{ asset='*'; source='baseline'; note='Timeline / Async Action not auto-generated (cannot-auto-cover).' },
  @{ asset='BP_06'; source='baseline'; note='Verify Create Event -> Bind delegate pin resolved visually.' },
  @{ asset='BP_05'; source='baseline'; note='Macro_LogWithPrefix body is a manual wiring step.' },
  @{ asset='BP_03'; source='baseline'; note='Soft object/class reference default values are empty by design.' }
) | ForEach-Object { [void]$manual.Add($_) }

# generation errors
$genErrors = @()
if ($GenerationLog -and (Test-Path $GenerationLog)) {
  try {
    $gl = Get-Content $GenerationLog -Raw | ConvertFrom-Json
    $genErrors = @($gl.assets | Where-Object { $_.compile_status -eq 'error' } | ForEach-Object { $_.asset_path })
    foreach ($ga in $genErrors) { [void]$failed.Add(@{ item=$ga; kind='compile_error'; detail='see generation_log.json' }) }
  } catch { [void]$failed.Add(@{ item='generation_log.json'; kind='invalid_json'; detail="$($_.Exception.Message)" }) }
}

$coverage = [ordered]@{
  schema_version='1.0'; timestamp=(Get-Date).ToUniversalTime().ToString('o')
  expected_ir_count=$irCount; unique_coverage_tags=$allTags.Count
  viz_dot=$dot; viz_mmd=$mmd; viz_png=$png; viz_svg=$svg
  referential_integrity_violations=$refViolations
  generation_errors=$genErrors
}
($coverage | ConvertTo-Json -Depth 6) | Out-File (Join-Path $OutputDir 'coverage_summary.json') -Encoding utf8
(@{ schema_version='1.0'; count=$failed.Count; items=$failed } | ConvertTo-Json -Depth 6) | Out-File (Join-Path $OutputDir 'failed_items.json') -Encoding utf8
(@{ schema_version='1.0'; count=$manual.Count; items=$manual } | ConvertTo-Json -Depth 6) | Out-File (Join-Path $OutputDir 'manual_check_required.json') -Encoding utf8

Write-Host "validate_outputs: ir=$irCount tags=$($allTags.Count) dot=$dot mmd=$mmd refViolations=$refViolations failed=$($failed.Count) manual=$($manual.Count)"

$hard = @($failed | Where-Object { $_.kind -in 'invalid_json','missing_fields','dangling_edge' }).Count
if ($hard -gt 0) { Write-Warning "Hard validation failures: $hard"; exit 1 }
exit 0
