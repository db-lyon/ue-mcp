"""Shared utility helpers for bridge handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def to_vec3(val, default=(0, 0, 0)):
    """Convert a location/scale/extent to (x, y, z) tuple.
    Accepts: dict {x,y,z}, list/tuple [x,y,z], or None (returns default).
    """
    if val is None:
        return default
    if isinstance(val, dict):
        return (val.get("x", 0), val.get("y", 0), val.get("z", 0))
    if isinstance(val, (list, tuple)):
        return (val[0] if len(val) > 0 else 0,
                val[1] if len(val) > 1 else 0,
                val[2] if len(val) > 2 else 0)
    return default


def to_rot3(val, default=(0, 0, 0)):
    """Convert a rotation to (pitch, yaw, roll) tuple.
    Accepts: dict {pitch,yaw,roll} or [pitch,yaw,roll] or None.
    """
    if val is None:
        return default
    if isinstance(val, dict):
        return (val.get("pitch", 0), val.get("yaw", 0), val.get("roll", 0))
    if isinstance(val, (list, tuple)):
        return (val[0] if len(val) > 0 else 0,
                val[1] if len(val) > 1 else 0,
                val[2] if len(val) > 2 else 0)
    return default


def resolve_asset_path(params, default_dir="/Game"):
    """Resolve asset name and package path from params.
    Supports: full 'path', or 'name' + 'packagePath', or 'assetPath'.
    Returns: (asset_name, package_path, full_path)
    """
    path = params.get("path") or params.get("assetPath") or ""
    if path and "/" in path:
        parts = path.rsplit("/", 1)
        return parts[1], parts[0], path

    name = params.get("name", "") or params.get("assetName", "")
    pkg = params.get("packagePath", "") or default_dir
    if name:
        return name, pkg, f"{pkg}/{name}"

    if path:
        return path, default_dir, f"{default_dir}/{path}"

    return "", default_dir, ""


def ensure_asset_cleared(full_path):
    """Delete an existing asset if it exists, preventing overwrite dialogs."""
    if not HAS_UNREAL or not full_path:
        return
    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(full_path):
            return

        asset = unreal.EditorAssetLibrary.load_asset(full_path)
        if asset is not None:
            try:
                subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
                if subsystem is not None:
                    subsystem.close_all_editors_for_asset(asset)
            except Exception:
                pass

        unreal.EditorAssetLibrary.delete_asset(full_path)
    except Exception:
        pass
