"""
Play-in-Editor (PIE) handlers.
Control PIE sessions and read runtime values from the game world.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def pie_control(params: dict) -> dict:
    """Start, stop, or query PIE status."""
    action = params.get("action", "status")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if action == "start":
        unreal.EditorLevelLibrary.editor_play_simulate()
        return {"action": "start", "success": True}

    elif action == "stop":
        unreal.EditorLevelLibrary.editor_end_play()
        return {"action": "stop", "success": True}

    elif action == "status":
        is_playing = unreal.EditorLevelLibrary.is_in_play_in_editor()
        return {
            "action": "status",
            "isPlaying": is_playing,
        }

    else:
        raise ValueError(f"Unknown PIE action: {action}. Use 'start', 'stop', or 'status'.")


def get_runtime_value(params: dict) -> dict:
    """Get a runtime property value from an actor during PIE."""
    actor_path = params.get("actorPath", "")
    property_name = params.get("propertyName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not unreal.EditorLevelLibrary.is_in_play_in_editor():
        raise RuntimeError("PIE is not active. Start a PIE session first.")

    world = unreal.EditorLevelLibrary.get_game_world()
    if world is None:
        raise RuntimeError("No game world available")

    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    target = None

    for actor in actors:
        if actor.get_name() == actor_path or str(actor.get_path_name()) == actor_path:
            target = actor
            break

    if target is None:
        available = [actor.get_name() for actor in actors[:20]]
        raise ValueError(
            f"Actor '{actor_path}' not found. Available actors (first 20): {available}"
        )

    try:
        value = target.get_editor_property(property_name)
        serialized = _serialize_runtime_value(value)
        return {
            "actorPath": actor_path,
            "propertyName": property_name,
            "value": serialized,
            "type": type(value).__name__,
        }
    except Exception as e:
        raise RuntimeError(f"Failed to read property '{property_name}' on '{actor_path}': {e}")


def _serialize_runtime_value(val):
    """Convert runtime values to JSON-serializable form."""
    if val is None:
        return None
    if isinstance(val, (bool, int, float, str)):
        return val
    if isinstance(val, (list, tuple)):
        return [_serialize_runtime_value(v) for v in val]

    if HAS_UNREAL:
        if isinstance(val, unreal.Vector):
            return {"x": val.x, "y": val.y, "z": val.z}
        if isinstance(val, unreal.Rotator):
            return {"pitch": val.pitch, "yaw": val.yaw, "roll": val.roll}
        if isinstance(val, unreal.LinearColor):
            return {"r": val.r, "g": val.g, "b": val.b, "a": val.a}

    return str(val)


HANDLERS = {
    "pie_control": pie_control,
    "get_runtime_value": get_runtime_value,
}
