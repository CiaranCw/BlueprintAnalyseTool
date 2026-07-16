<#
  editor_live_regression.ps1 — 14-case Editor Live production-hardening matrix.

  Runs against /Game/Generated/ assets only (never touches /Game/Assets/ business paths).
  Requires an open UE editor with BPParserTestGen loaded.

  Usage:
    .\scripts\editor_live_regression.ps1 `
      -ProjectUProject "D:/Projects/AClient/AClient.uproject" `
      -HardeningVersion 0.4.7

  Exit: 0 all executed cases passed, 10 partial (skipped pending plugin), 20 failed.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)] [string] $ProjectUProject,
  [string] $OutputDir = "",
  [string] $CopyAsset = "/Game/Generated/WBP_Agent_Live_Settings_Graphics",
  [string] $BaseAsset = "/Game/Generated/WBP_Agent_SettingsGraphicsBase",
  [string] $HardeningVersion = "0.4.7",
  [int] $TimeoutSeconds = 90,
  [switch] $StopOnFail
)
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$Client = Join-Path $ScriptDir 'editor_live_client.ps1'
if (-not (Test-Path $Client)) { Write-Error "Missing $Client"; exit 30 }

function Info($m) { Write-Host "[regression] $m" -ForegroundColor Cyan }
function Invoke-Live {
  param([hashtable] $Req, [string] $Label, [int] $ExpectExit = 0, [string[]] $ExpectStatus = @('success'))
  Info "CASE: $Label"
  $r = & $Client -ProjectUProject $ProjectUProject -RequestObject $Req -TimeoutSeconds $TimeoutSeconds
  $ok = ($r.exit_code -eq $ExpectExit) -and ($ExpectStatus -contains $r.status)
  $row = [ordered]@{
    case       = $Label
    request_id = $r.request_id
    exit_code  = $r.exit_code
    status     = $r.status
    manifest   = $r.manifest
    pass       = [bool]$ok
  }
  if (-not $ok -and $StopOnFail) {
    $row | ConvertTo-Json -Depth 4 | Write-Output
    exit 20
  }
  return [pscustomobject]$row
}
function Read-Manifest([string]$Path) {
  if (-not $Path -or -not (Test-Path $Path)) { return $null }
  $t = [IO.File]::ReadAllText($Path, (New-Object System.Text.UTF8Encoding($false)))
  return ($t | ConvertFrom-Json)
}
function Write-Utf8NoBom([string]$Path, [string]$Text) {
  $dir = Split-Path $Path -Parent
  if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  [IO.File]::WriteAllText($Path, [string]$Text, (New-Object System.Text.UTF8Encoding($false)))
}
function Skip-Case([string]$Label, [string]$Reason) {
  Info "SKIP: $Label — $Reason"
  return [pscustomobject]@{ case = $Label; pass = $null; skipped = $true; reason = $Reason }
}
function Pass-Case([string]$Label, [bool]$Ok, $Detail) {
  Info ("CASE: $Label -> " + $(if ($Ok) { 'PASS' } else { 'FAIL' }))
  return [pscustomobject]@{ case = $Label; pass = [bool]$Ok; detail = $Detail }
}
function Get-ReportDir([string]$ManifestPath) {
  if ($ManifestPath) { return (Split-Path $ManifestPath -Parent) } else { return "" }
}
function Get-Journal([string]$ReportDir) {
  $p = Join-Path $ReportDir 'request_journal.json'
  return (Read-Manifest $p)
}
function Find-EditResult([string]$ReportDir) {
  if (-not $ReportDir -or -not (Test-Path $ReportDir)) { return $null }
  $f = Get-ChildItem -Recurse $ReportDir -Filter edit_result.json -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($f) { return (Read-Manifest $f.FullName) } else { return $null }
}
# Map /Game/... package path to its on-disk .uasset under <project>/Content/...
function Get-UassetPath([string]$PackagePath) {
  $rel = $PackagePath -replace '^/Game/', ''
  return (Join-Path $ProjectDir ("Content/" + ($rel -replace '/','\') + ".uasset"))
}

$ProjectDir = Split-Path $ProjectUProject -Parent
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $ProjectDir 'Saved/BPParserAgentReports/editor_live_regression'
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$RunId = "reg_" + (Get-Date -Format 'yyyyMMdd_HHmmss')
$ReportPath = Join-Path $OutputDir "$RunId.json"
$Results = @()

# Probe service + plugin version hints
$st = & $Client -ProjectUProject $ProjectUProject -Task status -TimeoutSeconds 20
if (-not $st.available) { Write-Error "editor_live unavailable"; exit 24 }
$stManifest = Read-Manifest $st.manifest
$pluginVer = ""
if ($stManifest) {
  if ($stManifest.plugin_version) { $pluginVer = "$($stManifest.plugin_version)" }
  elseif ($stManifest.editor_live -and $stManifest.editor_live.plugin_version) { $pluginVer = "$($stManifest.editor_live.plugin_version)" }
}
# Runtime hardening requires compiled plugin; gate on semver when manifest exposes plugin_version.
function Test-HardenedRuntime {
  if ($pluginVer -match '^\d+\.\d+\.\d+$') { return ([version]$pluginVer -ge [version]$HardeningVersion) }
  return $false
}
$hardenedRuntime = Test-HardenedRuntime
$pvLabel = if ($pluginVer) { $pluginVer } else { 'unknown' }
$supports = $null
if ($stManifest -and $stManifest.editor_live -and $stManifest.editor_live.supports) { $supports = $stManifest.editor_live.supports }
function Has-Support([string]$Name) { return ($supports -and ($supports.PSObject.Properties.Name -contains $Name) -and ($supports.$Name -eq $true)) }
Info "editor_live up; plugin_version=$pvLabel hardened_runtime=$hardenedRuntime target=$HardeningVersion test_control=$(Has-Support 'test_control') recover_scan=$(Has-Support 'recover_scan')"

# 1 Normal analyze
$Results += Invoke-Live @{
  schema_version = '1.0'; task_type = 'analyze'; mode = 'editor_live'
  asset_paths = @($CopyAsset)
  execution = @{ read_only = $true; use_loaded_editor_state = $true; render_preview = $false }
  output_dir = ($OutputDir -replace '\\','/')
} -Label '01_normal_analyze'

# 2 Required property fail (preflight) — needs hardened plugin
if ($hardenedRuntime) {
  $Results += Invoke-Live @{
    schema_version = '1.0'; task_type = 'edit'; mode = 'editor_live'
    asset_paths = @($CopyAsset)
    execution = @{ read_only = $false; allow_edit = $true; run_preflight = $true; strict_preflight = $true; create_backup = $false }
    edit = @{
      asset_path = $CopyAsset; mode = 'apply-and-verify'
      operations = @(@{
        op_id = 'pf_req_fail'; operation = 'set_widget_property'
        widget = 'BP_Agent_NewCheckbox'; property = 'NopeRequiredField'; value = 'x'
      })
    }
    output_dir = ($OutputDir -replace '\\','/')
  } -Label '02_required_property_fail' -ExpectExit 20 -ExpectStatus @('failed')
} else { $Results += Skip-Case '02_required_property_fail' 'needs compiled plugin >= $HardeningVersion' }

# 3 Optional property fail (pass_with_warnings)
if ($hardenedRuntime) {
  $Results += Invoke-Live @{
    schema_version = '1.0'; task_type = 'edit'; mode = 'editor_live'
    asset_paths = @($CopyAsset)
    execution = @{ read_only = $false; allow_edit = $true; run_preflight = $true; strict_preflight = $true; create_backup = $false }
    edit = @{
      asset_path = $CopyAsset; mode = 'apply-and-verify'
      operations = @(@{
        op_id = 'pf_opt'; operation = 'set_widget_property'
        widget = 'BP_Agent_NewKeybind'; property = 'ItemId'; value = 'ignored'
        optional_properties = @('ItemId')
      })
    }
    output_dir = ($OutputDir -replace '\\','/')
  } -Label '03_optional_property_fail' -ExpectExit 10 -ExpectStatus @('success','success_with_warnings')
} else { $Results += Skip-Case '03_optional_property_fail' 'needs hardened preflight' }

# 4 Alias match success (DefaultChecked -> bDefaultChecked) on plan-only
$Results += Invoke-Live @{
  schema_version = '1.0'; task_type = 'edit'; mode = 'editor_live'
  asset_paths = @($CopyAsset)
  execution = @{ read_only = $false; allow_edit = $true; create_backup = $false }
  edit = @{
    asset_path = $CopyAsset; mode = 'plan-only'
    operations = @(@{
      op_id = 'alias'; operation = 'set_widget_property'
      widget = 'BP_Agent_NewCheckbox'; property = 'DefaultChecked'; value = $true
    })
  }
  output_dir = ($OutputDir -replace '\\','/')
} -Label '04_alias_match_plan_only'

# 5 Dirty target blocked (simulate by requiring clean — expect blocked if dirty)
if ($hardenedRuntime) {
  $Results += Invoke-Live @{
    schema_version = '1.0'; task_type = 'edit'; mode = 'editor_live'
    asset_paths = @($CopyAsset)
    execution = @{ read_only = $false; allow_edit = $true; allow_dirty_target = $false; create_backup = $false }
    edit = @{ asset_path = $CopyAsset; mode = 'plan-only'; operations = @() }
    output_dir = ($OutputDir -replace '\\','/')
  } -Label '05_dirty_target_policy' -ExpectExit 0 -ExpectStatus @('success','failed','blocked_by_editor_state')
} else { $Results += Skip-Case '05_dirty_target_policy' 'needs hardened dirty gate' }

# 6 PIE edit refused — inject a bounded PIE window (fault-injection; no real PIE session) then submit an
#   edit: the editor-state gate must refuse with blocked_by_editor_state. Clears the window afterwards.
if (Has-Support 'test_control') {
  $null = Invoke-Live @{ schema_version='1.0'; task_type='test_control'; mode='editor_live'; force_pie_ms=8000; output_dir=($OutputDir -replace '\\','/') } -Label '06_pie_inject'
  $r06 = Invoke-Live @{
    schema_version='1.0'; task_type='edit'; mode='editor_live'
    asset_paths=@($CopyAsset)
    execution=@{ read_only=$false; allow_edit=$true; allow_edit_during_pie=$false; create_backup=$false }
    edit=@{ asset_path=$CopyAsset; mode='plan-only'; operations=@(@{ op_id='pie'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value='pie' }) }
    output_dir=($OutputDir -replace '\\','/')
  } -Label '06_pie_edit_refused' -ExpectExit 30 -ExpectStatus @('blocked_by_editor_state','failed')
  # clear the injected PIE window so it cannot bleed into later cases
  $null = Invoke-Live @{ schema_version='1.0'; task_type='test_control'; mode='editor_live'; force_pie_ms=0; output_dir=($OutputDir -replace '\\','/') } -Label '06_pie_clear'
  $Results += $r06
} else { $Results += Skip-Case '06_pie_edit_refused' 'needs test_control (plugin >= 0.4.8)' }

# 7 Compiling wait — inject a bounded busy window (fault-injection; no real long compile) then submit an
#   edit: the pump must DEFER (journal waiting/editor_busy) and the request must still complete once the
#   window expires (resilient retry, not dropped/hung).
if (Has-Support 'test_control') {
  $null = Invoke-Live @{ schema_version='1.0'; task_type='test_control'; mode='editor_live'; force_busy_ms=6000; output_dir=($OutputDir -replace '\\','/') } -Label '07_busy_inject'
  $r07 = Invoke-Live @{
    schema_version='1.0'; task_type='edit'; mode='editor_live'
    asset_paths=@($CopyAsset)
    execution=@{ read_only=$false; allow_edit=$true; create_backup=$false }
    edit=@{ asset_path=$CopyAsset; mode='plan-only'; operations=@(@{ op_id='busy'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value='busy' }) }
    output_dir=($OutputDir -replace '\\','/')
  } -Label '07_compiling_wait' -ExpectExit 0 -ExpectStatus @('success','success_with_warnings')
  $j07 = Get-Journal (Get-ReportDir $r07.manifest)
  $waited = $false
  if ($j07 -and $j07.entries) { $waited = (@($j07.entries | Where-Object { "$($_.phase)" -eq 'waiting' -or "$($_.status)" -eq 'editor_busy' }).Count -gt 0) }
  $r07.pass = ([bool]$r07.pass -and $waited)
  $r07 | Add-Member -NotePropertyName deferred_while_busy -NotePropertyValue $waited -Force
  $null = Invoke-Live @{ schema_version='1.0'; task_type='test_control'; mode='editor_live'; force_busy_ms=0; output_dir=($OutputDir -replace '\\','/') } -Label '07_busy_clear'
  $Results += $r07
} else { $Results += Skip-Case '07_compiling_wait' 'needs test_control (plugin >= 0.4.8)' }

# 8 Duplicate request_id idempotency
$dupId = "req_dup_$RunId"
$dupReq = @{
  schema_version = '1.0'; request_id = $dupId; task_type = 'status'; mode = 'editor_live'
  output_dir = ($OutputDir -replace '\\','/')
}
$r1 = Invoke-Live $dupReq -Label '08_duplicate_id_first'
$r2 = Invoke-Live $dupReq -Label '08_duplicate_id_second'
$Results += [pscustomobject]@{ case = '08_duplicate_request_id'; pass = ($r1.pass -and $r2.pass); request_id = $dupId }

# 9 Concurrent same-asset — submit two plan-only edits on the SAME asset simultaneously; the file-queue
#   pump serializes one-per-tick and the asset lock guards overlap. Assert BOTH complete successfully
#   (no corruption / no lost request) rather than racing.
Info "CASE: 09_concurrent_same_asset (two simultaneous submissions)"
$reqA = @{ schema_version='1.0'; request_id="req_cc_a_$RunId"; task_type='edit'; mode='editor_live'
  asset_paths=@($CopyAsset); execution=@{ read_only=$false; allow_edit=$true; create_backup=$false }
  edit=@{ asset_path=$CopyAsset; mode='plan-only'; operations=@(@{ op_id='ccA'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value='CC A' }) }
  output_dir=($OutputDir -replace '\\','/') }
$reqB = @{ schema_version='1.0'; request_id="req_cc_b_$RunId"; task_type='edit'; mode='editor_live'
  asset_paths=@($CopyAsset); execution=@{ read_only=$false; allow_edit=$true; create_backup=$false }
  edit=@{ asset_path=$CopyAsset; mode='plan-only'; operations=@(@{ op_id='ccB'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value='CC B' }) }
  output_dir=($OutputDir -replace '\\','/') }
$ccJobs = @()
foreach ($rq in @($reqA,$reqB)) {
  $ccJobs += Start-Job -ScriptBlock {
    param($client,$proj,$reqJson,$to)
    $obj = $reqJson | ConvertFrom-Json
    $ht = @{}; $obj.PSObject.Properties | ForEach-Object { $ht[$_.Name] = $_.Value }
    & $client -ProjectUProject $proj -RequestObject $ht -TimeoutSeconds $to
  } -ArgumentList $Client, $ProjectUProject, ($rq | ConvertTo-Json -Depth 20), $TimeoutSeconds
}
$null = $ccJobs | Wait-Job -Timeout ($TimeoutSeconds + 30)
$ccOuts = $ccJobs | ForEach-Object { Receive-Job $_ -ErrorAction SilentlyContinue }
$ccJobs | Remove-Job -Force -ErrorAction SilentlyContinue
$ccStatuses = @($ccOuts | ForEach-Object { "$($_.status)" })
$okCC = (@($ccStatuses | Where-Object { $_ -in @('success','success_with_warnings') }).Count -eq 2)
$Results += Pass-Case '09_concurrent_same_asset' $okCC @{ statuses = $ccStatuses }

# 10 stale_plan
if ($hardenedRuntime) {
  $plan = Invoke-Live @{
    schema_version = '1.0'; task_type = 'edit'; mode = 'editor_live'
    asset_paths = @($CopyAsset)
    execution = @{ read_only = $false; allow_edit = $true; create_backup = $false }
    edit = @{ asset_path = $CopyAsset; mode = 'plan-only'; operations = @(@{
      op_id = 'noop'; operation = 'set_widget_property'; widget = 'BP_Agent_NewCheckbox'; property = 'TextName'; value = 'Agent Toggle'
    }) }
    output_dir = ($OutputDir -replace '\\','/')
  } -Label '10_stale_plan_plan_only'
  $planManifest = Read-Manifest $plan.manifest
  $hash = if ($planManifest.edit_result.baseline_ir_hash) { $planManifest.edit_result.baseline_ir_hash } elseif ($planManifest.plan.baseline_ir_hash) { $planManifest.plan.baseline_ir_hash } else { 'deadbeef' }
  $Results += Invoke-Live @{
    schema_version = '1.0'; task_type = 'edit'; mode = 'editor_live'
    asset_paths = @($CopyAsset)
    execution = @{ read_only = $false; allow_edit = $true; create_backup = $false; run_preflight = $false }
    edit = @{
      asset_path = $CopyAsset; mode = 'apply-and-verify'; baseline_ir_hash = $hash
      operations = @(@{ op_id = 'stale'; operation = 'set_widget_property'; widget = 'BP_Agent_NewCheckbox'; property = 'TextName'; value = 'Stale Test' })
    }
    output_dir = ($OutputDir -replace '\\','/')
  } -Label '10_stale_plan_apply_bad_hash' -ExpectExit 50 -ExpectStatus @('failed','stale_plan')
} else { $Results += Skip-Case '10_stale_plan' 'needs baseline_ir_hash in plan-only' }

# 11 Failing-op rollback + journal — run_preflight=false so a missing REQUIRED property fails at APPLY
#    time (not preflight), exercising the transaction rollback + journal 'rolled_back' path.
if ($hardenedRuntime) {
  $r11 = Invoke-Live @{
    schema_version='1.0'; task_type='edit'; mode='editor_live'
    asset_paths=@($CopyAsset)
    execution=@{ read_only=$false; allow_edit=$true; run_preflight=$false; create_backup=$false }
    edit=@{ asset_path=$CopyAsset; mode='apply-and-verify'
      operations=@(@{ op_id='rb'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='NopeRollbackField'; value='x' }) }
    output_dir=($OutputDir -replace '\\','/')
  } -Label '11_compile_fail_rollback' -ExpectExit 40 -ExpectStatus @('rolled_back')
  # Verify the journal recorded the rollback/failed terminal phase.
  $rd11 = Get-ReportDir $r11.manifest
  $j11 = Get-Journal $rd11
  $jphase = if ($j11 -and $j11.entries) { @($j11.entries | ForEach-Object { "$($_.phase)/$($_.status)" }) } else { @() }
  $journalOk = ($jphase -join ' ') -match 'rolled_back|failed'
  $er11 = Find-EditResult $rd11
  $rolledBack = ($er11 -and ($er11.status -eq 'rolled_back' -or $er11.rollback_performed -eq $true))
  $r11.pass = ([bool]$r11.pass -and $journalOk -and $rolledBack)
  $r11 | Add-Member -NotePropertyName journal_phases -NotePropertyValue $jphase -Force
} else { $r11 = Skip-Case '11_compile_fail_rollback' 'needs hardened rollback+journal' }
$Results += $r11

# 12 Save-fail handling — duplicate a throwaway copy, mark its .uasset read-only on disk, then apply a real
#    change: ops succeed but SaveAsset fails -> status 'partial' with save_status=failed. Restore after.
if ($hardenedRuntime) {
  $throwaway = "/Game/Generated/WBP_Agent_SaveFailTest_$RunId"
  $uasset = Get-UassetPath $throwaway
  $sfPass = $false; $sfDetail = @{}
  try {
    # (a) create/refresh the throwaway from the known copy (work_on_copy duplicates + saves it)
    $mk = Invoke-Live @{
      schema_version='1.0'; task_type='edit'; mode='editor_live'
      asset_paths=@($CopyAsset)
      execution=@{ read_only=$false; allow_edit=$true; create_backup=$false }
      edit=@{ asset_path=$CopyAsset; work_on_copy=$throwaway; mode='apply-and-verify'
        operations=@(@{ op_id='mk'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value='SaveFail Seed' }) }
      output_dir=($OutputDir -replace '\\','/')
    } -Label '12_savefail_seed' -ExpectExit 0 -ExpectStatus @('success','success_with_warnings')
    Start-Sleep -Milliseconds 500
    # (b) mark the on-disk package read-only
    if (Test-Path $uasset) { (Get-Item $uasset).IsReadOnly = $true; $sfDetail.uasset = $uasset }
    else { $sfDetail.uasset_missing = $uasset }
    # (c) apply a real change to the throwaway (NOT work_on_copy) -> save must fail on the read-only file
    $r12 = Invoke-Live @{
      schema_version='1.0'; task_type='edit'; mode='editor_live'
      asset_paths=@($throwaway)
      execution=@{ read_only=$false; allow_edit=$true; run_preflight=$false; create_backup=$false }
      edit=@{ asset_path=$throwaway; mode='apply-and-verify'
        operations=@(@{ op_id='sf'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value='SaveFail Change' }) }
      output_dir=($OutputDir -replace '\\','/')
    } -Label '12_save_fail_apply' -ExpectExit 10 -ExpectStatus @('partial','failed')
    $er12 = Find-EditResult (Get-ReportDir $r12.manifest)
    $saveStatus = if ($er12 -and $er12.validation) { "$($er12.validation.save_status)" } else { '' }
    $sfDetail.save_status = $saveStatus
    $sfPass = ($r12.status -in @('partial','failed')) -and ($saveStatus -eq 'failed')
  } catch { $sfDetail.error = "$($_.Exception.Message)" }
  finally { if (Test-Path $uasset) { try { (Get-Item $uasset).IsReadOnly = $false } catch {} } }
  $Results += Pass-Case '12_save_fail_handling' $sfPass $sfDetail
} else { $Results += Skip-Case '12_save_fail_handling' 'needs hardened save path' }

# 13 Post-analyze diff — a successful edit changes a real property with a UNIQUE value; verify the
#    post_analyze IR was produced and the diff_report reflects the modified property.
if ($hardenedRuntime) {
  $uniq = "PostAnalyze $RunId"
  $r13 = Invoke-Live @{
    schema_version='1.0'; task_type='edit'; mode='editor_live'
    asset_paths=@($CopyAsset)
    execution=@{ read_only=$false; allow_edit=$true; run_preflight=$false; create_backup=$false }
    edit=@{ asset_path=$CopyAsset; mode='apply-and-verify'
      operations=@(@{ op_id='pa'; operation='set_widget_property'; widget='BP_Agent_NewCheckbox'; property='TextName'; value=$uniq }) }
    output_dir=($OutputDir -replace '\\','/')
  } -Label '13_post_analyze_diff' -ExpectExit 0 -ExpectStatus @('success','success_with_warnings')
  $rd13 = Get-ReportDir $r13.manifest
  $paIr = Join-Path $rd13 'post_analyze\blueprint_ir.json'
  $paExists = Test-Path $paIr
  $er13 = Find-EditResult $rd13
  $diffHasProp = $false
  if ($er13 -and $er13.diff -and $er13.diff.modified_widget_properties) {
    $diffHasProp = (@($er13.diff.modified_widget_properties).Count -gt 0)
  }
  # cross-check: the unique value should appear in the post-analyze IR text
  $paReflects = $false
  if ($paExists) { try { $paReflects = ([IO.File]::ReadAllText($paIr) -match [regex]::Escape($uniq)) } catch {} }
  $r13.pass = ([bool]$r13.pass -and $paExists -and ($diffHasProp -or $paReflects))
  $r13 | Add-Member -NotePropertyName post_analyze_ir -NotePropertyValue $paExists -Force
  $r13 | Add-Member -NotePropertyName diff_has_prop -NotePropertyValue $diffHasProp -Force
  $r13 | Add-Member -NotePropertyName post_analyze_reflects -NotePropertyValue $paReflects -Force
  $Results += $r13
} else { $Results += Skip-Case '13_post_analyze_diff' 'needs post_analyze' }

# 14 Editor-exit recovery — seed an ORPHANED request (non-terminal journal, no outbox marker) as if the
#   editor exited mid-apply, then run recover_scan: it must flag that request pending_editor_restart.
if (Has-Support 'recover_scan') {
  $orphanId = "req_orphan_$RunId"
  $orphanDir = Join-Path $OutputDir ("editor_live/" + $orphanId)
  New-Item -ItemType Directory -Force -Path $orphanDir | Out-Null
  $orphanJournal = @{
    schema_version = '1.0'
    entries = @(
      @{ timestamp=(Get-Date).ToUniversalTime().ToString('o'); request_id=$orphanId; phase='received';  status='ok' },
      @{ timestamp=(Get-Date).ToUniversalTime().ToString('o'); request_id=$orphanId; phase='applying';  status='running' }
    )
  }
  Write-Utf8NoBom (Join-Path $orphanDir 'request_journal.json') ($orphanJournal | ConvertTo-Json -Depth 8)
  $r14 = Invoke-Live @{ schema_version='1.0'; task_type='recover_scan'; mode='editor_live'; output_dir=($OutputDir -replace '\\','/') } -Label '14_editor_exit_recovery'
  $m14 = Read-Manifest $r14.manifest
  $recoveredList = if ($m14 -and $m14.recovered) { @($m14.recovered | ForEach-Object { "$_" }) } else { @() }
  $flagged = ($recoveredList -contains $orphanId)
  # cross-check: the orphan journal now ends with a pending_editor_restart entry
  $oj = Read-Manifest (Join-Path $orphanDir 'request_journal.json')
  $marked = $false
  if ($oj -and $oj.entries) { $marked = (@($oj.entries | Where-Object { "$($_.phase)" -eq 'pending_editor_restart' }).Count -gt 0) }
  $r14.pass = ([bool]$flagged -and $marked)
  $r14 | Add-Member -NotePropertyName recovered -NotePropertyValue $recoveredList -Force
  $r14 | Add-Member -NotePropertyName orphan_marked -NotePropertyValue $marked -Force
  $Results += $r14
} else { $Results += Skip-Case '14_editor_exit_recovery' 'needs recover_scan (plugin >= 0.4.8)' }

$Summary = [ordered]@{
  run_id = $RunId
  generated_at = (Get-Date).ToUniversalTime().ToString('o')
  project = $ProjectUProject
  plugin_version_detected = $pluginVer
  hardening_target = $HardeningVersion
  passed = @($Results | Where-Object { $_.pass -eq $true }).Count
  failed = @($Results | Where-Object { $_.pass -eq $false }).Count
  skipped = @($Results | Where-Object { $_.skipped }).Count
  cases = $Results
}
($Summary | ConvertTo-Json -Depth 8) | Out-File $ReportPath -Encoding utf8
Info "report: $ReportPath"
$Summary | Write-Output
if ($Summary.failed -gt 0) { exit 20 }
if ($Summary.skipped -gt 0) { exit 10 }
exit 0
