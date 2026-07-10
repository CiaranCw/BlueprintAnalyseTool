# .agent — Canonical Local Agent Memory

This directory is the **single source of project continuity** for any agent/IDE tool
(Codex, Cursor, Claude Code, Gemini, …) working on this repository.

Tool entry files (`AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `.cursor/rules/*.mdc`) are **thin
adapters** — they only point here. **Project facts, state, tasks, decisions, and evidence
live here, not in the entry files.** Never duplicate project state into an entry file.

> Scope: this memory is about **developing this repo** (the Blueprint Agent tooling itself).
> It is separate from `Tools/BlueprintAgent`, which is what gets *installed into target UE projects*.

## Layers (read in this order of restraint)

### Hot — read every time, before non-trivial work
- `PROJECT_STATE.md` — current goal, status, stable facts, key files, last good result, open questions
- `TASKS.md` — active task + prioritized queue
- `HANDOFF.md` — last agent → next agent handoff (overwrite, not append)
- `CONTEXT_INDEX.md` — which files to read for a given kind of task

### Warm — read only when the task needs it
- `domains/*.md` — per-area maps (point to deep `docs/*.md` + source; don't duplicate)
- `DECISIONS.md` — durable technical decisions (ADR style)
- `EVIDENCE.md` — index (paths + summaries) of code / logs / test results

### Cold — read only when tracing history
- `logs/YYYY-MM-DD-session.md` — per-session process notes
- `archive/` — superseded plans / obsolete conclusions

## Rules
- Read hot first; warm only when relevant; cold only when historical tracing is needed.
- Keep hot files short: stable conclusions, exact paths/commands, results, open questions,
  next actions, known failed attempts. **No long logs / source / reasoning chains here.**
- Source-of-truth priority: current code & tests → latest reproducible run → `PROJECT_STATE.md`
  → `TASKS.md` → `domains/*.md` → `logs/` → `archive/`. If memory conflicts with code/tests,
  trust code/tests and update the memory.
- Deep stable references already live in `../docs/*.md`; `domains/*.md` link to them.
- See `../AGENTS.md` for the full collaboration protocol and per-session update rule.
