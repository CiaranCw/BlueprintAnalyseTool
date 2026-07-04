<#
  editor_live_client.ps1 - file-queue client for the in-editor BPAgentLiveService ("editor_live" mode).

  Submits ONE request into <Project>/Saved/BPParserAgentRequests/inbox and waits (bounded) for the
  in-editor plugin to answer via .../outbox/<id>.done|.failed. Never launches UnrealEditor-Cmd; if no
  live service consumes the request within -TimeoutSeconds, it reports the service as UNAVAILABLE so a
  caller can fall back to native_full.

  Usage (status probe):
    .\editor_live_client.ps1 -ProjectUProject "<...>.uproject" -Task status -TimeoutSeconds 20

  Usage (analyze):
    .\editor_live_client.ps1 -ProjectUProject "<...>.uproject" -Task analyze `
        -AssetPaths "/Game/UI/WBP_MainMenu" -TimeoutSeconds 60

  Usage (edit/create): pass -RequestJson pointing at a full editor_live request payload, or -RequestObject.

  Emits ONE PSCustomObject on the pipeline: { available, status, request_id, manifest, report_dir,
  outbox_marker, exit_code }. Exit codes: 0 success, 10 partial, 20 failed, 24 unavailable/timeout, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [ValidateSet('status','analyze','edit','create')] [string] $Task = 'status',
  [string[]] $AssetPaths = @(),
  [string] $RequestJson = "",           # optional: a complete editor_live request payload (overrides -Task/-AssetPaths)
  [hashtable] $RequestObject = $null,   # optional: same, as a hashtable
  [string] $OutputDir = "",             # default: <project>/Saved/BPParserAgentReports
  [int] $TimeoutSeconds = 30,
  [int] $PollMs = 500,
  [switch] $ReadOnly,                   # analyze default; forces read_only=true
  [switch] $AllowEdit,
  [switch] $AllowCreate,
  [switch] $AllowDestructiveEdit,
  [switch] $NoRenderPreview             # skip PNG/SVG rasterization of the returned viz
)
$ErrorActionPreference = 'Stop'
function Info($m){ Write-Host "[editor_live] $m" -ForegroundColor Cyan }
function Warn($m){ Write-Host "[editor_live] $m" -ForegroundColor Yellow }
function Write-Utf8NoBom([string]$path,[string]$text){
  $dir = Split-Path $path -Parent; if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  [IO.File]::WriteAllText($path, [string]$text, (New-Object System.Text.UTF8Encoding($false)))
}
function Emit($obj,[int]$code){ $obj | Write-Output; exit $code }

if (-not (Test-Path $ProjectUProject)) {
  Emit ([pscustomobject]@{ available=$false; status='bad_input'; request_id=''; manifest=''; report_dir=''; outbox_marker=''; exit_code=30 }) 30
}
$ProjectDir = Split-Path $ProjectUProject -Parent
$QueueRoot  = Join-Path $ProjectDir 'Saved\BPParserAgentRequests'
$Inbox      = Join-Path $QueueRoot 'inbox'
$Outbox     = Join-Path $QueueRoot 'outbox'
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports' }

# ---- build the request payload -------------------------------------------------
$reqId = "req_" + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + "_" + ([guid]::NewGuid().ToString('N').Substring(0,6))
if ($RequestJson -and (Test-Path $RequestJson)) {
  $req = Get-Content $RequestJson -Raw | ConvertFrom-Json
  if (-not $req.request_id) { $req | Add-Member -NotePropertyName request_id -NotePropertyValue $reqId -Force } else { $reqId = "$($req.request_id)" }
  if (-not $req.mode) { $req | Add-Member -NotePropertyName mode -NotePropertyValue 'editor_live' -Force }
} elseif ($RequestObject) {
  $req = [pscustomobject]$RequestObject
  if (-not $req.request_id) { $req | Add-Member -NotePropertyName request_id -NotePropertyValue $reqId -Force } else { $reqId = "$($req.request_id)" }
  if (-not $req.mode) { $req | Add-Member -NotePropertyName mode -NotePropertyValue 'editor_live' -Force }
} else {
  $exec = [ordered]@{
    read_only            = [bool]($ReadOnly -or $Task -eq 'analyze' -or $Task -eq 'status')
    strict               = $false
    render_preview       = (-not $NoRenderPreview)
    use_loaded_editor_state = $true
    allow_dirty_assets   = $false
  }
  if ($AllowEdit)            { $exec.read_only = $false; $exec.allow_edit = $true; $exec.create_backup = $true }
  if ($AllowDestructiveEdit) { $exec.allow_destructive_edit = $true }
  if ($AllowCreate)          { $exec.allow_create = $true }
  $req = [ordered]@{
    schema_version = '1.0'
    request_id     = $reqId
    task_type      = $Task
    mode           = 'editor_live'
    asset_paths    = @($AssetPaths)
    execution      = $exec
    output_dir     = ($OutputDir -replace '\\','/')
  }
}

# ---- submit: write payload, THEN the .ready commit marker ----------------------
New-Item -ItemType Directory -Force -Path $Inbox,$Outbox | Out-Null
$reqPath   = Join-Path $Inbox ("$reqId.request.json")
$readyPath = Join-Path $Inbox ("$reqId.ready")
$donePath  = Join-Path $Outbox ("$reqId.done")
$failPath  = Join-Path $Outbox ("$reqId.failed")
Write-Utf8NoBom $reqPath (($req | ConvertTo-Json -Depth 40))
Write-Utf8NoBom $readyPath (@{ committed_at=(Get-Date).ToUniversalTime().ToString('o') } | ConvertTo-Json)
Info "submitted request_id=$reqId task=$Task (timeout=${TimeoutSeconds}s)"

# ---- poll outbox (bounded; never hangs) ----------------------------------------
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$marker = ""; $ok = $false
while ((Get-Date) -lt $deadline) {
  if (Test-Path $donePath) { $marker = $donePath; $ok = $true; break }
  if (Test-Path $failPath) { $marker = $failPath; $ok = $false; break }
  Start-Sleep -Milliseconds $PollMs
}

if (-not $marker) {
  # No live service consumed the request -> unavailable. Clean up our stale inbox files.
  Warn "no response within ${TimeoutSeconds}s; editor_live UNAVAILABLE (is the UE editor open with the plugin loaded?)"
  Remove-Item $reqPath,$readyPath -Force -ErrorAction SilentlyContinue
  Emit ([pscustomobject]@{ available=$false; status='unavailable'; request_id=$reqId; manifest=''; report_dir=''; outbox_marker=''; exit_code=24 }) 24
}

# ---- read marker + manifest ----------------------------------------------------
$markerObj = try { Get-Content $marker -Raw | ConvertFrom-Json } catch { $null }
$manifestPath = if ($markerObj -and $markerObj.manifest) { "$($markerObj.manifest)" } else { "" }
$reportDir = if ($manifestPath) { Split-Path $manifestPath -Parent } else { "" }
$exit = if ($markerObj -and ($null -ne $markerObj.exit_code)) { [int]$markerObj.exit_code } else { if($ok){0}else{20} }

# ---- optional PNG/SVG rasterization for parity (client-side; C++ service emits DOT/MMD only) ----
if ($ok -and (-not $NoRenderPreview) -and $manifestPath -and (Test-Path $manifestPath)) {
  $dotExe = (Get-Command dot -ErrorAction SilentlyContinue).Source
  $dotFile = Join-Path $reportDir 'viz\blueprint.dot'
  if ($dotExe -and (Test-Path $dotFile)) {
    try {
      & $dotExe -Tpng $dotFile -o (Join-Path $reportDir 'viz\blueprint.png') 2>$null
      & $dotExe -Tsvg $dotFile -o (Join-Path $reportDir 'viz\blueprint.svg') 2>$null
    } catch {}
    if (Test-Path (Join-Path $reportDir 'viz\blueprint.png')) {
      try {
        $mf = Get-Content $manifestPath -Raw | ConvertFrom-Json
        if ($mf.outputs) {
          $mf.outputs.png = 'viz/blueprint.png'
          if (Test-Path (Join-Path $reportDir 'viz\blueprint.svg')) { $mf.outputs.svg = 'viz/blueprint.svg' }
          Write-Utf8NoBom $manifestPath ($mf | ConvertTo-Json -Depth 40)
          Info "rasterized viz PNG/SVG via Graphviz"
        }
      } catch {}
    }
  }
}

$status = switch ($exit) { 0 {'success'} 10 {'partial'} 40 {'rolled_back'} 41 {'exists_refused'} default { if($ok){'success'}else{'failed'} } }
Info "response: status=$status manifest=$manifestPath"
Emit ([pscustomobject]@{ available=$true; status=$status; request_id=$reqId; manifest=$manifestPath; report_dir=$reportDir; outbox_marker=$marker; exit_code=$exit }) $exit
