<#
  render_viz.ps1
  Renders the DOT (.dot) and Mermaid (.mmd) sources in .\viz to PNG + SVG.

  PNG/SVG are NOT produced by the generator agent (no Graphviz/Mermaid available in
  that environment). Run this locally to materialize the images.

  Requirements (any subset works):
    - Graphviz 'dot' on PATH         -> renders .dot  (https://graphviz.org/download/)
    - Mermaid CLI 'mmdc' on PATH     -> renders .mmd  (npm i -g @mermaid-js/mermaid-cli)

  Usage:
    powershell -ExecutionPolicy Bypass -File .\render_viz.ps1
    powershell -ExecutionPolicy Bypass -File .\render_viz.ps1 -OutDir "E:\BPTestProject\BPTest\Saved\BPParserTestReports"
#>
param(
  [string]$VizDir = (Join-Path $PSScriptRoot 'viz'),
  [string]$OutDir = (Join-Path $PSScriptRoot 'viz')
)

$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$hasDot  = [bool](Get-Command dot  -ErrorAction SilentlyContinue)
$hasMmdc = [bool](Get-Command mmdc -ErrorAction SilentlyContinue)

Write-Host "Graphviz dot : $(if($hasDot){'FOUND'}else{'missing'})"
Write-Host "Mermaid mmdc : $(if($hasMmdc){'FOUND'}else{'missing'})"

if ($hasDot) {
  Get-ChildItem -Path $VizDir -Filter *.dot | ForEach-Object {
    $base = [IO.Path]::GetFileNameWithoutExtension($_.Name)
    Write-Host "dot -> $base.png / .svg"
    & dot -Tpng $_.FullName -o (Join-Path $OutDir "$base.png")
    & dot -Tsvg $_.FullName -o (Join-Path $OutDir "$base.svg")
  }
} else {
  Write-Warning "Skipping .dot rendering (install Graphviz)."
}

if ($hasMmdc) {
  Get-ChildItem -Path $VizDir -Filter *.mmd | ForEach-Object {
    $base = [IO.Path]::GetFileNameWithoutExtension($_.Name)
    Write-Host "mmdc -> $base.mmd.png / .mmd.svg"
    & mmdc -i $_.FullName -o (Join-Path $OutDir "$base.mmd.png") -b transparent
    & mmdc -i $_.FullName -o (Join-Path $OutDir "$base.mmd.svg")
  }
} else {
  Write-Warning "Skipping .mmd rendering (install @mermaid-js/mermaid-cli)."
}

Write-Host "Done. Images (if any) are in: $OutDir"
