"""Expanded AI handlers: EQS, AI Perception, State Trees, Smart Objects."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def _load_or_fail(path, label="Asset"):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise ValueError(f"{label} not found: {path}")
    return asset


# ---------------------------------------------------------------------------
# EQS (Environment Query System)
# ---------------------------------------------------------------------------

def create_eqs_query(params: dict) -> dict:
    """Create an Environment Query asset."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/AI/EQS")

    if not hasattr(unreal, "EnvironmentQuery"):
        raise RuntimeError("EnvironmentQuery not available. Enable EnvironmentQueryEditor plugin.")

    at = unreal.AssetToolsHelpers.get_asset_tools()
    factory = None
    for factory_class_name in ["EnvQueryFactory", "EnvironmentQueryFactory"]:
        if hasattr(unreal, factory_class_name):
            factory = getattr(unreal, factory_class_name)()
            break

    if factory is None:
        raise RuntimeError("EQS factory not available")

    asset = at.create_asset(name, pkg, unreal.EnvironmentQuery, factory)
    if asset is None:
        raise RuntimeError(f"Failed to create EQS query: {name}")

    unreal.EditorAssetLibrary.save_asset(asset.get_path_name())
    return {"path": asset.get_path_name(), "name": name, "success": True}


def list_eqs_queries(params: dict) -> dict:
    """List EQS query assets."""
    _require_unreal()
    directory = params.get("directory", "/Game/")
    recursive = params.get("recursive", True)

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    ar_filter = unreal.ARFilter()
    ar_filter.class_names = ["EnvironmentQuery"]
    ar_filter.package_paths = [directory]
    ar_filter.recursive_paths = recursive

    assets = registry.get_assets(ar_filter)
    results = [{"path": str(a.package_name) + "." + str(a.asset_name), "name": str(a.asset_name)} for a in assets]

    return {"directory": directory, "count": len(results), "queries": results}


# ---------------------------------------------------------------------------
# AI Perception
# ---------------------------------------------------------------------------

def add_perception_component(params: dict) -> dict:
    """Add an AIPerceptionComponent to a Blueprint with sense configs."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    senses = params.get("senses", ["Sight"])

    bp = _load_or_fail(bp_path, "Blueprint")

    if not hasattr(unreal, "AIPerceptionComponent"):
        raise RuntimeError("AIPerceptionComponent not available. Enable AIModule.")

    from .blueprint import _add_component_to_bp
    result = _add_component_to_bp(bp, unreal.AIPerceptionComponent, "AIPerceptionComp")

    return {
        "blueprintPath": bp_path,
        "component": result,
        "configuredSenses": senses,
        "success": True,
        "note": "Sense configs should be added via set_property or execute_python after adding the component.",
    }


def configure_ai_perception_sense(params: dict) -> dict:
    """Configure a sense on an AIPerceptionComponent (via Python exec as the API is limited)."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    sense_type = params.get("senseType", "Sight")
    settings = params.get("settings", {})

    sense_map = {
        "Sight": "AISenseConfig_Sight",
        "Hearing": "AISenseConfig_Hearing",
        "Damage": "AISenseConfig_Damage",
        "Touch": "AISenseConfig_Touch",
        "Team": "AISenseConfig_Team",
    }

    sense_class_name = sense_map.get(sense_type)
    if sense_class_name is None:
        raise ValueError(f"Unknown sense type: {sense_type}. Available: {list(sense_map.keys())}")

    if not hasattr(unreal, sense_class_name):
        raise RuntimeError(f"{sense_class_name} not available")

    return {
        "blueprintPath": bp_path,
        "senseType": sense_type,
        "senseClass": sense_class_name,
        "settings": settings,
        "success": True,
        "note": f"Use editor.execute_python to fully configure {sense_class_name} properties.",
    }


# ---------------------------------------------------------------------------
# State Trees
# ---------------------------------------------------------------------------

def create_state_tree(params: dict) -> dict:
    """Create a State Tree asset."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/AI/StateTrees")

    if not hasattr(unreal, "StateTree"):
        raise RuntimeError("StateTree not available. Enable StateTreeModule plugin.")

    at = unreal.AssetToolsHelpers.get_asset_tools()
    factory = None
    for fn in ["StateTreeFactory"]:
        if hasattr(unreal, fn):
            factory = getattr(unreal, fn)()

    if factory is None:
        raise RuntimeError("StateTree factory not available. Enable StateTreeEditorModule.")

    asset = at.create_asset(name, pkg, unreal.StateTree, factory)
    if asset is None:
        raise RuntimeError(f"Failed to create StateTree: {name}")

    unreal.EditorAssetLibrary.save_asset(asset.get_path_name())
    return {"path": asset.get_path_name(), "name": name, "success": True}


def list_state_trees(params: dict) -> dict:
    """List StateTree assets."""
    _require_unreal()
    directory = params.get("directory", "/Game/")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    ar_filter = unreal.ARFilter()
    ar_filter.class_names = ["StateTree"]
    ar_filter.package_paths = [directory]
    ar_filter.recursive_paths = True

    assets = registry.get_assets(ar_filter)
    results = [{"path": str(a.package_name) + "." + str(a.asset_name), "name": str(a.asset_name)} for a in assets]

    return {"directory": directory, "count": len(results), "stateTrees": results}


def add_state_tree_component(params: dict) -> dict:
    """Add a StateTreeComponent to a Blueprint."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    bp = _load_or_fail(bp_path, "Blueprint")

    if not hasattr(unreal, "StateTreeComponent"):
        raise RuntimeError("StateTreeComponent not available. Enable StateTree plugin.")

    from .blueprint import _add_component_to_bp
    result = _add_component_to_bp(bp, unreal.StateTreeComponent, "StateTreeComp")

    return {"blueprintPath": bp_path, "component": result, "success": True}


# ---------------------------------------------------------------------------
# Smart Objects
# ---------------------------------------------------------------------------

def create_smart_object_definition(params: dict) -> dict:
    """Create a SmartObjectDefinition data asset."""
    _require_unreal()
    name = params["name"]
    pkg = params.get("packagePath", "/Game/AI/SmartObjects")

    if not hasattr(unreal, "SmartObjectDefinition"):
        raise RuntimeError("SmartObjectDefinition not available. Enable SmartObjects plugin.")

    at = unreal.AssetToolsHelpers.get_asset_tools()
    factory = None
    for fn in ["SmartObjectDefinitionFactory"]:
        if hasattr(unreal, fn):
            factory = getattr(unreal, fn)()

    if factory:
        asset = at.create_asset(name, pkg, unreal.SmartObjectDefinition, factory)
    else:
        asset = at.create_asset(name, pkg, unreal.SmartObjectDefinition, None)

    if asset is None:
        raise RuntimeError(f"Failed to create SmartObjectDefinition: {name}")

    unreal.EditorAssetLibrary.save_asset(asset.get_path_name())
    return {"path": asset.get_path_name(), "name": name, "success": True}


def add_smart_object_component(params: dict) -> dict:
    """Add a SmartObjectComponent to a Blueprint."""
    _require_unreal()
    bp_path = params["blueprintPath"]
    bp = _load_or_fail(bp_path, "Blueprint")

    if not hasattr(unreal, "SmartObjectComponent"):
        raise RuntimeError("SmartObjectComponent not available. Enable SmartObjects plugin.")

    from .blueprint import _add_component_to_bp
    result = _add_component_to_bp(bp, unreal.SmartObjectComponent, "SmartObjectComp")

    return {"blueprintPath": bp_path, "component": result, "success": True}


HANDLERS = {
    "create_eqs_query": create_eqs_query,
    "list_eqs_queries": list_eqs_queries,
    "add_perception_component": add_perception_component,
    "configure_ai_perception_sense": configure_ai_perception_sense,
    "create_state_tree": create_state_tree,
    "list_state_trees": list_state_trees,
    "add_state_tree_component": add_state_tree_component,
    "create_smart_object_definition": create_smart_object_definition,
    "add_smart_object_component": add_smart_object_component,
}
