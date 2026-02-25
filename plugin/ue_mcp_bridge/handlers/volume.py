"""Volume handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False

VOLUME_CLASSES = {
    "trigger": "TriggerVolume",
    "blocking": "BlockingVolume",
    "painCausing": "PainCausingVolume",
    "killZ": "KillZVolume",
    "audio": "AudioVolume",
    "postProcess": "PostProcessVolume",
    "lightmassImportance": "LightmassImportanceVolume",
    "cameraBlocking": "CameraBlockingVolume",
    "navModifier": "NavModifierVolume",
    "physics": "PhysicsVolume",
}


def spawn_volume(params: dict) -> dict:
    volume_type = params.get("volumeType", "trigger")
    location = params.get("location", [0, 0, 0])
    scale = params.get("scale", [1, 1, 1])
    label = params.get("label", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    class_name = VOLUME_CLASSES.get(volume_type, volume_type)
    vol_class = getattr(unreal, class_name, None)
    if vol_class is None:
        raise ValueError(f"Volume class not found: {class_name}. Available: {list(VOLUME_CLASSES.keys())}")

    loc = unreal.Vector(location[0], location[1], location[2])
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(vol_class, loc)
    if actor is None:
        raise RuntimeError(f"Failed to spawn {class_name}")

    actor.set_actor_scale3d(unreal.Vector(scale[0], scale[1], scale[2]))
    if label:
        actor.set_actor_label(label)

    return {
        "volumeType": volume_type,
        "label": label or actor.get_actor_label(),
        "success": True,
    }


def list_volumes(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    volumes = []

    for actor in actors:
        if actor.get_class().get_name().endswith("Volume") or isinstance(actor, unreal.Volume):
            loc = actor.get_actor_location()
            scale = actor.get_actor_scale3d()
            volumes.append({
                "label": actor.get_actor_label(),
                "className": actor.get_class().get_name(),
                "location": {"x": loc.x, "y": loc.y, "z": loc.z},
                "scale": {"x": scale.x, "y": scale.y, "z": scale.z},
            })

    return {"count": len(volumes), "volumes": volumes}


def set_volume_properties(params: dict) -> dict:
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
        raise ValueError(f"Volume not found: {actor_label}")

    changes = []
    for key, val in params.items():
        if key in ("actorLabel",):
            continue
        try:
            target.set_editor_property(key, val)
            changes.append(key)
        except Exception:
            pass

    return {"actorLabel": actor_label, "changes": changes, "success": True}


HANDLERS = {
    "spawn_volume": spawn_volume,
    "list_volumes": list_volumes,
    "set_volume_properties": set_volume_properties,
}
