# bp_analyze.py - read-only python_partial Blueprint extractor.
#
# Runs inside the TARGET project's UE via PythonScriptPlugin:
#   UnrealEditor-Cmd.exe <proj>.uproject -run=pythonscript -script="bp_analyze.py"
# Inputs are passed via environment variables (avoids -script arg quoting issues):
#   BPAT_ASSET = /Game/.../Asset   (package path)
#   BPAT_OUT   = <file path for partial_ir.json>
#   BPAT_LOG   = <file path for python log> (optional)
#
# It is strictly READ-ONLY: it loads the asset and reads reflected metadata + AssetRegistry
# data. The UE Python API cannot reliably traverse EdGraph node/pin/edge internals, so graph
# structure is intentionally left to native_full; this mode is always reported as "partial".
import os, json, traceback

def main():
    asset_path = os.environ.get('BPAT_ASSET', '')
    out_path = os.environ.get('BPAT_OUT', '')
    log_path = os.environ.get('BPAT_LOG', '')
    warnings, errors = [], []
    ir = {
        "schema_version": "1.0",
        "mode": "python_partial",
        "partial": True,
        "asset": {
            "asset_path": asset_path, "asset_name": "", "asset_type": "",
            "blueprint_class": "", "generated_class": "", "parent_class": "",
            "implemented_interfaces": [], "dependencies": []
        },
        "blueprint": {
            "variables": [], "functions": [], "macros": [], "event_dispatchers": [],
            "components": [], "timelines": [], "graphs": []
        },
        "graphs": [],
        "analysis": {
            "entry_points": [], "main_execution_paths": [], "data_flows": [],
            "external_calls": [], "risky_or_complex_nodes": [],
            "unsupported_or_unknown_nodes": [],
            "manual_check_required": [
                "EdGraph node/pin/edge structure is NOT available via the Python API; run native_full for the full graph IR."
            ]
        }
    }
    try:
        import unreal
    except Exception as e:
        errors.append("failed to import unreal module: %s" % e)
        _write(out_path, ir, warnings, errors); _log(log_path, warnings, errors); return

    try:
        ir["asset"]["asset_name"] = asset_path.rsplit('/', 1)[-1]
        pkg = asset_path.rsplit('.', 1)[0] if '.' in asset_path else asset_path

        # ---- AssetRegistry: tags (parent/generated/interfaces/type) + dependencies (no full load needed) ----
        try:
            ar = unreal.AssetRegistryHelpers.get_asset_registry()
            adata = ar.get_asset_by_object_path(asset_path)
            if not adata or not adata.is_valid():
                # try building an object path "<pkg>.<name>"
                name = pkg.rsplit('/', 1)[-1]
                adata = ar.get_asset_by_object_path("%s.%s" % (pkg, name))
            if adata and adata.is_valid():
                def tag(k):
                    try:
                        v = adata.get_tag_value(k)
                        return str(v) if v is not None else ""
                    except Exception:
                        return ""
                ir["asset"]["asset_type"] = str(adata.asset_class_path.asset_name) if hasattr(adata, "asset_class_path") else tag("Class")
                ir["asset"]["parent_class"] = tag("ParentClass")
                ir["asset"]["generated_class"] = tag("GeneratedClass")
                bt = tag("BlueprintType")
                if bt: ir["asset"]["blueprint_class"] = bt
                impl = tag("ImplementedInterfaces")
                if impl: ir["asset"]["implemented_interfaces"] = [impl]
            else:
                warnings.append("AssetData not found via AssetRegistry for %s" % asset_path)
        except Exception as e:
            warnings.append("AssetRegistry tag read failed: %s" % e)

        # dependencies
        try:
            ar = unreal.AssetRegistryHelpers.get_asset_registry()
            try:
                opts = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True)
                deps = ar.get_dependencies(pkg, opts)
            except Exception:
                deps = ar.get_dependencies(unreal.Name(pkg))
            if deps:
                ir["asset"]["dependencies"] = sorted(set(str(d) for d in deps))
        except Exception as e:
            warnings.append("dependency query failed: %s" % e)

        # ---- Load asset (read-only) for class / parent refinement ----
        try:
            bp = unreal.EditorAssetLibrary.load_asset(asset_path)
            if bp:
                ir["asset"]["asset_type"] = ir["asset"]["asset_type"] or bp.get_class().get_name()
                try:
                    gen = bp.get_editor_property('generated_class')
                    if gen: ir["asset"]["generated_class"] = ir["asset"]["generated_class"] or gen.get_name()
                except Exception: pass
                try:
                    par = bp.get_editor_property('parent_class')
                    if par: ir["asset"]["parent_class"] = ir["asset"]["parent_class"] or par.get_path_name()
                except Exception: pass
            else:
                warnings.append("EditorAssetLibrary.load_asset returned None")
        except Exception as e:
            warnings.append("load_asset failed (dependencies/plugins may be missing): %s" % e)
    except Exception as e:
        errors.append("python_partial fatal: %s\n%s" % (e, traceback.format_exc()))

    _write(out_path, ir, warnings, errors)
    _log(log_path, warnings, errors)


def _write(out_path, ir, warnings, errors):
    ir["warnings"] = warnings
    ir["errors"] = errors
    if out_path:
        try:
            with open(out_path, 'w', encoding='utf-8') as f:
                json.dump(ir, f, indent=2, ensure_ascii=False)
        except Exception as e:
            print("bp_analyze: cannot write %s: %s" % (out_path, e))


def _log(log_path, warnings, errors):
    if log_path:
        try:
            with open(log_path, 'w', encoding='utf-8') as f:
                f.write("warnings:\n" + "\n".join(warnings) + "\n\nerrors:\n" + "\n".join(errors))
        except Exception:
            pass


main()
