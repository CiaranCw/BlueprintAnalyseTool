<#
  compare_ir.ps1 — structural diff between an expected_ir baseline and a real IR
  dump (from export_ir.ps1). Compares by structure (node classes, graphs,
  variables, edge counts), NOT by literal ids (expected uses design ids; the
  dump uses UE GUIDs).

  Usage:
    .\compare_ir.ps1 -ExpectedJson "...\expected_ir\BP_01_PrimitivePins_Basic.json" `
                     -ActualJson   "...\ir_dumps\BP_01_PrimitivePins_Basic.ir.json" `
                     [-OutDiff "...\BP_01.diff.json"]

  Exit codes: 0 = no structural differences, 5 = differences found, 30 = bad args.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ExpectedJson,
  [Parameter(Mandatory=$true)] [string] $ActualJson,
  [string] $OutDiff = ""
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $ExpectedJson)) { Write-Error "Expected not found: $ExpectedJson"; exit 30 }
if (-not (Test-Path $ActualJson))   { Write-Error "Actual not found: $ActualJson"; exit 30 }

# Read as UTF-8 (PS 5.1 Get-Content uses the ANSI codepage and corrupts UTF-8, e.g. Chinese node comments in IR).
function Read-JsonUtf8([string]$p){ return ([System.IO.File]::ReadAllText($p, (New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json) }
$exp = Read-JsonUtf8 $ExpectedJson
$act = Read-JsonUtf8 $ActualJson

function NodeClassCounts($ir) {
  $h = @{}
  foreach ($g in $ir.graphs) { foreach ($n in $g.nodes) { $c = "$($n.node_class)"; if ($c) { $h[$c] = 1 + ($h[$c]) } } }
  return $h
}
function VarMap($ir) {
  $h = @{}
  foreach ($v in $ir.variables) { $h["$($v.name)"] = "$($v.category)/$($v.container_type)" }
  return $h
}
function GraphSet($ir) { return @($ir.graphs | ForEach-Object { "$($_.graph_name):$($_.graph_type)" }) }

$expNC = NodeClassCounts $exp; $actNC = NodeClassCounts $act
$nodeClassDiff = @()
foreach ($k in (@($expNC.Keys) + @($actNC.Keys) | Select-Object -Unique)) {
  $e = [int]$expNC[$k]; $a = [int]$actNC[$k]
  if ($e -ne $a) { $nodeClassDiff += @{ node_class=$k; expected=$e; actual=$a } }
}

$expV = VarMap $exp; $actV = VarMap $act
$varDiff = @()
foreach ($k in (@($expV.Keys) + @($actV.Keys) | Select-Object -Unique)) {
  if ("$($expV[$k])" -ne "$($actV[$k])") { $varDiff += @{ variable=$k; expected=$expV[$k]; actual=$actV[$k] } }
}

$expG = GraphSet $exp; $actG = GraphSet $act
$graphsOnlyExpected = @($expG | Where-Object { $actG -notcontains $_ })
$graphsOnlyActual   = @($actG | Where-Object { $expG -notcontains $_ })

function EdgeCount($ir) { $c=0; foreach ($g in $ir.graphs) { $c += @($g.edges).Count }; return $c }
$expE = EdgeCount $exp; $actE = EdgeCount $act

$risk = @()
if ($nodeClassDiff.Count -gt 0) { $risk += "Node-class counts differ (engine may auto-add hidden/advanced pins or expand macros)." }
if ($varDiff.Count -gt 0) { $risk += "Variable type/container differences may affect parser type inference." }
if ($graphsOnlyExpected.Count -gt 0 -or $graphsOnlyActual.Count -gt 0) { $risk += "Graph set differs (function/macro/delegate graph added or removed)." }

$diff = [ordered]@{
  schema_version='1.0'
  asset_name = $exp.asset_name
  expected = (Split-Path $ExpectedJson -Leaf)
  actual = (Split-Path $ActualJson -Leaf)
  node_class_diff = $nodeClassDiff
  variable_diff = $varDiff
  graphs_only_expected = $graphsOnlyExpected
  graphs_only_actual = $graphsOnlyActual
  edge_count = @{ expected=$expE; actual=$actE }
  risk_notes = $risk
}

$json = $diff | ConvertTo-Json -Depth 8
if ($OutDiff) { New-Item -ItemType Directory -Force -Path (Split-Path $OutDiff -Parent) | Out-Null; $json | Out-File $OutDiff -Encoding utf8; Write-Host "Diff written: $OutDiff" }
else { Write-Host $json }

$hasDiff = ($nodeClassDiff.Count + $varDiff.Count + $graphsOnlyExpected.Count + $graphsOnlyActual.Count) -gt 0
if ($hasDiff) { exit 5 } else { exit 0 }
