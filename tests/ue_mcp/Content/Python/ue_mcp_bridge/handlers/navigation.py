"""Navigation / NavMesh handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def rebuild_navigation(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")

    return {"success": True, "message": "Navigation rebuild triggered"}


def get_navmesh_info(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    nav_sys = unreal.NavigationSystemV1.get_navigation_system(
        unreal.EditorLevelLibrary.get_editor_world()
    ) if hasattr(unreal, "NavigationSystemV1") else None

    if nav_sys is None:
        return {"available": False, "note": "NavigationSystem not found"}

    info = {"available": True}

    if hasattr(nav_sys, "get_main_nav_data"):
        nav_data = nav_sys.get_main_nav_data()
        if nav_data:
            info["navDataClass"] = nav_data.get_class().get_name()

    return info


def project_point_to_navigation(params: dict) -> dict:
    from ._util import to_vec3
    location = params.get("location", [0, 0, 0])
    query_extent = params.get("queryExtent") or params.get("extent") or [100, 100, 100]

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    nav_sys = unreal.NavigationSystemV1.get_navigation_system(world) if hasattr(unreal, "NavigationSystemV1") else None

    if nav_sys is None:
        raise RuntimeError("NavigationSystem not available")

    lx, ly, lz = to_vec3(location)
    ex, ey, ez = to_vec3(query_extent, (100, 100, 100))
    loc = unreal.Vector(lx, ly, lz)
    extent = unreal.Vector(ex, ey, ez)

    projected = None
    try:
        projected = nav_sys.project_point_to_navigation(world, loc, nav_data=None, query_extent=extent)
    except TypeError:
        try:
            projected = nav_sys.project_point_to_navigation(loc, nav_data=None, query_extent=extent)
        except TypeError:
            try:
                projected = nav_sys.project_point_to_navigation(loc, extent)
            except Exception:
                pass

    return {
        "inputLocation": location,
        "projectedLocation": {"x": projected.x, "y": projected.y, "z": projected.z} if projected else None,
        "success": projected is not None,
    }


def spawn_nav_modifier_volume(params: dict) -> dict:
    from ._util import to_vec3
    location = params.get("location", [0, 0, 0])
    extent = params.get("extent", [200, 200, 200])
    label = params.get("label", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    lx, ly, lz = to_vec3(location)
    loc = unreal.Vector(lx, ly, lz)

    if hasattr(unreal, "NavModifierVolume"):
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.NavModifierVolume, loc)
    else:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.Volume, loc)

    if actor is None:
        raise RuntimeError("Failed to spawn nav modifier volume")

    if label:
        actor.set_actor_label(label)

    ex, ey, ez = to_vec3(extent, (200, 200, 200))
    actor.set_actor_scale3d(unreal.Vector(ex / 100, ey / 100, ez / 100))

    return {"label": label or actor.get_actor_label(), "success": True}


HANDLERS = {
    "rebuild_navigation": rebuild_navigation,
    "get_navmesh_info": get_navmesh_info,
    "project_point_to_navigation": project_point_to_navigation,
    "spawn_nav_modifier_volume": spawn_nav_modifier_volume,
}
