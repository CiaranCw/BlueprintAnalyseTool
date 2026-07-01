# Engine Compatibility — multi-version & custom/source UE

## Why one UE 5.4 plugin is not enough
A Blueprint's full Graph/Node/Pin/Edge structure lives in serialized `UEdGraph`/`UK2Node`/`UEdGraphPin`
objects whose classes (and the asset's parent class + any custom/plugin K2 nodes) are defined by the
**target project's engine + plugins**. To read them faithfully you must load the asset in **that**
engine context. Consequences:
- A project on a **custom/source engine** (EngineAssociation = GUID, e.g. `AEngine` UE 5.4.4) cannot be
  faithfully loaded by a stock launcher UE — module/plugin binaries and custom versions differ.
- A Widget/Anim/plugin-heavy blueprint's **parent class** is often a project C++ class (observed:
  `/Script/AClient.RGSettingsGraphicsWidget`) that a foreign engine lacks → load fails/partials.

Therefore full IR is only claimed by **native_full** (dumper running in the target engine); other modes
are explicit `partial` (see `docs/fallback_modes.md`).

## Engine resolution (implemented in analyze_blueprint.ps1)
- `EngineAssociation` is `"<major>.<minor>"` → launcher build: registry `HKLM\SOFTWARE\EpicGames\Unreal Engine\<v>`
  `InstalledDirectory`, then common install roots.
- `EngineAssociation` is a **GUID** → source/custom build: registry
  `HKCU/HKLM\SOFTWARE\Epic Games\Unreal Engine\Builds\<GUID>` → engine path.
- Version + custom detection from `Engine/Build/Build.version` and absence of `Engine/Build/InstalledBuild.txt`
  (or GUID association) → `is_custom_engine`.
- `-UERoot` always overrides auto-resolution.

## Version-sensitive surface (compat layer)
Version-risky APIs are centralized, not scattered through business logic:
- **`bpparser_testgen/.../Public/BPGenUECompat.h`** — engine version string, `SavePackage` overload,
  `FTopLevelAssetPath`/`ImplementNewInterface`, `MinimalAPI` node caveats, node-lifecycle ordering notes.
- **`bpparser_testgen/.../Private/BPGenIRDumper.cpp`** — the version-tolerant enumeration/serialization:
  Blueprint graph enumeration (`UbergraphPages`/`FunctionGraphs`/`MacroGraphs`/`DelegateSignatureGraphs`),
  Node GUID / Pin Id serialization, Pin type (category/subcategory/container) serialization, `LinkedTo`
  edge traversal, comment nodes. This is the concrete "BPATCompat" responsibility set; it is intentionally
  kept in one dumper rather than duplicated into a second module.

> Note: the prompt suggested a `Plugins/BlueprintAgentTools/Source/BPATCompat/` module. To avoid dead/
> duplicated code, those responsibilities are consolidated in `BPGenUECompat.h` + `BPGenIRDumper.cpp`
> (proven on UE 5.4.4). If a genuinely divergent engine (e.g. 5.3/5.5 with API breaks) is targeted, add
> the version-guarded shims **there**; the dumper already `#if ENGINE_MINOR_VERSION` where needed.

## Unknown / plugin / custom node policy (native_full)
1. Identify fully when possible.
2. Otherwise keep node **class name, title, pins, and links** — never drop.
3. Unknown node → emit its real `node_class`/title/pins (viewer can mark it Unknown/Plugin).
4. Unsupported graph → keep with best-effort `graph_type`.
5. All compat gaps → `logs/warnings.json` / `errors.json`.

## Cross-version checklist (what analyze_blueprint.ps1 verifies)
`.uproject` EngineAssociation → UERoot; `UnrealEditor-Cmd.exe` exists; source vs installed; project
custom Editor module present; plugin installed in project; plugin needs incremental build; plugin
distributed as **source** (compiles against the target engine, not prebuilt binaries).
