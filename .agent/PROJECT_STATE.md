# PROJECT_STATE

Last Updated: 2026-07-10

## Current Goal
Deliver a reusable, **AI-callable Blueprint tool for UE 5.4** that can *understand*, *edit*, and
*create* Blueprints (incl. Widget Blueprints) across projects/engines, with machine-readable
outputs (JSON + DOT/Mermaid), safe/verifiable/reversible operations, and multi-tool onboarding.

## Current Status
- Done (implemented + verified on UE 5.4):
  - Analyze (understand): unified IR, modes `auto/native_full/python_partial/offline/editor_live`.
  - Create: Actor/Component/Interface + **Widget Blueprints** (hierarchy, slots, Details via
    reflection, custom UserWidgets, events + handler wiring + body templates, full-spec MVP).
  - Edit (atomic, backup/plan/diff/rollback): graph ops, variables, `set_parent_class`/reparent,
    and **widget-tree edits** (set widget/slot prop, add/remove/move widget, bind event on existing WBP).
  - `settable_properties` discovery + alias resolution; CanvasPanelSlot anchor-aware geometry guard.
  - Distribution: install / update-sync (managed blocks, versioning) / warmup / capability_state.
- In Progress: none active (last phase — anchor-guard — passed acceptance).
- Blocked: none.
- Not Started (candidates): real business-asset `plan-only` editing; complex full WBP spec (nested
  layouts / animations were explicitly deferred).

## Stable Facts
- Agent version: **0.4.6** (`blueprint_agent.version.json`). Latest commit at last update: `c5fd197`.
- Repo remote: GitHub `CiaranCw/BlueprintAnalyseTool`, branch `main`.
- Test project (stock engine): `E:\BPTestProject\BPTest` (UE 5.4.4 at `D:\software\UE\UE_5.4`).
- Real project (custom engine): `D:\Projects\AClient` (engine `D:\Projects\AEngine`, UE 5.4.4);
  currently `stage=native_ready`, plugin built, `warmup_required=false`.
- Plugin source of truth (dev): `bpparser_testgen/Plugins/BPParserTestGen/`. It is *synced into* a
  target project's `Plugins/` and built per-project (warmup). Editing repo source needs re-sync+rebuild.
- All agent-generated test assets go under `/Game/Generated/WBP_Agent_*` (never touch user assets).

## Important Files
- `blueprint_agent.version.json` — version + capability flags (source of truth for versioning).
- `scripts/blueprint_agent.ps1` — unified task dispatcher (analyze/edit/create/status/warmup/update).
- `bpparser_testgen/Plugins/BPParserTestGen/Source/BPParserTestGen/` — the C++ editor plugin.
- `docs/*.md` — deep contracts/schemas (see `CONTEXT_INDEX.md` / `domains/*`).

## Latest Known Good Result
- Date: 2026-07-10
- What: Existing-WBP edit acceptance re-run on `/Game/Generated` copy of `WBP_Settings_Graphics` (AClient).
- Result: all widget-tree edits succeeded; the previously-corrupting `ScrollBox_List Size` op is now
  **skipped** by the anchor guard (Offset Bottom stays 60, not 620); `unexpected_changes=0`, source untouched.
- Evidence: `EVIDENCE.md` → "Existing WBP edit acceptance"; AClient `Saved/BPParserAgentReports/accept_edit_v2b`.

## Open Questions
- [ ] Do we proceed to real-business-asset editing (plan-only first), or to complex full WBP spec generation?
- [ ] Should `edit`/`dump` wrappers all adopt the P18 post-write-crash tolerance (create + edit done; others?).
