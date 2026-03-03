"""Skeleton / Physics Asset handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def get_skeleton_info(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Asset not found: {asset_path}")

    info = {
        "path": asset_path,
        "name": asset.get_name(),
        "className": asset.get_class().get_name(),
    }

    skeleton = None
    if hasattr(asset, "skeleton"):
        skeleton = asset.get_editor_property("skeleton")
    elif asset.get_class().get_name() == "Skeleton":
        skeleton = asset

    if skeleton:
        info["skeletonPath"] = skeleton.get_path_name()

    sockets = []
    if skeleton and hasattr(skeleton, "sockets"):
        for sock in skeleton.get_editor_property("sockets"):
            sockets.append({
                "name": str(sock.get_editor_property("socket_name")),
                "boneName": str(sock.get_editor_property("bone_name")),
            })
    info["sockets"] = sockets

    bones = []
    if hasattr(asset, "get_bone_name"):
        ref_skeleton = asset.get_editor_property("ref_skeleton") if hasattr(asset, "ref_skeleton") else None
    info["boneCount"] = len(bones) if bones else None

    return info


def list_sockets(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Asset not found: {asset_path}")

    skeleton = None
    if hasattr(asset, "skeleton"):
        skeleton = asset.get_editor_property("skeleton")
    elif asset.get_class().get_name() == "Skeleton":
        skeleton = asset

    if skeleton is None:
        raise ValueError(f"No skeleton associated with {asset_path}")

    sockets = []
    if hasattr(skeleton, "sockets"):
        for sock in skeleton.get_editor_property("sockets"):
            loc = sock.get_editor_property("relative_location") if hasattr(sock, "relative_location") else None
            rot = sock.get_editor_property("relative_rotation") if hasattr(sock, "relative_rotation") else None
            sockets.append({
                "name": str(sock.get_editor_property("socket_name")),
                "boneName": str(sock.get_editor_property("bone_name")),
                "location": {"x": loc.x, "y": loc.y, "z": loc.z} if loc else None,
                "rotation": {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll} if rot else None,
            })

    return {"path": asset_path, "count": len(sockets), "sockets": sockets}


def list_skeletal_meshes(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    meshes = []
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in ("SkeletalMesh", "Skeleton", "PhysicsAsset"):
            meshes.append({
                "path": str(ad.package_name),
                "name": str(ad.asset_name),
                "type": class_name,
            })

    return {"directory": directory, "count": len(meshes), "assets": meshes}


def get_physics_asset_info(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    pa = unreal.EditorAssetLibrary.load_asset(asset_path)
    if pa is None:
        raise ValueError(f"PhysicsAsset not found: {asset_path}")

    info = {
        "path": asset_path,
        "name": pa.get_name(),
        "className": pa.get_class().get_name(),
    }

    if hasattr(pa, "skeletal_body_setups"):
        bodies = []
        for body in pa.get_editor_property("skeletal_body_setups"):
            bodies.append({
                "boneName": str(body.get_editor_property("bone_name")) if hasattr(body, "bone_name") else None,
            })
        info["bodies"] = bodies
        info["bodyCount"] = len(bodies)

    return info


HANDLERS = {
    "get_skeleton_info": get_skeleton_info,
    "list_sockets": list_sockets,
    "list_skeletal_meshes": list_skeletal_meshes,
    "get_physics_asset_info": get_physics_asset_info,
}
