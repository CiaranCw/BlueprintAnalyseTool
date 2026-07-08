<#
  create_blueprint.ps1 - spec-driven Blueprint creation (Create task).
  Runs the read/write BPCreate commandlet in the target project's UE.

  Usage:
    .\create_blueprint.ps1 -ProjectUProject "<...>.uproject" -SpecFile ".\create_spec.json" `
        [-UERoot "<engine>"] [-OutputDir "<dir>"]

  The editor MUST be closed (creation writes/saves an asset).
  Exit codes (from commandlet): 0 success, 10 partial, 20 failed, 30 bad input, 41 exists(refused).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [Parameter(Mandatory=$true)] [string] $SpecFile,
  [string] $UERoot = "",
  [string] $OutputDir = ""
)
$ErrorActionPreference='Stop'
function Fail($m,$c){ Write-Error $m; exit $c }
if (-not (Test-Path $ProjectUProject)) { Fail "Project not found: $ProjectUProject" 30 }
if (-not (Test-Path $SpecFile)) { Fail "Spec not found: $SpecFile" 30 }
# Read as UTF-8 (PS 5.1 Get-Content uses the ANSI codepage and corrupts UTF-8, e.g. Chinese in a create spec).
try { [System.IO.File]::ReadAllText($SpecFile, (New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json | Out-Null } catch { Fail "Spec is not valid JSON/UTF-8: $_" 30 }
$ProjectDir = Split-Path $ProjectUProject -Parent

if ([string]::IsNullOrWhiteSpace($UERoot)) {
  $assoc = (Get-Content $ProjectUProject -Raw | ConvertFrom-Json).EngineAssociation
  if ($assoc -match '^\d+\.\d+$') {
    foreach ($d in 'C','D','E','F') { foreach ($b in @("$($d):\Program Files\Epic Games\UE_$assoc","$($d):\UE\UE_$assoc","$($d):\software\UE\UE_$assoc")) { if (Test-Path (Join-Path $b 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { $UERoot=$b; break } }; if($UERoot){break} }
  } else {
    foreach ($rp in @("HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds","HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds")) { try { $v=(Get-ItemProperty $rp -EA Stop).$assoc; if($v -and (Test-Path $v)){ $UERoot=$v; break } } catch {} }
  }
}
$cmd = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $cmd)) { Fail "UnrealEditor-Cmd not found (pass -UERoot)." 30 }
if (Get-Process -Name UnrealEditor -EA SilentlyContinue) { Fail "Close the UE editor first (creation writes an asset)." 20 }
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports\create' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

New-Item -ItemType Directory -Force -Path (Join-Path $OutputDir 'logs') | Out-Null
$stdoutLog = Join-Path $OutputDir 'logs\create_stdout.txt'
Write-Host "BPCreate: $SpecFile ..." -ForegroundColor Cyan
& $cmd "$ProjectUProject" -run=BPCreate -SpecFile="$SpecFile" -OutputDir="$OutputDir" -unattended -nopause -nop4 -stdout *>&1 | Out-File -Encoding utf8 $stdoutLog
$rc=$LASTEXITCODE

# The commandlet writes its manifest/create_result BEFORE the engine shuts down. UE headless teardown can crash
# in GC/module-shutdown AFTER all artifacts are written (e.g. 0xC0000005 / -1073741819). Do NOT judge overall
# success purely by the process exit code: if the reports are complete and status=success, treat it as
# success_with_exit_warning (record the raw exit code + log for a later root-cause pass), never fake failure.
function Read-JsonUtf8($p){ if(-not(Test-Path $p)){return $null}; try { [System.IO.File]::ReadAllText($p,(New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json } catch { $null } }

# Emit a graph preview (nodes + edges) from created_ir.graphs: viz/graph.dot (+ .mmd). Exec edges solid, data dashed.
function Emit-GraphPreview($createdIrPath, $vizDir){
  try {
    $ir = Read-JsonUtf8 $createdIrPath
    if(-not $ir -or -not $ir.graphs){ return }
    New-Item -ItemType Directory -Force -Path $vizDir | Out-Null
    $dot = New-Object System.Text.StringBuilder
    $mmd = New-Object System.Text.StringBuilder
    [void]$dot.AppendLine('digraph BPGraphs {'); [void]$dot.AppendLine('  rankdir=LR; compound=true; node[shape=box,style=rounded,fontsize=10];')
    [void]$mmd.AppendLine('flowchart LR')
    $nid = @{}; $counter = 0
    function San($s){ if(-not $s){return ''}; ($s -replace '[\r\n]+',' ' -replace '"','''') }
    foreach($g in $ir.graphs){
      $gname = San $g.graph_name
      $safeG = ($g.graph_name -replace '[^A-Za-z0-9_]','_')
      [void]$dot.AppendLine("  subgraph cluster_$safeG {"); [void]$dot.AppendLine("    label=`"$gname`"; style=rounded; color=gray70;")
      [void]$mmd.AppendLine("  subgraph $safeG[$gname]")
      foreach($n in @($g.nodes)){
        $counter++; $id = "n$counter"; $nid[[string]$n.node_id] = $id
        $label = San $n.node_title; if(-not $label){ $label = San $n.node_class }
        [void]$dot.AppendLine("    $id [label=`"$label`"];")
        [void]$mmd.AppendLine("    $id[`"$label`"]")
      }
      [void]$dot.AppendLine('  }')
      [void]$mmd.AppendLine('  end')
    }
    foreach($g in $ir.graphs){
      foreach($e in @($g.edges)){
        $f = $nid[[string]$e.from_node]; $t = $nid[[string]$e.to_node]
        if(-not $f -or -not $t){ continue }
        $isExec = ($e.edge_type -eq 'exec')
        $style = if($isExec){'solid'}else{'dashed'}
        [void]$dot.AppendLine("  $f -> $t [style=$style];")
        if($isExec){ [void]$mmd.AppendLine("  $f --> $t") } else { [void]$mmd.AppendLine("  $f -.-> $t") }
      }
    }
    [void]$dot.AppendLine('}')
    [System.IO.File]::WriteAllText((Join-Path $vizDir 'graph.dot'), $dot.ToString(), (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText((Join-Path $vizDir 'graph.mmd'), $mmd.ToString(), (New-Object System.Text.UTF8Encoding($false)))
  } catch { Write-Host "[warn] graph preview generation failed: $($_.Exception.Message)" -ForegroundColor Yellow }
}
$known = @(0,10,20,30,41)
$manifestPath = Join-Path $OutputDir 'manifest.json'
$man = Read-JsonUtf8 $manifestPath
$createdIrP = Join-Path $OutputDir 'created_ir.json'
if (Test-Path $createdIrP) { Emit-GraphPreview $createdIrP (Join-Path $OutputDir 'viz') }
$reportsComplete = ($man -ne $null) -and (Test-Path (Join-Path $OutputDir 'create_result.json')) -and (Test-Path $createdIrP)
$manStatus = if ($man) { "$($man.status)" } else { '' }

$name = switch ($rc) { 0 {'success'} 10 {'partial'} 20 {'failed'} 30 {'bad_input'} 41 {'exists_refused'} default {"exit_$rc"} }
$effRc = $rc
if (($known -notcontains $rc) -and $reportsComplete -and ($manStatus -eq 'success' -or $manStatus -eq 'partial')) {
  # Reclassify a post-write shutdown crash: keep artifacts + status, surface the crash as a warning.
  $note = "post_exit_crash: commandlet exited with code $rc AFTER writing complete artifacts (status=$manStatus); see logs/create_stdout.txt. Asset creation succeeded; the crash is in engine teardown and is tracked separately."
  try {
    if (-not ($man.PSObject.Properties.Name -contains 'warnings') -or $null -eq $man.warnings) { $man | Add-Member -NotePropertyName warnings -NotePropertyValue @() -Force }
    $man.warnings = @($man.warnings) + $note
    $man | Add-Member -NotePropertyName post_exit -NotePropertyValue ([pscustomobject]@{ exit_code=$rc; crashed=$true; stdout_log='logs/create_stdout.txt' }) -Force
    ($man | ConvertTo-Json -Depth 40) | Out-File -Encoding utf8 $manifestPath
  } catch {}
  $name = if ($manStatus -eq 'success') { 'success_with_exit_warning' } else { 'partial_with_exit_warning' }
  $effRc = if ($manStatus -eq 'success') { 0 } else { 10 }
  Write-Host "BPCreate -> $name (raw exit $rc; artifacts complete, status=$manStatus). Reports under: $OutputDir" -ForegroundColor Yellow
  Write-Host "  note: $note" -ForegroundColor Yellow
  exit $effRc
}
$col = if ($rc -eq 0) {'Green'} else {'Yellow'}
Write-Host "BPCreate -> $name (exit $rc). Reports under: $OutputDir" -ForegroundColor $col
exit $rc
