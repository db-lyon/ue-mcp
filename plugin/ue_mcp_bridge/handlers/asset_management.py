"""Asset management — duplicate, rename, move, delete assets."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def duplicate_asset(params: dict) -> dict:
    source = params.get("sourcePath", "")
    dest = params.get("destinationPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not source or not dest:
        raise ValueError("Both sourcePath and destinationPath are required")

    if not unreal.EditorAssetLibrary.does_asset_exist(source):
        raise ValueError(f"Source asset not found: {source}")

    result = unreal.EditorAssetLibrary.duplicate_asset(source, dest)
    if not result:
        raise RuntimeError(f"Failed to duplicate {source} -> {dest}")

    return {"sourcePath": source, "destinationPath": dest, "success": True}


def rename_asset(params: dict) -> dict:
    source = params.get("sourcePath", "")
    dest = params.get("destinationPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not source or not dest:
        raise ValueError("Both sourcePath and destinationPath are required")

    if not unreal.EditorAssetLibrary.does_asset_exist(source):
        raise ValueError(f"Asset not found: {source}")

    result = unreal.EditorAssetLibrary.rename_asset(source, dest)
    if not result:
        raise RuntimeError(f"Failed to rename {source} -> {dest}")

    return {"sourcePath": source, "destinationPath": dest, "success": True}


def move_asset(params: dict) -> dict:
    source = params.get("sourcePath", "")
    dest = params.get("destinationPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not source or not dest:
        raise ValueError("Both sourcePath and destinationPath are required")

    if not unreal.EditorAssetLibrary.does_asset_exist(source):
        raise ValueError(f"Asset not found: {source}")

    result = unreal.EditorAssetLibrary.rename_asset(source, dest)
    if not result:
        raise RuntimeError(f"Failed to move {source} -> {dest}")

    return {"sourcePath": source, "destinationPath": dest, "success": True}


def delete_asset(params: dict) -> dict:
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not asset_path:
        raise ValueError("path is required")

    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        raise ValueError(f"Asset not found: {asset_path}")

    result = unreal.EditorAssetLibrary.delete_asset(asset_path)
    if not result:
        raise RuntimeError(f"Failed to delete {asset_path} (may have references)")

    return {"path": asset_path, "success": True}


HANDLERS = {
    "duplicate_asset": duplicate_asset,
    "rename_asset": rename_asset,
    "move_asset": move_asset,
    "delete_asset": delete_asset,
}
