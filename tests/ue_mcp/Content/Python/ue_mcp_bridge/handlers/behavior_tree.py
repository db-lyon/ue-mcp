"""Behavior Tree handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def list_behavior_trees(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    ai_types = {"BehaviorTree", "BlackboardData"}
    results = []
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in ai_types:
            results.append({
                "path": str(ad.package_name),
                "name": str(ad.asset_name),
                "type": class_name,
            })

    return {"directory": directory, "count": len(results), "assets": results}


def get_behavior_tree_info(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    bt = unreal.EditorAssetLibrary.load_asset(asset_path)
    if bt is None:
        raise ValueError(f"BehaviorTree not found: {asset_path}")

    info = {
        "path": asset_path,
        "name": bt.get_name(),
        "className": bt.get_class().get_name(),
    }

    if hasattr(bt, "blackboard_asset"):
        bb = bt.get_editor_property("blackboard_asset")
        if bb:
            info["blackboardAsset"] = bb.get_path_name()
            keys = []
            if hasattr(bb, "keys"):
                for key in bb.get_editor_property("keys"):
                    keys.append({
                        "name": key.get_editor_property("entry_name") if hasattr(key, "entry_name") else str(key),
                    })
            info["blackboardKeys"] = keys

    return info


def create_blackboard(params: dict) -> dict:
    from ._util import resolve_asset_path, ensure_asset_cleared
    keys = params.get("keys", [])

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not hasattr(unreal, "BlackboardData"):
        raise RuntimeError("BlackboardData class not available")

    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/AI")
    if not asset_name:
        raise ValueError("path or name+packagePath is required")
    ensure_asset_cleared(full_path)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bb = asset_tools.create_asset(asset_name, package_path, unreal.BlackboardData, None)

    if bb is None:
        raise RuntimeError(f"Failed to create BlackboardData at {full_path}")

    return {"path": full_path, "success": True}


def create_behavior_tree(params: dict) -> dict:
    from ._util import resolve_asset_path, ensure_asset_cleared
    blackboard_path = params.get("blackboardPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not hasattr(unreal, "BehaviorTree"):
        raise RuntimeError("BehaviorTree class not available")

    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/AI")
    if not asset_name:
        raise ValueError("path or name+packagePath is required")
    ensure_asset_cleared(full_path)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bt = asset_tools.create_asset(asset_name, package_path, unreal.BehaviorTree, None)

    if bt is None:
        raise RuntimeError(f"Failed to create BehaviorTree at {full_path}")

    if blackboard_path:
        bb = unreal.EditorAssetLibrary.load_asset(blackboard_path)
        if bb:
            bt.set_editor_property("blackboard_asset", bb)

    return {"path": full_path, "blackboardPath": blackboard_path, "success": True}


HANDLERS = {
    "list_behavior_trees": list_behavior_trees,
    "get_behavior_tree_info": get_behavior_tree_info,
    "create_blackboard": create_blackboard,
    "create_behavior_tree": create_behavior_tree,
}
