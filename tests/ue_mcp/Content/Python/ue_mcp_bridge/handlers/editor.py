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

    if asset_path == "all" or not asset_path:
        try:
            unreal.EditorAssetLibrary.save_loaded_assets()
        except TypeError:
            unreal.EditorAssetLibrary.save_loaded_assets(True)
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
    import io
    import sys

    code = params.get("code", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not code.strip():
        raise ValueError("No code provided")

    capture = io.StringIO()
    old_stdout = sys.stdout
    sys.stdout = capture
    try:
        local_vars = {"unreal": unreal, "__result__": None}
        exec(code, {"unreal": unreal, "__builtins__": __builtins__}, local_vars)
    finally:
        sys.stdout = old_stdout

    result = local_vars.get("__result__")
    if result is not None:
        if not isinstance(result, (dict, list, str, int, float, bool, type(None))):
            result = str(result)

    output = capture.getvalue()
    response = {"success": True, "result": result}
    if output:
        response["output"] = output
    return response


def set_config(params: dict) -> dict:
    """Write a value to an INI config file (DefaultEngine.ini, DefaultGame.ini, etc.)."""
    config_file = params.get("configFile", "")
    section = params.get("section", "")
    key = params.get("key", "")
    value = params.get("value", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not section or not key:
        raise ValueError("section and key are required")

    import os

    if config_file and os.path.isabs(config_file):
        ini_path = config_file
    else:
        project_dir = os.environ.get("UE_PROJECT_DIR", "")
        if not project_dir:
            paths = unreal.Paths
            if hasattr(paths, "project_config_dir"):
                config_dir = paths.project_config_dir()
            else:
                raise RuntimeError("Cannot determine project config directory")
        else:
            config_dir = os.path.join(project_dir, "Config")

        if not config_file:
            config_file = "DefaultEngine.ini"
        ini_path = os.path.join(config_dir, config_file)

    lines = []
    if os.path.exists(ini_path):
        with open(ini_path, "r", encoding="utf-8") as f:
            lines = f.readlines()

    section_header = f"[{section}]"
    section_idx = -1
    key_idx = -1
    next_section_idx = len(lines)

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == section_header:
            section_idx = i
        elif section_idx >= 0 and stripped.startswith("[") and stripped.endswith("]"):
            next_section_idx = i
            break
        elif section_idx >= 0 and stripped.startswith(f"{key}="):
            key_idx = i

    new_line = f"{key}={value}\n"

    if key_idx >= 0:
        lines[key_idx] = new_line
    elif section_idx >= 0:
        lines.insert(section_idx + 1, new_line)
    else:
        if lines and not lines[-1].endswith("\n"):
            lines.append("\n")
        lines.append(f"\n{section_header}\n")
        lines.append(new_line)

    with open(ini_path, "w", encoding="utf-8") as f:
        f.writelines(lines)

    return {
        "configFile": os.path.basename(ini_path),
        "section": section,
        "key": key,
        "value": value,
        "success": True,
    }


HANDLERS = {
    "execute_command": execute_command,
    "execute_python": execute_python,
    "set_property": set_property,
    "save_asset": save_asset,
    "undo": undo,
    "redo": redo,
    "set_config": set_config,
}
