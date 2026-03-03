"""Game Framework handlers: GameMode, GameState, PlayerController, HUD."""

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


def _create_framework_bp(parent_class_name, name, package_path):
    """Create a Blueprint with a UE framework parent class."""
    from ._util import ensure_asset_cleared
    _require_unreal()
    parent = getattr(unreal, parent_class_name, None)
    if parent is None:
        raise RuntimeError(f"Class {parent_class_name} not available")

    ensure_asset_cleared(f"{package_path}/{name}")

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)

    at = unreal.AssetToolsHelpers.get_asset_tools()
    bp = at.create_asset(name, package_path, unreal.Blueprint, factory)
    if bp is None:
        raise RuntimeError(f"Failed to create {parent_class_name} Blueprint: {name}")

    unreal.EditorAssetLibrary.save_asset(bp.get_path_name())
    return bp


def create_game_mode(params: dict) -> dict:
    """Create a GameModeBase Blueprint."""
    name = params["name"]
    pkg = params.get("packagePath", "/Game/Framework")
    parent = params.get("parentClass", "GameModeBase")

    bp = _create_framework_bp(parent, name, pkg)

    defaults = params.get("defaults", {})
    cdo = None
    try:
        gen = bp.generated_class()
        if gen and hasattr(gen, "get_default_object"):
            cdo = gen.get_default_object()
    except (TypeError, AttributeError):
        pass
    if cdo is None:
        try:
            gen = getattr(bp, "generated_class", None)
            if gen and hasattr(gen, "get_default_object"):
                cdo = gen.get_default_object()
        except Exception:
            pass

    set_classes = {}
    if cdo:
        class_props = {
            "defaultPawnClass": "default_pawn_class",
            "hudClass": "hud_class",
            "playerControllerClass": "player_controller_class",
            "gameStateClass": "game_state_class",
            "playerStateClass": "player_state_class",
            "spectatorClass": "spectator_class",
        }
        for key, ue_prop in class_props.items():
            val = defaults.get(key)
            if val and hasattr(cdo, ue_prop):
                cls = unreal.EditorAssetLibrary.load_asset(val)
                if cls:
                    gen = cls.get_editor_property("generated_class") if hasattr(cls, "generated_class") else cls
                    cdo.set_editor_property(ue_prop, gen)
                    set_classes[key] = val

    return {"path": bp.get_path_name(), "name": name, "parent": parent, "configuredClasses": set_classes, "success": True}


def create_game_state(params: dict) -> dict:
    """Create a GameStateBase Blueprint."""
    name = params["name"]
    pkg = params.get("packagePath", "/Game/Framework")
    parent = params.get("parentClass", "GameStateBase")

    bp = _create_framework_bp(parent, name, pkg)
    return {"path": bp.get_path_name(), "name": name, "parent": parent, "success": True}


def create_player_controller(params: dict) -> dict:
    """Create a PlayerController Blueprint."""
    name = params["name"]
    pkg = params.get("packagePath", "/Game/Framework")
    parent = params.get("parentClass", "PlayerController")

    bp = _create_framework_bp(parent, name, pkg)
    return {"path": bp.get_path_name(), "name": name, "parent": parent, "success": True}


def create_player_state(params: dict) -> dict:
    """Create a PlayerState Blueprint."""
    name = params["name"]
    pkg = params.get("packagePath", "/Game/Framework")

    bp = _create_framework_bp("PlayerState", name, pkg)
    return {"path": bp.get_path_name(), "name": name, "success": True}


def create_hud(params: dict) -> dict:
    """Create a HUD Blueprint."""
    name = params["name"]
    pkg = params.get("packagePath", "/Game/Framework")

    bp = _create_framework_bp("HUD", name, pkg)
    return {"path": bp.get_path_name(), "name": name, "success": True}


def set_world_settings_game_mode(params: dict) -> dict:
    """Set the default GameMode override in world settings."""
    _require_unreal()
    game_mode_path = params["gameModePath"]

    gm = _load_or_fail(game_mode_path, "GameMode Blueprint")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        raise RuntimeError("No editor world available")

    ws = world.get_world_settings() if hasattr(world, "get_world_settings") else None
    if ws and hasattr(ws, "default_game_mode"):
        gen = gm.get_editor_property("generated_class") if hasattr(gm, "generated_class") else gm
        ws.set_editor_property("default_game_mode", gen)
        return {"gameModePath": game_mode_path, "success": True}

    return {"gameModePath": game_mode_path, "success": False, "note": "Could not access world settings"}


def get_game_framework_info(params: dict) -> dict:
    """Get current level's game framework classes."""
    _require_unreal()
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        return {"error": "No editor world"}

    info = {}
    ws = world.get_world_settings() if hasattr(world, "get_world_settings") else None
    if ws:
        for prop in ["default_game_mode"]:
            if hasattr(ws, prop):
                val = ws.get_editor_property(prop)
                info[prop] = str(val.get_name()) if val else None

    return info


HANDLERS = {
    "create_game_mode": create_game_mode,
    "create_game_state": create_game_state,
    "create_player_controller": create_player_controller,
    "create_player_state": create_player_state,
    "create_hud": create_hud,
    "set_world_game_mode": set_world_settings_game_mode,
    "get_game_framework_info": get_game_framework_info,
}
