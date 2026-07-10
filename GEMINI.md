# GEMINI.md

This project uses a **shared local agent memory system**. This file is a thin entry adapter only —
it holds **no project facts**.

- Canonical agent protocol: **`AGENTS.md`** (see "§0. Local Agent Memory System").
- Canonical project memory: **`.agent/`**.

Do not duplicate project state here.

## Before non-trivial work, read hot context first
1. `.agent/PROJECT_STATE.md`
2. `.agent/TASKS.md`
3. `.agent/HANDOFF.md`
4. `.agent/CONTEXT_INDEX.md`

Then read only the relevant `.agent/domains/*.md` (and deep `docs/*.md`) per `.agent/CONTEXT_INDEX.md`.
Do not read `.agent/logs/` or `.agent/archive/` unless tracing history.

## After meaningful progress
Update the shared `.agent/` files per `AGENTS.md` §0. Keep hot files short.
