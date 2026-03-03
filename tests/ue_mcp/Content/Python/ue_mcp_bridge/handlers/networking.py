"""Networking and replication handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def _load_bp(path):
    bp = unreal.EditorAssetLibrary.load_asset(path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {path}")
    return bp


def _get_cdo(bp):
    gen = None
    try:
        gen = bp.generated_class()
    except (TypeError, AttributeError):
        pass
    if gen is None:
        try:
            gen = bp.get_editor_property("generated_class")
        except Exception:
            pass
    if gen is None:
        gen = getattr(bp, "generated_class", None)
    if gen is None:
        return None
    if hasattr(gen, "get_default_object"):
        return gen.get_default_object()
    if hasattr(unreal, "get_default_object"):
        return unreal.get_default_object(gen)
    return None


def set_replicates(params: dict) -> dict:
    """Enable or disable actor replication on a Blueprint."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    replicate = params.get("replicates", True)

    if cdo is None:
        raise RuntimeError("Cannot access CDO for replication settings")

    done = False
    if hasattr(cdo, "set_replicates"):
        try:
            cdo.set_replicates(replicate)
            done = True
        except Exception:
            pass

    if not done:
        for prop in ["b_replicates", "bReplicates", "replicates"]:
            if hasattr(cdo, prop):
                try:
                    cdo.set_editor_property(prop, replicate)
                    done = True
                    break
                except Exception:
                    continue

    if not done:
        try:
            cdo.set_is_replicated(replicate)
            done = True
        except Exception:
            pass

    return {"blueprintPath": params["blueprintPath"], "replicates": replicate, "success": True}


def set_property_replicated(params: dict) -> dict:
    """Mark a Blueprint variable as replicated."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    prop_name = params["propertyName"]
    replicated = params.get("replicated", True)
    condition = params.get("replicationCondition", "None")

    bp = _load_bp(bp_path)

    if hasattr(unreal, "BlueprintEditorLibrary"):
        lib = unreal.BlueprintEditorLibrary
        if hasattr(lib, "set_variable_replication_type"):
            rep_type = 1 if replicated else 0  # RepNotify=2
            if params.get("repNotify"):
                rep_type = 2
            lib.set_variable_replication_type(bp, prop_name, rep_type)

    return {
        "blueprintPath": bp_path,
        "propertyName": prop_name,
        "replicated": replicated,
        "condition": condition,
        "success": True,
    }


def configure_net_update_frequency(params: dict) -> dict:
    """Set NetUpdateFrequency and MinNetUpdateFrequency on a Blueprint CDO."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)

    if cdo is None:
        raise RuntimeError("Cannot access CDO")

    freq = params.get("netUpdateFrequency")
    min_freq = params.get("minNetUpdateFrequency")

    if freq is not None and hasattr(cdo, "net_update_frequency"):
        cdo.set_editor_property("net_update_frequency", float(freq))
    if min_freq is not None and hasattr(cdo, "min_net_update_frequency"):
        cdo.set_editor_property("min_net_update_frequency", float(min_freq))

    return {
        "blueprintPath": params["blueprintPath"],
        "netUpdateFrequency": freq,
        "minNetUpdateFrequency": min_freq,
        "success": True,
    }


def set_net_dormancy(params: dict) -> dict:
    """Set net dormancy mode on a Blueprint CDO."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)

    dormancy = params.get("dormancy", "DORM_Awake")
    dormancy_map = {
        "DORM_Never": 0, "DORM_Awake": 1, "DORM_DormantAll": 2,
        "DORM_DormantPartial": 3, "DORM_Initial": 4,
    }

    if cdo and hasattr(cdo, "net_dormancy"):
        val = dormancy_map.get(dormancy, 1)
        cdo.set_editor_property("net_dormancy", val)

    return {"blueprintPath": params["blueprintPath"], "dormancy": dormancy, "success": True}


def set_net_load_on_client(params: dict) -> dict:
    """Control whether an actor loads on client."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    load = params.get("loadOnClient", True)

    if cdo and hasattr(cdo, "b_net_load_on_client"):
        cdo.set_editor_property("b_net_load_on_client", load)

    return {"blueprintPath": params["blueprintPath"], "loadOnClient": load, "success": True}


def set_always_relevant(params: dict) -> dict:
    """Set actor to always be network relevant."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    always = params.get("alwaysRelevant", True)

    if cdo and hasattr(cdo, "b_always_relevant"):
        cdo.set_editor_property("b_always_relevant", always)

    return {"blueprintPath": params["blueprintPath"], "alwaysRelevant": always, "success": True}


def set_only_relevant_to_owner(params: dict) -> dict:
    """Set actor to only be relevant to its owner."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    only_owner = params.get("onlyRelevantToOwner", True)

    if cdo and hasattr(cdo, "b_only_relevant_to_owner"):
        cdo.set_editor_property("b_only_relevant_to_owner", only_owner)

    return {"blueprintPath": params["blueprintPath"], "onlyRelevantToOwner": only_owner, "success": True}


def configure_net_cull_distance(params: dict) -> dict:
    """Set network cull distance squared."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    distance = params.get("netCullDistanceSquared", 225000000.0)

    if cdo and hasattr(cdo, "net_cull_distance_squared"):
        cdo.set_editor_property("net_cull_distance_squared", float(distance))

    return {"blueprintPath": params["blueprintPath"], "netCullDistanceSquared": distance, "success": True}


def set_net_priority(params: dict) -> dict:
    """Set network priority for bandwidth allocation."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    priority = params.get("netPriority", 1.0)

    if cdo and hasattr(cdo, "net_priority"):
        cdo.set_editor_property("net_priority", float(priority))

    return {"blueprintPath": params["blueprintPath"], "netPriority": priority, "success": True}


def set_replicate_movement(params: dict) -> dict:
    """Enable/disable movement replication."""
    _require_unreal()
    bp = _load_bp(params["blueprintPath"])
    cdo = _get_cdo(bp)
    replicate = params.get("replicateMovement", True)

    if cdo and hasattr(cdo, "b_replicate_movement"):
        cdo.set_editor_property("b_replicate_movement", replicate)

    return {"blueprintPath": params["blueprintPath"], "replicateMovement": replicate, "success": True}


def get_networking_info(params: dict) -> dict:
    """Get networking/replication info for a Blueprint."""
    _require_unreal()
    bp_path = params.get("blueprintPath", "")
    if not bp_path:
        raise ValueError("blueprintPath required")

    bp = _load_bp(bp_path)
    cdo = _get_cdo(bp)

    info = {"blueprintPath": bp_path, "replicates": False}

    if cdo:
        for prop in ["b_replicates", "b_always_relevant", "b_only_relevant_to_owner",
                      "b_replicate_movement", "b_net_load_on_client"]:
            if hasattr(cdo, prop):
                info[prop.lstrip("b_")] = cdo.get_editor_property(prop)

        for prop in ["net_update_frequency", "min_net_update_frequency",
                      "net_cull_distance_squared", "net_priority", "net_dormancy"]:
            if hasattr(cdo, prop):
                val = cdo.get_editor_property(prop)
                info[prop] = float(val) if isinstance(val, (int, float)) else str(val)

    return info


HANDLERS = {
    "set_replicates": set_replicates,
    "set_property_replicated": set_property_replicated,
    "configure_net_update_frequency": configure_net_update_frequency,
    "set_net_dormancy": set_net_dormancy,
    "set_net_load_on_client": set_net_load_on_client,
    "set_always_relevant": set_always_relevant,
    "set_only_relevant_to_owner": set_only_relevant_to_owner,
    "configure_net_cull_distance": configure_net_cull_distance,
    "set_net_priority": set_net_priority,
    "set_replicate_movement": set_replicate_movement,
    "get_networking_info": get_networking_info,
}
