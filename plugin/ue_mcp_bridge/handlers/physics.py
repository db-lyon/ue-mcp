"""Physics and collision handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _find_actor(label):
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    raise ValueError(f"Actor not found: {label}")


def set_collision_profile(params: dict) -> dict:
    """Set the collision profile on an actor's primitive component."""
    actor_label = params.get("actorLabel", "")
    profile_name = params.get("profileName", "")
    component_class = params.get("componentClass", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not actor_label or not profile_name:
        raise ValueError("actorLabel and profileName are required")

    actor = _find_actor(actor_label)

    comp = None
    if component_class:
        cls = getattr(unreal, component_class, None)
        if cls:
            comp = actor.get_component_by_class(cls)
    if comp is None:
        for cls_name in ("StaticMeshComponent", "SkeletalMeshComponent", "CapsuleComponent", "SphereComponent", "BoxComponent"):
            cls = getattr(unreal, cls_name, None)
            if cls:
                comp = actor.get_component_by_class(cls)
                if comp:
                    break

    if comp is None:
        raise ValueError(f"No primitive component found on {actor_label}")

    comp.set_editor_property("collision_profile_name", profile_name)

    return {
        "actorLabel": actor_label,
        "profileName": profile_name,
        "componentClass": comp.get_class().get_name(),
        "success": True,
    }


def set_simulate_physics(params: dict) -> dict:
    """Enable or disable physics simulation on an actor's primitive component."""
    actor_label = params.get("actorLabel", "")
    simulate = params.get("simulate", True)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    actor = _find_actor(actor_label)

    comp = None
    for cls_name in ("StaticMeshComponent", "SkeletalMeshComponent"):
        cls = getattr(unreal, cls_name, None)
        if cls:
            comp = actor.get_component_by_class(cls)
            if comp:
                break

    if comp is None:
        raise ValueError(f"No mesh component found on {actor_label}")

    comp.set_simulate_physics(simulate)

    return {
        "actorLabel": actor_label,
        "simulatePhysics": simulate,
        "componentClass": comp.get_class().get_name(),
        "success": True,
    }


def set_collision_enabled(params: dict) -> dict:
    """Set the collision enabled state on an actor's component."""
    actor_label = params.get("actorLabel", "")
    collision_type = params.get("collisionEnabled", "QueryAndPhysics")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    actor = _find_actor(actor_label)

    comp = None
    for cls_name in ("StaticMeshComponent", "SkeletalMeshComponent", "CapsuleComponent", "SphereComponent", "BoxComponent"):
        cls = getattr(unreal, cls_name, None)
        if cls:
            comp = actor.get_component_by_class(cls)
            if comp:
                break

    if comp is None:
        raise ValueError(f"No primitive component found on {actor_label}")

    type_map = {
        "NoCollision": unreal.CollisionEnabled.NO_COLLISION,
        "QueryOnly": unreal.CollisionEnabled.QUERY_ONLY,
        "PhysicsOnly": unreal.CollisionEnabled.PHYSICS_ONLY,
        "QueryAndPhysics": unreal.CollisionEnabled.QUERY_AND_PHYSICS,
    }

    enum_val = type_map.get(collision_type)
    if enum_val is None:
        raise ValueError(f"Invalid collisionEnabled: {collision_type}. Valid: {list(type_map.keys())}")

    comp.set_collision_enabled(enum_val)

    return {
        "actorLabel": actor_label,
        "collisionEnabled": collision_type,
        "success": True,
    }


def set_physics_properties(params: dict) -> dict:
    """Set physics properties (mass, damping, gravity) on an actor's component."""
    actor_label = params.get("actorLabel", "")
    properties = params.get("properties", {})

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    actor = _find_actor(actor_label)

    comp = None
    for cls_name in ("StaticMeshComponent", "SkeletalMeshComponent"):
        cls = getattr(unreal, cls_name, None)
        if cls:
            comp = actor.get_component_by_class(cls)
            if comp:
                break

    if comp is None:
        raise ValueError(f"No mesh component found on {actor_label}")

    updated = []
    for key, value in properties.items():
        try:
            comp.set_editor_property(key, value)
            updated.append(key)
        except Exception:
            try:
                body = comp.get_editor_property("body_instance")
                if body:
                    body.set_editor_property(key, value)
                    updated.append(key)
            except Exception:
                pass

    return {
        "actorLabel": actor_label,
        "updated": updated,
        "success": len(updated) > 0,
    }


HANDLERS = {
    "set_collision_profile": set_collision_profile,
    "set_simulate_physics": set_simulate_physics,
    "set_collision_enabled": set_collision_enabled,
    "set_physics_properties": set_physics_properties,
}
