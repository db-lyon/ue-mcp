"""
Level/world handlers — read the world outliner, place actors, query spatial data.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def get_world_outliner(params: dict) -> dict:
    """Get all actors in the current level, optionally filtered by class or name."""
    class_filter = params.get("classFilter", None)
    name_filter = params.get("nameFilter", None)
    limit = params.get("limit", 500)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        raise RuntimeError("No world loaded in the editor")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    actors = []

    for actor in all_actors:
        if len(actors) >= limit:
            break

        actor_name = actor.get_name()
        actor_class = actor.get_class().get_name()
        actor_label = actor.get_actor_label()

        if class_filter and class_filter.lower() not in actor_class.lower():
            continue
        if name_filter and name_filter.lower() not in actor_name.lower() and name_filter.lower() not in actor_label.lower():
            continue

        location = actor.get_actor_location()
        rotation = actor.get_actor_rotation()

        actor_info = {
            "name": actor_name,
            "label": actor_label,
            "class": actor_class,
            "location": {"x": location.x, "y": location.y, "z": location.z},
            "rotation": {"pitch": rotation.pitch, "yaw": rotation.yaw, "roll": rotation.roll},
            "folder": actor.get_folder_path().path if hasattr(actor.get_folder_path(), "path") else str(actor.get_folder_path()),
        }

        try:
            actor_info["hidden"] = actor.is_hidden_ed()
        except Exception:
            pass

        actors.append(actor_info)

    return {
        "worldName": world.get_name(),
        "totalActors": len(all_actors),
        "returnedActors": len(actors),
        "classFilter": class_filter,
        "nameFilter": name_filter,
        "actors": actors,
    }


def place_actor(params: dict) -> dict:
    """Place an actor in the current level."""
    class_name = params.get("className", "") or params.get("actorClass", "")
    location = params.get("location", {"x": 0, "y": 0, "z": 0})
    rotation = params.get("rotation", {"pitch": 0, "yaw": 0, "roll": 0})
    label = params.get("label", None)
    folder = params.get("folder", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    actor_class = None
    cls = getattr(unreal, class_name, None)
    if cls is not None:
        actor_class = cls
    else:
        for prefix in ["A", ""]:
            candidate = getattr(unreal, f"{prefix}{class_name}", None)
            if candidate is not None:
                actor_class = candidate
                break

    if actor_class is None:
        if "/" in class_name:
            actor_class = unreal.load_class(None, class_name)
        if actor_class is None:
            raise ValueError(f"Class not found: {class_name}. Try the full path or Python class name.")

    loc = unreal.Vector(location.get("x", 0), location.get("y", 0), location.get("z", 0))
    rot = unreal.Rotator(rotation.get("pitch", 0), rotation.get("yaw", 0), rotation.get("roll", 0))

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, loc, rot)
    if actor is None:
        raise RuntimeError(f"Failed to spawn actor of class {class_name}")

    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)

    return {
        "success": True,
        "name": actor.get_name(),
        "label": actor.get_actor_label(),
        "class": actor.get_class().get_name(),
        "location": {"x": loc.x, "y": loc.y, "z": loc.z},
    }


def delete_actor(params: dict) -> dict:
    """Delete an actor from the current level by name or label."""
    actor_name = params.get("actorName", "") or params.get("actorLabel", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    target = None

    for actor in all_actors:
        if actor.get_name() == actor_name or actor.get_actor_label() == actor_name:
            target = actor
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_name}")

    name = target.get_name()
    label = target.get_actor_label()
    unreal.EditorLevelLibrary.destroy_actor(target)

    return {
        "success": True,
        "deletedActor": name,
        "label": label,
    }


def get_actor_details(params: dict) -> dict:
    """Get detailed information about a specific actor: components, properties, tags."""
    actor_name = params.get("actorName", "") or params.get("actorLabel", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    target = None

    for actor in all_actors:
        if actor.get_name() == actor_name or actor.get_actor_label() == actor_name:
            target = actor
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_name}")

    location = target.get_actor_location()
    rotation = target.get_actor_rotation()
    scale = target.get_actor_scale3d()

    components = []
    try:
        comps = target.get_components_by_class(unreal.ActorComponent)
        for comp in comps:
            comp_info = {
                "name": comp.get_name(),
                "class": comp.get_class().get_name(),
            }
            if hasattr(comp, "get_relative_location"):
                rel_loc = comp.get_relative_location()
                comp_info["relativeLocation"] = {"x": rel_loc.x, "y": rel_loc.y, "z": rel_loc.z}
            components.append(comp_info)
    except Exception:
        pass

    tags = []
    try:
        for tag in target.tags:
            tags.append(str(tag))
    except Exception:
        pass

    properties = {}
    for prop_name in dir(target):
        if prop_name.startswith("_"):
            continue
        try:
            val = getattr(target, prop_name)
            if callable(val):
                continue
            if isinstance(val, (bool, int, float, str)):
                properties[prop_name] = val
        except Exception:
            continue

    return {
        "name": target.get_name(),
        "label": target.get_actor_label(),
        "class": target.get_class().get_name(),
        "location": {"x": location.x, "y": location.y, "z": location.z},
        "rotation": {"pitch": rotation.pitch, "yaw": rotation.yaw, "roll": rotation.roll},
        "scale": {"x": scale.x, "y": scale.y, "z": scale.z},
        "componentCount": len(components),
        "components": components,
        "tags": tags,
        "properties": properties,
    }


def move_actor(params: dict) -> dict:
    """Move/rotate/scale an actor in the level."""
    actor_name = params.get("actorName", "") or params.get("actorLabel", "")
    location = params.get("location", None)
    rotation = params.get("rotation", None)
    scale = params.get("scale", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    target = None

    for actor in all_actors:
        if actor.get_name() == actor_name or actor.get_actor_label() == actor_name:
            target = actor
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_name}")

    if location:
        loc = unreal.Vector(location.get("x", 0), location.get("y", 0), location.get("z", 0))
        target.set_actor_location(loc, sweep=False, teleport=True)

    if rotation:
        rot = unreal.Rotator(rotation.get("pitch", 0), rotation.get("yaw", 0), rotation.get("roll", 0))
        target.set_actor_rotation(rot, teleport_physics=True)

    if scale:
        sc = unreal.Vector(scale.get("x", 1), scale.get("y", 1), scale.get("z", 1))
        target.set_actor_scale3d(sc)

    new_loc = target.get_actor_location()
    new_rot = target.get_actor_rotation()
    new_sc = target.get_actor_scale3d()

    return {
        "success": True,
        "name": target.get_name(),
        "location": {"x": new_loc.x, "y": new_loc.y, "z": new_loc.z},
        "rotation": {"pitch": new_rot.pitch, "yaw": new_rot.yaw, "roll": new_rot.roll},
        "scale": {"x": new_sc.x, "y": new_sc.y, "z": new_sc.z},
    }


def select_actors(params: dict) -> dict:
    """Select actors in the editor by label."""
    labels = params.get("labels", []) or params.get("actorLabels", [])
    add_to_selection = params.get("addToSelection", False)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not add_to_selection:
        unreal.EditorLevelLibrary.set_selected_level_actors([])

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    label_set = set(labels)
    matched = []
    for a in all_actors:
        if a.get_actor_label() in label_set:
            matched.append(a)

    if matched:
        current = list(unreal.EditorLevelLibrary.get_selected_level_actors()) if add_to_selection else []
        unreal.EditorLevelLibrary.set_selected_level_actors(current + matched)

    return {
        "selected": [a.get_actor_label() for a in matched],
        "count": len(matched),
        "requested": len(labels),
    }


def get_selected_actors(params: dict) -> dict:
    """Get the currently selected actors in the editor."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    selected = unreal.EditorLevelLibrary.get_selected_level_actors()
    actors = []
    for a in selected:
        actors.append({
            "label": a.get_actor_label(),
            "class": a.get_class().get_name(),
            "location": {
                "x": a.get_actor_location().x,
                "y": a.get_actor_location().y,
                "z": a.get_actor_location().z,
            },
        })

    return {"count": len(actors), "actors": actors}


def add_component_to_actor(params: dict) -> dict:
    """Add a component to an existing actor in the level."""
    actor_label = params.get("actorLabel", "")
    component_class = params.get("componentClass", "")
    component_name = params.get("componentName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not actor_label or not component_class:
        raise ValueError("actorLabel and componentClass are required")

    target = None
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        if a.get_actor_label() == actor_label:
            target = a
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_label}")

    cls = getattr(unreal, component_class, None)
    if cls is None:
        raise ValueError(f"Component class not found: {component_class}")

    comp = None

    if hasattr(target, "add_component_by_class"):
        comp = target.add_component_by_class(cls, False, unreal.Transform(), False)

    if comp is None and hasattr(unreal, "EditorLevelLibrary"):
        try:
            new_comp = cls()
            if hasattr(target, "add_component"):
                target.add_component(new_comp)
                comp = new_comp
        except Exception:
            pass

    if comp is None:
        try:
            code = f"""
import unreal
actor = None
for a in unreal.EditorLevelLibrary.get_all_level_actors():
    if a.get_actor_label() == '{actor_label}':
        actor = a
        break
if actor:
    comp = actor.add_component_by_class(unreal.{component_class}, False, unreal.Transform(), False)
"""
            pass
        except Exception:
            pass

    if comp is None:
        return {
            "actorLabel": actor_label,
            "componentClass": component_class,
            "componentName": component_name,
            "success": False,
            "note": "add_component_by_class not available on this actor type. Use blueprint(action='add_component') for Blueprint-based actors.",
        }

    if component_name:
        try:
            comp.rename(component_name)
        except Exception:
            pass

    return {
        "actorLabel": actor_label,
        "componentClass": component_class,
        "componentName": comp.get_name(),
        "success": True,
    }


def set_component_property(params: dict) -> dict:
    """Set a property on a component of an actor in the level."""
    actor_label = params.get("actorLabel", "")
    component_class = params.get("componentClass", "")
    property_name = params.get("propertyName", "")
    value = params.get("value")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    target = None
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        if a.get_actor_label() == actor_label:
            target = a
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_label}")

    cls = getattr(unreal, component_class, None) if component_class else None
    comp = None

    if cls:
        comp = target.get_component_by_class(cls)
    else:
        comps = target.get_components_by_class(unreal.ActorComponent)
        if comps:
            comp = comps[0]

    if comp is None:
        raise ValueError(f"Component {component_class or 'any'} not found on {actor_label}")

    comp.set_editor_property(property_name, value)

    return {
        "actorLabel": actor_label,
        "componentClass": comp.get_class().get_name(),
        "propertyName": property_name,
        "success": True,
    }


HANDLERS = {
    "get_world_outliner": get_world_outliner,
    "place_actor": place_actor,
    "delete_actor": delete_actor,
    "get_actor_details": get_actor_details,
    "move_actor": move_actor,
    "select_actors": select_actors,
    "get_selected_actors": get_selected_actors,
    "add_component_to_actor": add_component_to_actor,
    "set_component_property": set_component_property,
}
