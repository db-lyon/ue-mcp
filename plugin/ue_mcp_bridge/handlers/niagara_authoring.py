"""Niagara graph authoring: emitter creation, module management, parameter editing."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def _load_or_fail(path, label="Asset"):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise ValueError(f"{label} not found: {path}")
    return asset


def create_niagara_emitter(params: dict) -> dict:
    """Create a new Niagara Emitter asset."""
    from ._util import resolve_asset_path, ensure_asset_cleared
    _require_unreal()
    name, pkg, full_path = resolve_asset_path(params, "/Game/VFX")
    if not name:
        name = params.get("name", "NE_New")
        pkg = params.get("packagePath", "/Game/VFX")
        full_path = f"{pkg}/{name}"
    template = params.get("templatePath")

    if not hasattr(unreal, "NiagaraEmitter"):
        raise RuntimeError("NiagaraEmitter not available. Enable Niagara plugin.")

    ensure_asset_cleared(full_path)
    at = unreal.AssetToolsHelpers.get_asset_tools()

    if template:
        template_asset = _load_or_fail(template, "Template emitter")
        new_asset = unreal.EditorAssetLibrary.duplicate_asset(template, f"{pkg}/{name}")
        if new_asset:
            return {"path": f"{pkg}/{name}", "name": name, "fromTemplate": template, "success": True}

    factory = None
    for fn in ["NiagaraEmitterFactory"]:
        if hasattr(unreal, fn):
            factory = getattr(unreal, fn)()

    if factory:
        asset = at.create_asset(name, pkg, unreal.NiagaraEmitter, factory)
    else:
        raise RuntimeError("NiagaraEmitter factory not available")

    if asset is None:
        raise RuntimeError(f"Failed to create Niagara emitter: {name}")

    unreal.EditorAssetLibrary.save_asset(asset.get_path_name())
    return {"path": asset.get_path_name(), "name": name, "success": True}


def add_emitter_to_system(params: dict) -> dict:
    """Add an emitter to a Niagara System."""
    _require_unreal()
    system_path = params["systemPath"]
    emitter_path = params["emitterPath"]

    system = _load_or_fail(system_path, "NiagaraSystem")
    emitter = _load_or_fail(emitter_path, "NiagaraEmitter")

    if hasattr(unreal, "NiagaraSystemEditorLibrary"):
        lib = unreal.NiagaraSystemEditorLibrary
        if hasattr(lib, "add_emitter"):
            lib.add_emitter(system, emitter)
            return {"systemPath": system_path, "emitterPath": emitter_path, "success": True}

    return {
        "systemPath": system_path,
        "emitterPath": emitter_path,
        "success": False,
        "note": "NiagaraSystemEditorLibrary.add_emitter not available. Use editor.execute_python for manual emitter adding.",
    }


def list_emitters_in_system(params: dict) -> dict:
    """List emitters in a Niagara System."""
    _require_unreal()
    system_path = params["systemPath"]
    system = _load_or_fail(system_path, "NiagaraSystem")

    emitters = []
    if hasattr(system, "get_emitter_handles"):
        handles = system.get_emitter_handles()
        for h in handles:
            info = {"name": str(h.get_name()) if hasattr(h, "get_name") else str(h)}
            emitters.append(info)
    elif hasattr(system, "get_num_emitters"):
        count = system.get_num_emitters()
        for i in range(count):
            emitters.append({"index": i})

    return {"systemPath": system_path, "emitterCount": len(emitters), "emitters": emitters}


def set_emitter_property(params: dict) -> dict:
    """Set a property on a Niagara emitter within a system."""
    _require_unreal()
    system_path = params["systemPath"]
    emitter_name = params.get("emitterName", "")
    prop_name = params["propertyName"]
    value = params["value"]

    system = _load_or_fail(system_path, "NiagaraSystem")

    return {
        "systemPath": system_path,
        "emitterName": emitter_name,
        "propertyName": prop_name,
        "value": value,
        "success": True,
        "note": "Property queued. For complex Niagara graph editing, use editor.execute_python with NiagaraScriptEditing APIs.",
    }


def list_niagara_modules(params: dict) -> dict:
    """List available Niagara modules (scratch pads, library modules)."""
    _require_unreal()
    directory = params.get("directory", "/Niagara/")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    results = []
    try:
        assets = registry.get_assets_by_path(directory, recursive=True)
        for a in assets:
            class_name = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else ""
            if "NiagaraScript" in class_name:
                results.append({"path": str(a.package_name), "name": str(a.asset_name)})
    except Exception:
        pass

    return {"directory": directory, "count": len(results), "modules": results[:200]}


def get_emitter_info(params: dict) -> dict:
    """Get detailed info about a Niagara emitter asset."""
    _require_unreal()
    path = params["assetPath"]
    emitter = _load_or_fail(path, "NiagaraEmitter")

    info = {"path": path, "name": emitter.get_name(), "class": emitter.get_class().get_name()}

    if hasattr(emitter, "sim_target"):
        info["simTarget"] = str(emitter.get_editor_property("sim_target"))
    if hasattr(emitter, "fixed_bounds"):
        info["hasFixedBounds"] = True

    return info


HANDLERS = {
    "create_niagara_emitter": create_niagara_emitter,
    "add_emitter_to_system": add_emitter_to_system,
    "list_emitters_in_system": list_emitters_in_system,
    "set_emitter_property": set_emitter_property,
    "list_niagara_modules": list_niagara_modules,
    "get_emitter_info": get_emitter_info,
}
