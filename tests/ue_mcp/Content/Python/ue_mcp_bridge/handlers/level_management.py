"""Level structure / management handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def get_current_level(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        return {"loaded": False}

    level_path = world.get_path_name()

    sublevels = []
    if hasattr(world, "get_streaming_levels"):
        for sl in world.get_streaming_levels():
            sublevels.append({
                "packageName": str(sl.get_world_asset_package_name()) if hasattr(sl, "get_world_asset_package_name") else str(sl),
                "isLoaded": sl.is_level_loaded() if hasattr(sl, "is_level_loaded") else None,
                "isVisible": sl.is_level_visible() if hasattr(sl, "is_level_visible") else None,
            })

    return {
        "levelPath": level_path,
        "worldName": world.get_name(),
        "sublevelCount": len(sublevels),
        "sublevels": sublevels,
    }


def load_level(params: dict) -> dict:
    level_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if hasattr(unreal, "LevelEditorSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if subsystem:
            success = subsystem.load_level(level_path)
            return {"path": level_path, "success": success}

    success = unreal.EditorLevelLibrary.load_level(level_path) if hasattr(unreal.EditorLevelLibrary, "load_level") else False
    return {"path": level_path, "success": success}


def save_current_level(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if hasattr(unreal, "LevelEditorSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if subsystem:
            success = subsystem.save_current_level()
            return {"success": success}

    unreal.EditorLevelLibrary.save_current_level() if hasattr(unreal.EditorLevelLibrary, "save_current_level") else None
    return {"success": True}


def list_levels(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    levels = []
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in ("World", "MapBuildDataRegistry"):
            if class_name == "World":
                levels.append({
                    "path": str(ad.package_name),
                    "name": str(ad.asset_name),
                })

    return {"directory": directory, "count": len(levels), "levels": levels}


def create_new_level(params: dict) -> dict:
    level_path = params.get("path", "")
    template = params.get("template", "Default")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if hasattr(unreal, "LevelEditorSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if subsystem and hasattr(subsystem, "new_level"):
            success = subsystem.new_level(level_path)
            return {"path": level_path, "success": success}

    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, f"MAP NEW")
    return {"path": level_path, "success": True, "note": "Created via console command"}


HANDLERS = {
    "get_current_level": get_current_level,
    "load_level": load_level,
    "save_current_level": save_current_level,
    "list_levels": list_levels,
    "create_new_level": create_new_level,
}
