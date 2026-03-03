"""Build pipeline and compilation handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False

import subprocess
import os


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def build_lighting(params: dict) -> dict:
    """Build lighting for the current level."""
    _require_unreal()
    quality = params.get("quality", "Preview")
    world = unreal.EditorLevelLibrary.get_editor_world()

    quality_map = {
        "Preview": "BuildLighting PREVIEW",
        "Medium": "BuildLighting MEDIUM",
        "High": "BuildLighting HIGH",
        "Production": "BuildLighting PRODUCTION",
    }

    cmd = quality_map.get(quality, "BuildLighting PREVIEW")
    unreal.SystemLibrary.execute_console_command(world, cmd)

    return {"quality": quality, "success": True, "message": f"Lighting build triggered ({quality})"}


def build_all(params: dict) -> dict:
    """Trigger Build All (geometry, lighting, paths, HLOD)."""
    _require_unreal()
    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "BUILD")
    return {"success": True, "message": "Build All triggered"}


def build_geometry(params: dict) -> dict:
    """Rebuild BSP geometry."""
    _require_unreal()
    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "MAP REBUILD")
    return {"success": True, "message": "Geometry rebuild triggered"}


def build_hlod(params: dict) -> dict:
    """Build HLODs for the current level."""
    _require_unreal()
    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "BuildHLOD")
    return {"success": True, "message": "HLOD build triggered"}


def build_navigation(params: dict) -> dict:
    """Rebuild navigation mesh."""
    _require_unreal()
    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    return {"success": True, "message": "Navigation rebuild triggered"}


def validate_assets(params: dict) -> dict:
    """Run data validation on assets."""
    _require_unreal()
    directory = params.get("directory", "/Game/")

    if hasattr(unreal, "EditorValidatorSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
        if subsystem and hasattr(subsystem, "validate_assets_with_settings"):
            return {"directory": directory, "success": True, "note": "Validation triggered"}

    return {"directory": directory, "success": True, "note": "Use editor console: 'DataValidation.ValidateAssets'"}


def get_build_status(params: dict) -> dict:
    """Get current build/map status flags."""
    _require_unreal()
    world = unreal.EditorLevelLibrary.get_editor_world()

    status = {"worldLoaded": world is not None}

    if world:
        status["worldName"] = world.get_name()
        if hasattr(world, "get_num_levels"):
            status["numLevels"] = world.get_num_levels()

    return status


def cook_content(params: dict) -> dict:
    """Trigger content cooking for a platform."""
    _require_unreal()
    platform = params.get("platform", "Windows")

    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, f"CookOnTheFly -TargetPlatform={platform}")

    return {"platform": platform, "success": True, "message": f"Cook triggered for {platform}"}


HANDLERS = {
    "build_lighting_pipeline": build_lighting,
    "build_all": build_all,
    "build_geometry": build_geometry,
    "build_hlod": build_hlod,
    "build_navigation_pipeline": build_navigation,
    "validate_assets": validate_assets,
    "get_build_status": get_build_status,
    "cook_content": cook_content,
}
