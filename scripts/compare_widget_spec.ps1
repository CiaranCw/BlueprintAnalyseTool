<#
  compare_widget_spec.ps1 - expected-vs-actual compare for a Widget Blueprint create request.

  Reads the create request (expected: hierarchy widgets + events/handlers) and the redumped created_ir.json
  (actual: widget_tree + widget_event_bindings), and reports whether every requested widget exists and every
  requested event is bound to the requested handler and connected. Writes compare_report.json.

  Usage:
    .\compare_widget_spec.ps1 -SpecFile ".\create_spec.json" -CreatedIr "<out>\created_ir.json" `
        [-Manifest "<out>\manifest.json"] [-OutFile "<out>\compare_report.json"]

  Exit codes: 0 match, 10 mismatch, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $SpecFile,
  [Parameter(Mandatory=$true)] [string] $CreatedIr,
  [string] $Manifest = "",
  [string] $OutFile = ""
)
$ErrorActionPreference='Stop'
function Read-JsonUtf8($p){ if(-not(Test-Path $p)){return $null}; [System.IO.File]::ReadAllText($p,(New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json }
function Fail($m,$c){ Write-Error $m; exit $c }
if(-not(Test-Path $SpecFile)){ Fail "Spec not found: $SpecFile" 30 }
if(-not(Test-Path $CreatedIr)){ Fail "created_ir not found: $CreatedIr" 30 }
$spec = Read-JsonUtf8 $SpecFile
$ir   = Read-JsonUtf8 $CreatedIr
if(-not $spec -or -not $ir){ Fail "could not parse spec/created_ir JSON" 30 }
if(-not $OutFile){ $OutFile = Join-Path (Split-Path $CreatedIr -Parent) 'compare_report.json' }
$man = if($Manifest){ Read-JsonUtf8 $Manifest } else { $null }

# spec may be a full request (.request/.widget) or bare (.widget)
$req = if($spec.request){ $spec.request } else { $spec }
$widget = $req.widget
if(-not $widget){ Fail "spec has no .widget section" 30 }

function BaseName([string]$s){ if(-not $s){return ''}; $t=$s -replace '.*[\./]',''; return ($t -replace '_C$','') }

# ---- expected widgets (flatten spec hierarchy) ----
$expectedWidgets = New-Object System.Collections.ArrayList
function Walk-Spec($node){ if(-not $node){return}; [void]$expectedWidgets.Add([pscustomobject]@{ name=$node.name; type=$node.type }); if($node.children){ foreach($c in $node.children){ Walk-Spec $c } } }
$root = if($widget.hierarchy.root){ $widget.hierarchy.root } else { $widget.hierarchy }
Walk-Spec $root

# ---- actual widgets (flatten created_ir widget_tree) ----
$actualWidgets = @{}
function Walk-Actual($node){ if(-not $node){return}; if($node.name){ $actualWidgets[[string]$node.name]=[string]$node.class }; if($node.children){ foreach($c in $node.children){ Walk-Actual $c } } }
if($ir.widget_tree){ $aroot = if($ir.widget_tree.root){ $ir.widget_tree.root } else { $ir.widget_tree }; Walk-Actual $aroot }

# ---- compare widgets ----
$widgetRows = New-Object System.Collections.ArrayList
$wOk=0
foreach($e in $expectedWidgets){
  $found = $actualWidgets.ContainsKey([string]$e.name)
  $actualClass = if($found){ $actualWidgets[[string]$e.name] } else { '' }
  $classMatch = $found -and ((BaseName $e.type) -eq (BaseName $actualClass))
  if($found -and $classMatch){ $wOk++ }
  [void]$widgetRows.Add([pscustomobject]@{ name=$e.name; expected_type=$e.type; found=$found; actual_class=$actualClass; class_match=$classMatch })
}

# ---- compare events/handlers ----
# actual bindings keyed by widget (lower)
$actualBind = @{}
foreach($b in @($ir.widget_event_bindings)){ if($b.widget){ $actualBind[([string]$b.widget).ToLower()] = $b } }
$eventRows = New-Object System.Collections.ArrayList
$eOk=0
foreach($ev in @($widget.events)){
  $w=[string]$ev.widget; $evt=[string]$ev.event
  $expType = if($ev.handler.type){ [string]$ev.handler.type } else { 'bound_event' }
  $expName = [string]$ev.handler.name
  $act = $actualBind[$w.ToLower()]
  $found = $null -ne $act
  $hType = if($found -and $act.handler){ [string]$act.handler.type } else { '' }
  $hName = if($found -and $act.handler){ [string]$act.handler.name } else { '' }
  $connected = $found -and $act.handler -and [bool]$act.handler.connected
  $typeMatch = $hType -eq $expType
  $nameMatch = ($expName -eq '') -or ($hName -eq $expName)
  $ok = $found -and $typeMatch -and $nameMatch -and ($expType -eq 'bound_event' -or $connected)
  if($ok){ $eOk++ }
  [void]$eventRows.Add([pscustomobject]@{ widget=$w; event=$evt; expected_handler_type=$expType; expected_handler_name=$expName;
    found=$found; actual_handler_type=$hType; actual_handler_name=$hName; connected=$connected; match=$ok })
}

# ---- compile status ----
$compile = ''
if($man -and $man.compile_status){ $compile = [string]$man.compile_status }
elseif($ir.compile_status){ $compile = [string]$ir.compile_status }
$compileOk = -not ($compile -match 'error|fail')

$widgetsMatch = ($wOk -eq $expectedWidgets.Count)
$eventsMatch  = ($eOk -eq @($widget.events).Count)
$overall = if($widgetsMatch -and $eventsMatch){ 'match' } else { 'mismatch' }

$report = [pscustomobject]@{
  schema_version = '1.0'
  status = $overall
  asset_path = $req.asset.asset_path
  widgets = [pscustomobject]@{ expected=$expectedWidgets.Count; matched=$wOk; rows=$widgetRows }
  events  = [pscustomobject]@{ expected=@($widget.events).Count; matched=$eOk; rows=$eventRows }
  compile = [pscustomobject]@{ status=$compile; ok=$compileOk }
  generated_at = (Get-Date).ToUniversalTime().ToString('o')
}
($report | ConvertTo-Json -Depth 12) | Out-File -Encoding utf8 $OutFile

$col = if($overall -eq 'match'){'Green'}else{'Yellow'}
Write-Host ("compare: {0}  widgets {1}/{2}  events {3}/{4}  -> {5}" -f $overall,$wOk,$expectedWidgets.Count,$eOk,@($widget.events).Count,$OutFile) -ForegroundColor $col
foreach($r in $widgetRows){ if(-not $r.class_match){ Write-Host ("  [widget] {0}: found={1} actual={2}" -f $r.name,$r.found,$r.actual_class) -ForegroundColor Yellow } }
foreach($r in $eventRows){ if(-not $r.match){ Write-Host ("  [event] {0}.{1}: found={2} type={3} name={4} connected={5}" -f $r.widget,$r.event,$r.found,$r.actual_handler_type,$r.actual_handler_name,$r.connected) -ForegroundColor Yellow } }
if($overall -eq 'match'){ exit 0 } else { exit 10 }
