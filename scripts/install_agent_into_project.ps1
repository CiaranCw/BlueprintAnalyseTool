<#
  install_agent_into_project.ps1 - make the Blueprint Agent DISCOVERABLE and SELF-USABLE by the AI
  built into another project (Claude Code, Codex, Cursor, Gemini, ...).

  It writes the instruction files those tools auto-read, plus a machine-readable descriptor, a version
  file, recorded content hashes (for later update conflict detection), and example requests.

  Idempotent + non-destructive: AGENTS.md / CLAUDE.md get a delimited MANAGED BLOCK (replaced on re-run,
  never clobbering surrounding content). The .cursor/.claude/tool files are ours and are overwritten.
  Managed content + version/hash logic live in scripts/agent_sync_lib.ps1 (shared with update).

  Usage:
    .\install_agent_into_project.ps1 -TargetDir "<project root>" [-ProjectUProject "<...>.uproject"] [-AgentRoot "<agent repo>"] [-Reference]

  Default: copies scripts/docs/plugin into <TargetDir>/Tools/BlueprintAgent (self-contained).
  -Reference: instead point the docs at -AgentRoot's absolute paths (no copy).
  Exit codes: 0 ok, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $TargetDir,
  [string] $ProjectUProject = "",
  [string] $AgentRoot = "",
  [switch] $Reference,
  [switch] $NoBackup   # skip backing up files we are about to (over)write (default: back up)
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'agent_sync_lib.ps1')

if (-not (Test-Path $TargetDir)) { Write-Error "TargetDir not found: $TargetDir"; exit 30 }
if ([string]::IsNullOrWhiteSpace($AgentRoot)) { $AgentRoot = Split-Path $PSScriptRoot -Parent }
if (-not (Test-Path (Join-Path $AgentRoot 'scripts\blueprint_agent.ps1'))) { Write-Error "AgentRoot invalid (no scripts/blueprint_agent.ps1): $AgentRoot"; exit 30 }
if ([string]::IsNullOrWhiteSpace($ProjectUProject)) {
  $up = Get-ChildItem $TargetDir -Filter *.uproject -EA SilentlyContinue | Select-Object -First 1
  if ($up) { $ProjectUProject = $up.FullName }
}
$uprojForDoc = if ($ProjectUProject) { $ProjectUProject } else { "<PATH>/YourProject.uproject" }
# JSON forbids single backslashes; UE accepts forward slashes on all platforms. Normalize so every
# generated example/doc path is valid JSON out of the box (fixes the "unrecognized escape" trap).
$uprojForDoc = $uprojForDoc -replace '\\','/'

$ver = Read-AgentVersion $AgentRoot
$toolsDir = Join-Path $TargetDir 'Tools\BlueprintAgent'

# ---- backup anything we are about to (over)write / mirror (reversible first install) ----
# install (unlike update) previously had NO backup: overwriting our own files, and especially the /MIR
# mirror of Tools/BlueprintAgent (which deletes extra files), could not be undone. Back up first.
$backedUp = New-Object System.Collections.ArrayList
if (-not $NoBackup) {
  $ts = Get-Date -Format 'yyyyMMdd_HHmmss'
  $backupDir = Join-Path $TargetDir "Saved\BPParserAgentReports\install\$ts\backup"
  function Backup-File([string]$path,[string]$asName){
    if (Test-Path $path -PathType Leaf) {
      New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
      Copy-Item $path (Join-Path $backupDir $asName) -Force
      [void]$backedUp.Add($asName)
    }
  }
  function Backup-Dir([string]$path,[string]$asName){
    if (Test-Path $path -PathType Container) {
      $d = Join-Path $backupDir $asName; New-Item -ItemType Directory -Force -Path $d | Out-Null
      robocopy $path $d /E /NFL /NDL /NJH /NJS /NP | Out-Null
      [void]$backedUp.Add("$asName\")
    }
  }
  Backup-File (Join-Path $TargetDir 'AGENTS.md') 'AGENTS.md'
  Backup-File (Join-Path $TargetDir 'CLAUDE.md') 'CLAUDE.md'
  Backup-File (Join-Path $TargetDir 'GEMINI.md') 'GEMINI.md'
  Backup-File (Join-Path $TargetDir '.cursor\rules\blueprint-agent.mdc') 'cursor_rules_blueprint-agent.mdc'
  Backup-File (Join-Path $TargetDir '.claude\commands\blueprint.md') 'claude_commands_blueprint.md'
  Backup-Dir  $toolsDir 'Tools_BlueprintAgent'
  if ($backedUp.Count -gt 0) { Write-Host ("Backed up {0} existing item(s) -> {1}" -f $backedUp.Count,$backupDir) -ForegroundColor DarkGray }
}

# ---- resolve entry + plugin-source paths (copy vs reference) ----
if ($Reference) {
  $entry     = Join-Path $AgentRoot 'scripts\blueprint_agent.ps1'
  $docsPath  = Join-Path $AgentRoot 'docs'
  $pluginSrc = Join-Path $AgentRoot 'bpparser_testgen\Plugins\BPParserTestGen'
} else {
  New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
  [void](Copy-ManagedTree (Join-Path $AgentRoot 'scripts') (Join-Path $toolsDir 'scripts'))
  [void](Copy-ManagedTree (Join-Path $AgentRoot 'docs')    (Join-Path $toolsDir 'docs'))
  [void](Copy-ManagedTree (Join-Path $AgentRoot 'bpparser_testgen\Plugins\BPParserTestGen') (Join-Path $toolsDir 'plugin\BPParserTestGen'))
  $entry     = 'Tools\BlueprintAgent\scripts\blueprint_agent.ps1'
  $docsPath  = 'Tools/BlueprintAgent/docs'
  $pluginSrc = 'Tools\BlueprintAgent\plugin\BPParserTestGen'
}

$written = New-Object System.Collections.ArrayList

# ---- managed block (AGENTS.md + CLAUDE.md always; GEMINI.md only if the project already uses it) ----
$block = Get-BABlock $entry $docsPath $uprojForDoc $pluginSrc
[void]$written.Add(@{ file='AGENTS.md'; action=(Upsert-ManagedBlock (Join-Path $TargetDir 'AGENTS.md') $block) })
[void]$written.Add(@{ file='CLAUDE.md'; action=(Upsert-ManagedBlock (Join-Path $TargetDir 'CLAUDE.md') $block) })
if (Test-Path (Join-Path $TargetDir 'GEMINI.md')) { [void]$written.Add(@{ file='GEMINI.md'; action=(Upsert-ManagedBlock (Join-Path $TargetDir 'GEMINI.md') $block) }) }

# ---- Cursor rule ----
$cursorDir = Join-Path $TargetDir '.cursor\rules'; New-Item -ItemType Directory -Force -Path $cursorDir | Out-Null
Write-Utf8NoBom (Join-Path $cursorDir 'blueprint-agent.mdc') (Get-BAMdc $entry $docsPath $uprojForDoc $pluginSrc)
[void]$written.Add(@{ file='.cursor/rules/blueprint-agent.mdc'; action='written' })

# ---- Claude Code slash command ----
$claudeDir = Join-Path $TargetDir '.claude\commands'; New-Item -ItemType Directory -Force -Path $claudeDir | Out-Null
Write-Utf8NoBom (Join-Path $claudeDir 'blueprint.md') (Get-BACommand $entry $docsPath $uprojForDoc $pluginSrc)
[void]$written.Add(@{ file='.claude/commands/blueprint.md'; action='written' })

# ---- machine-readable descriptor (+ install record) ----
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
$descriptor = Get-BADescriptor $entry $docsPath $pluginSrc $ver
$descriptor.install = [ordered]@{
  installed_agent_version = $(if($ver){"$($ver.agent_version)"}else{''})
  installed_agent_commit  = $(if($ver){"$($ver.agent_commit)"}else{''})
  install_mode            = $(if($Reference){'reference'}else{'copy'})
  source_agent_root       = ($AgentRoot -replace '\\','/')
  last_update_time        = (Get-Date).ToUniversalTime().ToString('o')
  last_update_status      = 'success'
  warmup_required_after_update = $true
}
Write-Utf8NoBom (Join-Path $toolsDir 'blueprint_agent.manifest.json') ($descriptor | ConvertTo-Json -Depth 10)
[void]$written.Add(@{ file='Tools/BlueprintAgent/blueprint_agent.manifest.json'; action='written' })

# ---- version file (copy of source version, with resolved commit) ----
if ($ver) {
  Write-Utf8NoBom (Join-Path $toolsDir 'blueprint_agent.version.json') ($ver | ConvertTo-Json -Depth 8)
  [void]$written.Add(@{ file='Tools/BlueprintAgent/blueprint_agent.version.json'; action='written' })
}

# ---- example requests (with the resolved uproject) ----
$reqDir = Join-Path $toolsDir 'requests'; New-Item -ItemType Directory -Force -Path $reqDir | Out-Null
$mk = { param($obj,$name) Write-Utf8NoBom (Join-Path $reqDir $name) ($obj | ConvertTo-Json -Depth 20) }
& $mk (@{ schema_version='1.0'; task_type='status'; project=@{ uproject=$uprojForDoc } }) 'status.template.json'
& $mk (@{ schema_version='1.0'; task_type='warmup'; project=@{ uproject=$uprojForDoc; engine_policy=@{ allow_incremental_compile=$true } }; request=@{ smoke_asset_path='/Game/...' } }) 'warmup.template.json'
& $mk (@{ schema_version='1.0'; task_type='analyze'; project=@{ uproject=$uprojForDoc }; execution=@{ mode='auto' }; request=@{ asset_paths=@('/Game/UI/WBP_Example') } }) 'analyze.template.json'
& $mk (@{ schema_version='1.0'; task_type='update'; project=@{ uproject=$uprojForDoc }; request=@{ source_agent_root=($AgentRoot -replace '\\','/') } }) 'update.template.json'
[void]$written.Add(@{ file='Tools/BlueprintAgent/requests/*.template.json'; action='written' })

# ---- record managed-content hashes (baseline for future update conflict detection) ----
$hashes = Get-TargetManagedHashes $TargetDir
$syncState = [ordered]@{
  schema_version='1.0'
  installed_agent_version=$(if($ver){"$($ver.agent_version)"}else{''})
  installed_agent_commit=$(if($ver){"$($ver.agent_commit)"}else{''})
  install_mode=$(if($Reference){'reference'}else{'copy'})
  source_agent_root=($AgentRoot -replace '\\','/')
  installed_at=(Get-Date).ToUniversalTime().ToString('o')
  last_update_time=(Get-Date).ToUniversalTime().ToString('o')
  last_update_status='success'
  warmup_required_after_update=$true
  managed_hashes=$hashes
}
Write-Utf8NoBom (Get-SyncStatePath $TargetDir) ($syncState | ConvertTo-Json -Depth 8)
[void]$written.Add(@{ file='Tools/BlueprintAgent/.agent_sync/sync_state.json'; action='written' })

# ---- install summary ----
$summary = [ordered]@{ schema_version='1.0'; installed_at=(Get-Date).ToUniversalTime().ToString('o');
  target_dir=$TargetDir; agent_root=$AgentRoot; mode=$(if($Reference){'reference'}else{'copy'});
  agent_version=$(if($ver){"$($ver.agent_version)"}else{''}); entry=$entry; uproject=$ProjectUProject;
  backup_dir=$(if($backedUp.Count -gt 0){($backupDir -replace '\\','/')}else{''}); backups=@($backedUp); files=@($written) }
Write-Utf8NoBom (Join-Path $toolsDir 'onboarding_install.json') ($summary | ConvertTo-Json -Depth 8)

Write-Host "== Blueprint Agent onboarding installed ==" -ForegroundColor Green
$written | ForEach-Object { Write-Host ("  {0,-10} {1}" -f $_.action,$_.file) }
Write-Host "entry: $entry  version: $(if($ver){$ver.agent_version}else{'?'})"
Write-Host "Other-project AIs (Claude Code/Codex/Cursor) will now auto-discover the agent via AGENTS.md / CLAUDE.md / .cursor / .claude."
exit 0
