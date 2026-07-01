# Integration Guide — temporarily attaching the analyzer to another UE project

How to run full `native_full` analysis on a blueprint in an arbitrary UE project (including
custom/source engines), with read-only guarantees and a clean removal path.

## Prerequisites
- The target `.uproject` path.
- The engine it uses (auto-resolved from `EngineAssociation`; override with `-UERoot`). For a custom
  source build, point `-UERoot` at that engine root (e.g. `D:\Projects\AEngine`).
- Consent to (a) add a plugin to the project and (b) incrementally build the project's Editor target.

## One-shot (auto handles install+build when allowed)
```powershell
.\scripts\analyze_blueprint.ps1 `
  -UERoot "<engine root>" `
  -ProjectUProject "<...>.uproject" `
  -AssetPath "/Game/UI/WBP_X" `
  -OutputDir "<project>\Saved\BPParserAgentReports" `
  -Mode native-full -AllowPluginInstall -AllowBuild
```

## Manual steps (equivalent)
1. **Install (source form)** — copies `BPParserTestGen` into `<project>/Plugins/` (source only; excludes
   Binaries/Intermediate so it compiles against the target engine):
   ```powershell
   .\scripts\install_project_plugin.ps1 -ProjectUProject "<...>.uproject"
   ```
2. **Build (incremental)** — builds `<Project>Editor` with the target engine's UBT. The project's game
   modules are usually already built, so this mainly compiles our plugin + relinks the editor DLL:
   ```powershell
   .\scripts\build_project_plugin.ps1 -UERoot "<engine root>" -ProjectUProject "<...>.uproject"
   ```
3. **Analyze (read-only commandlet)**:
   ```powershell
   .\scripts\analyze_blueprint.ps1 -UERoot "<engine root>" -ProjectUProject "<...>.uproject" `
     -AssetPath "/Game/UI/WBP_X" -Mode native-full
   ```

## Read-only guarantees
- The plugin's analysis path only **loads and dumps**; it never compiles/saves the target asset.
- No blueprint content is modified. Outputs go to `<project>/Saved/BPParserAgentReports/`.
- Adding the plugin + building is a **project/tooling** change, not an asset change, and is opt-in.

## Removal
```powershell
Remove-Item "<project>\Plugins\BPParserTestGen" -Recurse -Force
# then regenerate project files / rebuild the editor if you want the plugin gone from the build
```
`Saved/BPParserAgentReports/` outputs can be deleted freely.

## If you cannot / will not build
Use non-invasive modes:
```powershell
.\scripts\analyze_blueprint.ps1 -UERoot "<engine>" -ProjectUProject "<...>.uproject" -AssetPath "/Game/UI/WBP_X" -Mode python-partial
# or -Mode offline
```
These yield `partial` results (no full EdGraph) — see `docs/fallback_modes.md`.

## Custom/source engine notes
- Distribute the plugin as **source**; never ship prebuilt binaries built against a different engine.
- The engine version should match the plugin's target (proven on UE 5.4.4, incl. source builds). Minor
  version drift is centralized in `BPGenUECompat.h` (see `docs/engine_compatibility.md`).
- Building requires the target engine's full C++ toolchain (the same used to build the project).
