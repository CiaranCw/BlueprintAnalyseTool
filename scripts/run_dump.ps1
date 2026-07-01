<#
  run_dump.ps1 - thin alias to export a single Blueprint's raw IR via the native dumper
  (-run=BPParserTestDump). Kept for naming parity with the agent contract; delegates to export_ir.ps1.

  Usage:
    .\run_dump.ps1 -UERoot "<UE>" -ProjectUProject "<...>.uproject" -AssetPath "/Game/..." [-OutputDir "<dir>"]
#>
[CmdletBinding()]
param(
  [string] $UERoot = $env:UE_ROOT,
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [Parameter(Mandatory=$true)] [string] $AssetPath,
  [string] $OutputDir = ""
)
& (Join-Path $PSScriptRoot 'export_ir.ps1') -UERoot $UERoot -ProjectUProject $ProjectUProject -AssetPath $AssetPath -OutputDir $OutputDir
exit $LASTEXITCODE
