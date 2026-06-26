<#
  render_viz.ps1 — render bpparser_testgen DOT/Mermaid sources to PNG + SVG.

  PNG/SVG are NOT produced by the generator agent. Run this locally.
  Requires (any subset): Graphviz 'dot' for .dot ; Mermaid CLI 'mmdc' for .mmd.

  Usage:
    .\render_viz.ps1
    .\render_viz.ps1 -VIZ_DIR "..\bpparser_testgen\deliverables\viz" -OUT_DIR "..\bpparser_testgen\deliverables\viz"
#>
[CmdletBinding()]
param(
  [string]$VIZ_DIR = (Join-Path $PSScriptRoot '..\bpparser_testgen\deliverables\viz'),
  [string]$OUT_DIR = (Join-Path $PSScriptRoot '..\bpparser_testgen\deliverables\viz')
)
$ErrorActionPreference = 'Continue'
function Fail($msg){ Write-Error $msg; exit 1 }

if (-not (Test-Path $VIZ_DIR)) { Fail "VIZ_DIR not found: $VIZ_DIR" }
New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null

$hasDot  = [bool](Get-Command dot  -ErrorAction SilentlyContinue)
$hasMmdc = [bool](Get-Command mmdc -ErrorAction SilentlyContinue)
Write-Host "Graphviz dot : $(if($hasDot){'FOUND'}else{'MISSING -> install https://graphviz.org/download/'})"
Write-Host "Mermaid mmdc : $(if($hasMmdc){'FOUND'}else{'MISSING -> npm i -g @mermaid-js/mermaid-cli'})"

$any = $false
if ($hasDot) {
  Get-ChildItem -Path $VIZ_DIR -Filter *.dot | ForEach-Object {
    $b = [IO.Path]::GetFileNameWithoutExtension($_.Name); $any = $true
    & dot -Tpng $_.FullName -o (Join-Path $OUT_DIR "$b.png")
    & dot -Tsvg $_.FullName -o (Join-Path $OUT_DIR "$b.svg")
    Write-Host "dot  -> $b.png / $b.svg"
  }
}
if ($hasMmdc) {
  Get-ChildItem -Path $VIZ_DIR -Filter *.mmd | ForEach-Object {
    $b = [IO.Path]::GetFileNameWithoutExtension($_.Name); $any = $true
    & mmdc -i $_.FullName -o (Join-Path $OUT_DIR "$b.mmd.png") -b transparent
    & mmdc -i $_.FullName -o (Join-Path $OUT_DIR "$b.mmd.svg")
    Write-Host "mmdc -> $b.mmd.png / $b.mmd.svg"
  }
}

if (-not $any) { Write-Warning "No renderer available; nothing rendered. Install Graphviz and/or Mermaid CLI."; exit 2 }
Write-Host "Done. Images in: $OUT_DIR" -ForegroundColor Green
exit 0
