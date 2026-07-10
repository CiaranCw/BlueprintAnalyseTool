# CLAUDE.md

This project uses a **shared local agent memory system**. This file is a thin entry adapter only —
it holds **no project facts**.

- The canonical agent protocol is **`AGENTS.md`** (see its "§0. Local Agent Memory System").
- The canonical project memory is under **`.agent/`**.

Do not duplicate project state here. Do not load the entire `.agent/` directory into startup context.

## Before non-trivial work, read hot context first
1. `.agent/PROJECT_STATE.md`
2. `.agent/TASKS.md`
3. `.agent/HANDOFF.md`
4. `.agent/CONTEXT_INDEX.md`

Then read only the relevant `.agent/domains/*.md` (and deep `docs/*.md`) per `.agent/CONTEXT_INDEX.md`.
Do not read `.agent/logs/` or `.agent/archive/` unless tracing history.

## After meaningful progress
Update the shared `.agent/` memory files as defined in `AGENTS.md` §0 (Update Rule). Keep hot files short.

> Note: the domain-specific Blueprint-agent working rules also live in `AGENTS.md`; follow them.
