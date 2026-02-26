"""
Foliage handlers — read foliage types, sample instances, paint/erase foliage.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def list_foliage_types(params: dict) -> dict:
    """List all foliage types used in the level."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    ifa = None
    for actor in all_actors:
        if "InstancedFoliageActor" in actor.get_class().get_name() or "FoliageActor" in actor.get_class().get_name():
            ifa = actor
            break

    if ifa is None:
        return {"count": 0, "foliageTypes": [], "note": "No foliage actor found in the level"}

    types = []
    try:
        foliage_meshes = ifa.get_editor_property("foliage_meshes") if hasattr(ifa, "get_editor_property") else {}
        for key in dir(ifa):
            if "foliage" in key.lower() and "type" in key.lower():
                try:
                    ft = getattr(ifa, key)
                    if ft and hasattr(ft, "__iter__"):
                        for item in ft:
                            type_info = _read_foliage_type_basic(item)
                            if type_info:
                                types.append(type_info)
                except Exception:
                    continue
    except Exception:
        pass

    if not types:
        try:
            if hasattr(unreal, "FoliageEditorLibrary"):
                all_types = unreal.FoliageEditorLibrary.get_foliage_types() if hasattr(unreal.FoliageEditorLibrary, "get_foliage_types") else []
                for ft in all_types:
                    type_info = _read_foliage_type_basic(ft)
                    if type_info:
                        types.append(type_info)
        except Exception:
            pass

    if not types:
        try:
            props = {}
            for prop_name in dir(ifa):
                if prop_name.startswith("_"):
                    continue
                try:
                    val = getattr(ifa, prop_name)
                    if callable(val):
                        continue
                    if isinstance(val, (bool, int, float, str)):
                        props[prop_name] = val
                except Exception:
                    continue
            return {
                "count": 0,
                "foliageTypes": [],
                "note": "Could not enumerate foliage types. Actor properties available for inspection.",
                "actorClass": ifa.get_class().get_name(),
            }
        except Exception:
            pass

    return {
        "count": len(types),
        "foliageTypes": types,
    }


def get_foliage_type_settings(params: dict) -> dict:
    """Read full settings for a foliage type."""
    foliage_type_path = params.get("foliageTypePath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(foliage_type_path)
    if asset is None:
        raise ValueError(f"Foliage type not found: {foliage_type_path}")

    result = {
        "path": foliage_type_path,
        "class": asset.get_class().get_name(),
        "name": asset.get_name(),
    }

    for prop_name in ["mesh", "density", "density_adjustment", "radius",
                       "scaling_x", "scaling_y", "scaling_z",
                       "scale_min_x", "scale_max_x", "scale_min_y", "scale_max_y",
                       "scale_min_z", "scale_max_z",
                       "align_to_normal", "random_yaw", "random_pitch_angle",
                       "ground_slope_angle",
                       "height_min", "height_max",
                       "landscape_layers", "landscape_layer",
                       "cull_distance_min", "cull_distance_max",
                       "collision_with_world", "collision_radius", "collision_scale",
                       "cast_shadow", "receives_decals",
                       "min_initial_seed_to_spawn_count"]:
        try:
            val = asset.get_editor_property(prop_name)
            if hasattr(val, "get_path_name"):
                result[prop_name] = val.get_path_name()
            elif hasattr(val, "x"):
                result[prop_name] = {"x": val.x, "y": val.y}
                if hasattr(val, "z"):
                    result[prop_name]["z"] = val.z
            elif isinstance(val, (bool, int, float, str)):
                result[prop_name] = val
            else:
                result[prop_name] = str(val)
        except Exception:
            pass

    return result


def sample_foliage(params: dict) -> dict:
    """Query foliage instances in a region."""
    center = params.get("center", {"x": 0, "y": 0, "z": 0})
    radius = params.get("radius", 1000)
    foliage_type_filter = params.get("foliageType", None)
    limit = params.get("limit", 100)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    cx, cy, cz = center.get("x", 0), center.get("y", 0), center.get("z", 0)

    instances = []

    try:
        all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
        for actor in all_actors:
            if len(instances) >= limit:
                break

            class_name = actor.get_class().get_name()
            if "InstancedFoliageActor" not in class_name and "FoliageActor" not in class_name:
                continue

            try:
                ismc_comps = actor.get_components_by_class(unreal.InstancedStaticMeshComponent) if hasattr(unreal, "InstancedStaticMeshComponent") else []
                for comp in ismc_comps:
                    if len(instances) >= limit:
                        break

                    mesh_name = ""
                    try:
                        mesh = comp.get_editor_property("static_mesh")
                        mesh_name = mesh.get_path_name() if mesh else ""
                    except Exception:
                        pass

                    if foliage_type_filter and foliage_type_filter.lower() not in mesh_name.lower():
                        continue

                    count = comp.get_instance_count() if hasattr(comp, "get_instance_count") else 0
                    nearby = 0

                    for i in range(min(count, 10000)):
                        if len(instances) >= limit:
                            break
                        try:
                            transform = comp.get_instance_transform(i, world_space=True) if hasattr(comp, "get_instance_transform") else None
                            if transform:
                                loc = transform.translation
                                dx = loc.x - cx
                                dy = loc.y - cy
                                dist = (dx*dx + dy*dy) ** 0.5
                                if dist <= radius:
                                    nearby += 1
                                    instances.append({
                                        "mesh": mesh_name.split("/")[-1] if mesh_name else "Unknown",
                                        "location": {"x": loc.x, "y": loc.y, "z": loc.z},
                                        "distance": round(dist, 1),
                                    })
                        except Exception:
                            break
            except Exception:
                continue
    except Exception as e:
        return {"error": str(e)}

    return {
        "center": center,
        "radius": radius,
        "foliageTypeFilter": foliage_type_filter,
        "instanceCount": len(instances),
        "instances": instances,
    }


def paint_foliage(params: dict) -> dict:
    """Add foliage instances in a radius."""
    foliage_type = params.get("foliageType", "")
    location = params.get("location", {"x": 0, "y": 0, "z": 0})
    radius = params.get("radius", 500)
    density = params.get("density", 100)
    paint_layers = params.get("paintLayers", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    x = location.get("x", 0)
    y = location.get("y", 0)
    z = location.get("z", 0)

    if hasattr(unreal, "FoliageEditorLibrary"):
        try:
            lib = unreal.FoliageEditorLibrary
            loc = unreal.Vector(x, y, z)
            if hasattr(lib, "paint_foliage"):
                lib.paint_foliage(loc, radius, foliage_type, density)
                return {"success": True, "foliageType": foliage_type, "location": {"x": x, "y": y, "z": z}, "radius": radius}
        except Exception as e:
            raise RuntimeError(f"Foliage paint failed: {e}")

    return {
        "success": False,
        "error": "FoliageEditorLibrary not available. Foliage painting may require editor APIs not exposed to Python in this engine version.",
    }


def erase_foliage(params: dict) -> dict:
    """Remove foliage instances in a radius."""
    location = params.get("location", {"x": 0, "y": 0, "z": 0})
    radius = params.get("radius", 500)
    foliage_type_filter = params.get("foliageType", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    x = location.get("x", 0)
    y = location.get("y", 0)
    z = location.get("z", 0)

    if hasattr(unreal, "FoliageEditorLibrary"):
        try:
            lib = unreal.FoliageEditorLibrary
            loc = unreal.Vector(x, y, z)
            if hasattr(lib, "erase_foliage"):
                lib.erase_foliage(loc, radius, foliage_type_filter)
                return {"success": True, "location": {"x": x, "y": y, "z": z}, "radius": radius}
        except Exception as e:
            raise RuntimeError(f"Foliage erase failed: {e}")

    return {
        "success": False,
        "error": "FoliageEditorLibrary not available. Foliage erasure may require editor APIs not exposed to Python in this engine version.",
    }


def set_foliage_type_settings(params: dict) -> dict:
    """Modify settings on a foliage type (partial update)."""
    foliage_type_path = params.get("foliageTypePath", "")
    settings = params.get("settings", {})

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(foliage_type_path)
    if asset is None:
        raise ValueError(f"Foliage type not found: {foliage_type_path}")

    updated = []
    failed = []
    for key, value in settings.items():
        try:
            asset.set_editor_property(key, value)
            updated.append(key)
        except Exception as e:
            failed.append({"key": key, "error": str(e)})

    return {
        "success": len(failed) == 0,
        "foliageTypePath": foliage_type_path,
        "updated": updated,
        "failed": failed,
    }


def _read_foliage_type_basic(ft) -> dict | None:
    """Extract basic info from a foliage type."""
    try:
        info = {"name": ft.get_name(), "class": ft.get_class().get_name()}
        try:
            mesh = ft.get_editor_property("mesh")
            info["mesh"] = mesh.get_path_name() if mesh else None
        except Exception:
            pass
        try:
            info["density"] = ft.get_editor_property("density")
        except Exception:
            pass
        return info
    except Exception:
        return None


def create_foliage_type(params: dict) -> dict:
    """Create a new FoliageType asset from a StaticMesh."""
    asset_name = params.get("name", "FT_New")
    package_path = params.get("packagePath", "/Game/Foliage")
    mesh_path = params.get("meshPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    tools = unreal.AssetToolsHelpers.get_asset_tools()

    ft_class = None
    if hasattr(unreal, "FoliageType_InstancedStaticMesh"):
        ft_class = unreal.FoliageType_InstancedStaticMesh
    elif hasattr(unreal, "FoliageType"):
        ft_class = unreal.FoliageType

    if ft_class is None:
        raise RuntimeError("FoliageType class not available")

    factory = None
    for name in ("FoliageType_InstancedStaticMeshFactory", "FoliageTypeFactory"):
        if hasattr(unreal, name):
            factory = getattr(unreal, name)()
            break

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        asset = tools.create_asset(asset_name, package_path, ft_class, None)

    if asset is None:
        raise RuntimeError(f"Failed to create FoliageType at {package_path}/{asset_name}")

    if mesh_path:
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if mesh:
            try:
                asset.set_editor_property("mesh", mesh)
            except Exception:
                pass

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
        "meshPath": mesh_path,
    }


HANDLERS = {
    "list_foliage_types": list_foliage_types,
    "get_foliage_type_settings": get_foliage_type_settings,
    "sample_foliage": sample_foliage,
    "paint_foliage": paint_foliage,
    "erase_foliage": erase_foliage,
    "set_foliage_type_settings": set_foliage_type_settings,
    "create_foliage_type": create_foliage_type,
}
