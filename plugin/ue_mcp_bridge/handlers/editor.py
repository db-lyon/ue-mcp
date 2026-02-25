"""
General editor operation handlers.
Console commands, asset saving, property modification.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def execute_command(params: dict) -> dict:
    """Execute a console command in the editor."""
    command = params.get("command", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, command)

    return {
        "command": command,
        "success": True,
    }


def set_property(params: dict) -> dict:
    """Set a property value on an object."""
    asset_path = params.get("path", "")
    object_name = params.get("objectName", "")
    property_name = params.get("propertyName", "")
    value = params.get("value")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Asset not found: {asset_path}")

    try:
        asset.set_editor_property(property_name, value)
        return {
            "path": asset_path,
            "propertyName": property_name,
            "success": True,
        }
    except Exception as e:
        raise RuntimeError(f"Failed to set property: {e}")


def save_asset(params: dict) -> dict:
    """Save an asset or all modified assets."""
    asset_path = params.get("path", "all")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if asset_path == "all":
        unreal.EditorAssetLibrary.save_loaded_assets()
        return {"success": True, "message": "All modified assets saved"}

    success = unreal.EditorAssetLibrary.save_asset(asset_path)
    return {
        "path": asset_path,
        "success": success,
    }


def undo(params: dict) -> dict:
    """Undo the last editor transaction."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    count = params.get("count", 1)
    results = []
    for _ in range(count):
        success = unreal.EditorLevelLibrary.editor_undo() if hasattr(unreal.EditorLevelLibrary, "editor_undo") else False
        if not success:
            try:
                unreal.SystemLibrary.execute_console_command(
                    unreal.EditorLevelLibrary.get_editor_world(), "TRANSACTION UNDO")
                success = True
            except Exception:
                success = False
        results.append(success)

    return {
        "undoCount": count,
        "results": results,
        "success": all(results),
    }


def redo(params: dict) -> dict:
    """Redo the last undone editor transaction."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    count = params.get("count", 1)
    results = []
    for _ in range(count):
        success = unreal.EditorLevelLibrary.editor_redo() if hasattr(unreal.EditorLevelLibrary, "editor_redo") else False
        if not success:
            try:
                unreal.SystemLibrary.execute_console_command(
                    unreal.EditorLevelLibrary.get_editor_world(), "TRANSACTION REDO")
                success = True
            except Exception:
                success = False
        results.append(success)

    return {
        "redoCount": count,
        "results": results,
        "success": all(results),
    }


def execute_python(params: dict) -> dict:
    """Execute arbitrary Python code inside the editor's Python environment."""
    code = params.get("code", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not code.strip():
        raise ValueError("No code provided")

    local_vars = {"unreal": unreal, "__result__": None}
    exec(code, {"unreal": unreal, "__builtins__": __builtins__}, local_vars)

    result = local_vars.get("__result__")
    if result is not None:
        if not isinstance(result, (dict, list, str, int, float, bool, type(None))):
            result = str(result)

    return {
        "success": True,
        "result": result,
    }


HANDLERS = {
    "execute_command": execute_command,
    "execute_python": execute_python,
    "set_property": set_property,
    "save_asset": save_asset,
    "undo": undo,
    "redo": redo,
}
