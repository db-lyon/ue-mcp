"""Niagara VFX handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def list_niagara_systems(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    systems = []
    for asset_data in assets:
        if str(asset_data.asset_class_path.asset_name) in ("NiagaraSystem", "NiagaraEmitter"):
            systems.append({
                "path": str(asset_data.package_name),
                "name": str(asset_data.asset_name),
                "type": str(asset_data.asset_class_path.asset_name),
            })

    return {"directory": directory, "count": len(systems), "systems": systems}


def get_niagara_info(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    system = unreal.EditorAssetLibrary.load_asset(asset_path)
    if system is None:
        raise ValueError(f"Niagara asset not found: {asset_path}")

    info = {
        "path": asset_path,
        "name": system.get_name(),
        "className": system.get_class().get_name(),
    }

    if hasattr(system, "get_emitter_handles"):
        emitters = []
        for handle in system.get_emitter_handles():
            emitters.append({
                "name": handle.get_name() if hasattr(handle, "get_name") else str(handle),
            })
        info["emitters"] = emitters

    return info


def spawn_niagara_at_location(params: dict) -> dict:
    system_path = params.get("systemPath", "")
    location = params.get("location", [0, 0, 0])
    rotation = params.get("rotation", [0, 0, 0])
    auto_destroy = params.get("autoDestroy", True)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    system = unreal.EditorAssetLibrary.load_asset(system_path)
    if system is None:
        raise ValueError(f"Niagara system not found: {system_path}")

    world = unreal.EditorLevelLibrary.get_editor_world()
    loc = unreal.Vector(location[0], location[1], location[2])
    rot = unreal.Rotator(rotation[0], rotation[1], rotation[2])

    if hasattr(unreal, "NiagaraFunctionLibrary"):
        comp = unreal.NiagaraFunctionLibrary.spawn_system_at_location(
            world, system, loc, rot, auto_destroy=auto_destroy
        )
        return {"systemPath": system_path, "success": comp is not None}

    return {"systemPath": system_path, "success": False, "note": "NiagaraFunctionLibrary not available"}


def set_niagara_parameter(params: dict) -> dict:
    actor_label = params.get("actorLabel", "")
    parameter_name = params.get("parameterName", "")
    value = params.get("value")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    target = None
    for a in actors:
        if a.get_actor_label() == actor_label or a.get_name() == actor_label:
            target = a
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_label}")

    comp = target.get_component_by_class(unreal.NiagaraComponent) if hasattr(unreal, "NiagaraComponent") else None
    if comp is None:
        raise ValueError(f"No NiagaraComponent found on actor '{actor_label}'")

    if isinstance(value, (int, float)):
        comp.set_variable_float(parameter_name, float(value))
    elif isinstance(value, list) and len(value) == 3:
        comp.set_variable_vec3(parameter_name, unreal.Vector(value[0], value[1], value[2]))
    elif isinstance(value, bool):
        comp.set_variable_bool(parameter_name, value)

    return {"actorLabel": actor_label, "parameterName": parameter_name, "success": True}


HANDLERS = {
    "list_niagara_systems": list_niagara_systems,
    "get_niagara_info": get_niagara_info,
    "spawn_niagara_at_location": spawn_niagara_at_location,
    "set_niagara_parameter": set_niagara_parameter,
}
