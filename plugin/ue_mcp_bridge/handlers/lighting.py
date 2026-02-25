"""Lighting handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False

LIGHT_CLASSES = {
    "point": "PointLight",
    "spot": "SpotLight",
    "directional": "DirectionalLight",
    "rect": "RectLight",
    "sky": "SkyLight",
}


def spawn_light(params: dict) -> dict:
    light_type = params.get("lightType", "point").lower()
    location = params.get("location", [0, 0, 0])
    rotation = params.get("rotation", [0, 0, 0])
    intensity = params.get("intensity")
    color = params.get("color")
    label = params.get("label", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    class_name = LIGHT_CLASSES.get(light_type)
    if class_name is None:
        raise ValueError(f"Unknown light type: {light_type}. Options: {list(LIGHT_CLASSES.keys())}")

    loc = unreal.Vector(location[0], location[1], location[2])
    rot = unreal.Rotator(rotation[0], rotation[1], rotation[2])

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        getattr(unreal, class_name), loc, rot
    )

    if actor is None:
        raise RuntimeError(f"Failed to spawn {class_name}")

    if label:
        actor.set_actor_label(label)

    comp = actor.get_component_by_class(unreal.LightComponent) if hasattr(unreal, "LightComponent") else None
    if comp:
        if intensity is not None:
            comp.set_editor_property("intensity", float(intensity))
        if color and len(color) >= 3:
            comp.set_editor_property("light_color", unreal.Color(int(color[0]), int(color[1]), int(color[2])))

    return {
        "lightType": light_type,
        "label": label or actor.get_actor_label(),
        "success": True,
    }


def set_light_properties(params: dict) -> dict:
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

    comp = target.get_component_by_class(unreal.LightComponent) if hasattr(unreal, "LightComponent") else None
    if comp is None:
        raise ValueError(f"No light component found on '{actor_label}'")

    changes = []
    if "intensity" in params:
        comp.set_editor_property("intensity", float(params["intensity"]))
        changes.append("intensity")
    if "color" in params:
        c = params["color"]
        comp.set_editor_property("light_color", unreal.Color(int(c[0]), int(c[1]), int(c[2])))
        changes.append("color")
    if "temperature" in params:
        comp.set_editor_property("temperature", float(params["temperature"]))
        comp.set_editor_property("use_temperature", True)
        changes.append("temperature")
    if "attenuationRadius" in params:
        comp.set_editor_property("attenuation_radius", float(params["attenuationRadius"]))
        changes.append("attenuationRadius")
    if "castShadows" in params:
        comp.set_editor_property("cast_shadows", bool(params["castShadows"]))
        changes.append("castShadows")
    if "innerConeAngle" in params:
        comp.set_editor_property("inner_cone_angle", float(params["innerConeAngle"]))
        changes.append("innerConeAngle")
    if "outerConeAngle" in params:
        comp.set_editor_property("outer_cone_angle", float(params["outerConeAngle"]))
        changes.append("outerConeAngle")

    return {"actorLabel": actor_label, "changes": changes, "success": True}


def build_lighting(params: dict) -> dict:
    quality = params.get("quality", "Preview")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, f"BUILD LIGHTING QUALITY={quality}")

    return {"quality": quality, "success": True}


HANDLERS = {
    "spawn_light": spawn_light,
    "set_light_properties": set_light_properties,
    "build_lighting": build_lighting,
}
