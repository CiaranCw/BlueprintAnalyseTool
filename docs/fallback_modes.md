# Fallback Modes — offline / python-partial / native-full

Three layered analysis modes with clear capability boundaries. `-Mode auto` walks them and records
`fallbacks_used` in `manifest.json`. No mode ever silent-fails; every run writes `manifest.json`.

## Mode 0 — offline_asset_scan
- **Runs**: pure PowerShell. No UE launch, no build, no project change.
- **Gets**: `.uproject` presence + `EngineAssociation` (version **or** GUID) → resolved `UERoot`;
  engine version + custom/source detection; asset path validity; `.uasset` package version header
  (PackageFileTag, FileVersionUE4/UE5); whether higher modes are feasible.
- **Does NOT get**: Graph/Node/Pin/Edge, variables, functions (no reliable full binary graph parse).
- **status**: `partial` (or `failed` if the file can't be located).

## Mode 1 — python_partial
- **Runs**: target UE `UnrealEditor-Cmd -run=pythonscript` (needs PythonScriptPlugin enabled/available)
  with `scripts/bp_analyze.py`. Read-only. **No build, no plugin install.**
- **Gets**: asset type (e.g. WidgetBlueprint), `BlueprintType`, parent class, generated class,
  implemented interfaces, and **dependencies** via AssetRegistry — all by reflection/registry.
- **Does NOT get**: full EdGraph traversal (node/pin/edge) — the UE Python API does not expose K2
  graph internals reliably. Always reported as `partial`.
- **Fails when**: PythonScriptPlugin not available, or project fails to load (missing plugins).

## Mode 2 — native_full
- **Runs**: our read-only C++ dumper commandlet (`-run=BPParserTestDump`, `FBPGenIRDumper`) inside the
  **target project + its engine**. For a foreign project the plugin is copied in (`-AllowPluginInstall`)
  and the editor target is incrementally built (`-AllowBuild`) — opt-in and removable.
- **Gets**: the **complete** IR — every Graph (event/function/macro/delegate), Node (class/title/guid/
  position/comment/flags), Pin (id/name/dir/type/default/links), Edge, plus variables/functions/macros/
  dispatchers/interfaces. Widget/Anim/plugin/custom K2 nodes preserved (unknown → kept, not dropped).
- **status**: `success`.

## Why the layering
Full, faithful Graph/Node/Pin/Edge structure **requires the asset to be loaded in its own engine
context** (correct engine version + all project plugins that define its parent class and custom nodes).
`offline` and `python_partial` are safe, non-invasive approximations for when that in-engine dumper
can't (or shouldn't yet) be run. The auto chain always leaves you with the best obtainable result.

## Observed example (real)
`WBP_Settings_Graphics` (project `AClient`, custom engine `AEngine` = UE 5.4.4):
- offline → `partial` (engine=5.4.4 custom, uasset ue5=1012).
- python_partial → `partial`, 0 errors: asset_type=WidgetBlueprint, parent=`/Script/AClient.RGSettingsGraphicsWidget`,
  generated=`WBP_Settings_Graphics_C`, 15 dependencies (setting sub-widgets + DLSS/Streamline libs).
- native_full → would need the plugin built into AClient against AEngine (user consent required).
