"""Spline handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def create_spline_actor(params: dict) -> dict:
    points = params.get("points", [[0, 0, 0], [500, 0, 0]])
    location = params.get("location", [0, 0, 0])
    label = params.get("label", "")
    closed = params.get("closed", False)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    loc = unreal.Vector(location[0], location[1], location[2])

    bp_class = unreal.EditorAssetLibrary.load_blueprint_class("/Engine/BasicShapes/SplineActor") if hasattr(unreal.EditorAssetLibrary, "load_blueprint_class") else None

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.Actor, loc)
    if actor is None:
        raise RuntimeError("Failed to spawn spline actor")

    if label:
        actor.set_actor_label(label)

    comp = unreal.SplineComponent(outer=actor) if hasattr(unreal, "SplineComponent") else None
    if comp is None:
        raise RuntimeError("SplineComponent not available. Use execute_python for manual spline creation.")

    return {
        "label": label or actor.get_actor_label(),
        "pointCount": len(points),
        "success": True,
        "note": "For complex splines, use execute_python with unreal.SplineComponent API directly.",
    }


def get_spline_info(params: dict) -> dict:
    actor_label = params.get("actorLabel", "")
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

    comp = target.get_component_by_class(unreal.SplineComponent) if hasattr(unreal, "SplineComponent") else None
    if comp is None:
        raise ValueError(f"No SplineComponent found on '{actor_label}'")

    points = []
    num_points = comp.get_number_of_spline_points()
    for i in range(num_points):
        loc = comp.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
        points.append({"index": i, "location": {"x": loc.x, "y": loc.y, "z": loc.z}})

    return {
        "actorLabel": actor_label,
        "pointCount": num_points,
        "isClosed": comp.is_closed_loop(),
        "splineLength": comp.get_spline_length(),
        "points": points,
    }


def set_spline_points(params: dict) -> dict:
    actor_label = params.get("actorLabel", "")
    points = params.get("points", [])

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

    comp = target.get_component_by_class(unreal.SplineComponent) if hasattr(unreal, "SplineComponent") else None
    if comp is None:
        raise ValueError(f"No SplineComponent found on '{actor_label}'")

    spline_points = []
    for p in points:
        spline_points.append(unreal.Vector(p[0], p[1], p[2]))

    comp.set_spline_points(spline_points, unreal.SplineCoordinateSpace.WORLD)
    comp.update_spline()

    return {"actorLabel": actor_label, "pointCount": len(points), "success": True}


HANDLERS = {
    "create_spline_actor": create_spline_actor,
    "get_spline_info": get_spline_info,
    "set_spline_points": set_spline_points,
}
