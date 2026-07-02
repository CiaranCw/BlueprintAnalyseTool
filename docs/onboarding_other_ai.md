# Onboarding other-project AIs — make them discover & self-use the Blueprint Agent

## The problem
The agent's scripts/commandlets live outside a target game project. For the AI **built into** another
project (Claude Code, Codex, Cursor, Gemini CLI, ...) to notice the agent, understand it, and drive it
without a human explaining each time, the instructions must live in the files those tools **auto-read at
startup**.

## The mechanism (auto-read entry points)
| Tool | Auto-reads |
|---|---|
| Codex | `AGENTS.md` (agents.md standard) |
| Claude Code | `CLAUDE.md`, `.claude/commands/*.md` (slash commands) |
| Cursor | `.cursor/rules/*.mdc`, also `AGENTS.md` |
| Gemini CLI | `GEMINI.md` (AGENTS.md-style) |
| Any tool / script | a machine-readable descriptor `blueprint_agent.manifest.json` |

So we **inject** into the target project:
1. A delimited **MANAGED BLOCK** in `AGENTS.md` and `CLAUDE.md` — a concise, actionable "there is a
   Blueprint Agent; call it like this; start with `status`; decide; read the manifest".
2. `.cursor/rules/blueprint-agent.mdc` — a Cursor rule (description-scoped) with the same flow.
3. `.claude/commands/blueprint.md` — a Claude Code `/blueprint <request>` slash command.
4. `Tools/BlueprintAgent/blueprint_agent.manifest.json` — machine-readable descriptor (entry, task_types,
   modes, docs, outputs, safety) for programmatic discovery.
5. `Tools/BlueprintAgent/requests/*.json` — runnable example requests (status/warmup/analyze) pre-filled
   with the project's `.uproject`.
6. (copy mode) the agent's `scripts/`, `docs/`, and read-only `plugin/` copied under `Tools/BlueprintAgent/`
   so the project is self-contained.

## How to install (one command, into any project)
```powershell
.\scripts\install_agent_into_project.ps1 -TargetDir "<project root>" [-ProjectUProject "<...>.uproject"] [-AgentRoot "<this repo>"] [-Reference]
```
- default = **copy** (self-contained: entry becomes `Tools\BlueprintAgent\scripts\blueprint_agent.ps1`).
- `-Reference` = don't copy; point the docs at `-AgentRoot`'s absolute paths.
- **Idempotent + non-destructive**: AGENTS.md/CLAUDE.md keep your existing content; only the delimited
  managed block is created/replaced. Re-running never duplicates.

## The resulting discover → understand → use chain
1. The target AI boots, auto-reads AGENTS.md/CLAUDE.md/.cursor/.claude → **notices** the agent + the flow.
2. It runs the read-only `status` probe → reads `capability_state.json` → **understands** the current stage
   and what's possible (and whether a one-time `warmup` is needed).
3. It builds a `request.json` (examples + `request_schemas.md` provided) and calls
   `blueprint_agent.ps1 -RequestJson ...` → **uses** analyze/edit/create → reads `manifest.json`.

This makes the agent self-describing and self-drivable by any of those AIs, with no per-session human
explanation, while keeping all safety gates (read-only status/analyze, consent-gated warmup, backup/
rollback for edit, overwrite_policy for create).

## Notes
- Nothing is hardcoded to a specific project/engine; the block/descriptor reference the resolved entry
  and the target's own `.uproject`.
- Installing the onboarding kit does **not** perform warmup or touch blueprint assets; it only adds
  discoverability files (and, in copy mode, the tool folder). The target AI decides when to `warmup`
  (with user consent) to unlock native_full.
- Verified against a scratch project: AGENTS.md existing content preserved + managed block; CLAUDE.md,
  .cursor rule, .claude command, descriptor, and example requests generated; re-run replaced the block
  (single BEGIN marker).
