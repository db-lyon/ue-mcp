"""Performance / stats / viewport handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def get_editor_performance_stats(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    actors = unreal.EditorLevelLibrary.get_all_level_actors()

    actor_counts = {}
    for actor in actors:
        cls_name = actor.get_class().get_name()
        actor_counts[cls_name] = actor_counts.get(cls_name, 0) + 1

    top_classes = sorted(actor_counts.items(), key=lambda x: x[1], reverse=True)[:20]

    return {
        "totalActors": len(actors),
        "topActorClasses": [{"class": k, "count": v} for k, v in top_classes],
    }


def run_stat_command(params: dict) -> dict:
    command = params.get("command", "stat fps")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, command)

    return {"command": command, "success": True}


def set_scalability(params: dict) -> dict:
    level = params.get("level", "Epic")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    world = unreal.EditorLevelLibrary.get_editor_world()

    level_map = {"Low": 0, "Medium": 1, "High": 2, "Epic": 3, "Cinematic": 4}
    idx = level_map.get(level, 3)

    commands = [
        f"sg.ViewDistanceQuality {idx}",
        f"sg.AntiAliasingQuality {idx}",
        f"sg.ShadowQuality {idx}",
        f"sg.GlobalIlluminationQuality {idx}",
        f"sg.ReflectionQuality {idx}",
        f"sg.PostProcessQuality {idx}",
        f"sg.TextureQuality {idx}",
        f"sg.EffectsQuality {idx}",
        f"sg.FoliageQuality {idx}",
        f"sg.ShadingQuality {idx}",
    ]

    for cmd in commands:
        unreal.SystemLibrary.execute_console_command(world, cmd)

    return {"level": level, "success": True}


def capture_screenshot(params: dict) -> dict:
    filename = params.get("filename", "screenshot.png")
    resolution_x = params.get("resolutionX", 1920)
    resolution_y = params.get("resolutionY", 1080)
    show_ui = params.get("showUI", False)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if hasattr(unreal, "AutomationLibrary") and hasattr(unreal.AutomationLibrary, "take_high_res_screenshot"):
        unreal.AutomationLibrary.take_high_res_screenshot(
            resolution_x, resolution_y, filename
        )
        return {"filename": filename, "success": True}

    world = unreal.EditorLevelLibrary.get_editor_world()
    cmd = f"HighResShot {resolution_x}x{resolution_y} filename=\"{filename}\""
    unreal.SystemLibrary.execute_console_command(world, cmd)
    return {"filename": filename, "success": True, "note": "Triggered via console command"}


def get_viewport_info(params: dict) -> dict:
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    info = {}

    if hasattr(unreal, "UnrealEditorSubsystem"):
        subsys = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if subsys and hasattr(subsys, "get_level_viewport_camera_info"):
            loc, rot = subsys.get_level_viewport_camera_info()
            if loc:
                info["cameraLocation"] = {"x": loc.x, "y": loc.y, "z": loc.z}
            if rot:
                info["cameraRotation"] = {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll}
    elif hasattr(unreal, "EditorLevelLibrary") and hasattr(unreal.EditorLevelLibrary, "get_level_viewport_camera_info"):
        loc, rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
        if loc:
            info["cameraLocation"] = {"x": loc.x, "y": loc.y, "z": loc.z}
        if rot:
            info["cameraRotation"] = {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll}

    return info


def set_viewport_camera(params: dict) -> dict:
    from ._util import to_vec3, to_rot3
    location = params.get("location")
    rotation = params.get("rotation")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if location and rotation:
        lx, ly, lz = to_vec3(location)
        rp, ry, rr = to_rot3(rotation)
        loc = unreal.Vector(lx, ly, lz)
        rot = unreal.Rotator(rp, ry, rr)

        if hasattr(unreal, "UnrealEditorSubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
            if subsys and hasattr(subsys, "set_level_viewport_camera_info"):
                subsys.set_level_viewport_camera_info(loc, rot)
                return {"location": location, "rotation": rotation, "success": True}

        if hasattr(unreal.EditorLevelLibrary, "set_level_viewport_camera_info"):
            unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)
            return {"location": location, "rotation": rotation, "success": True}

    return {"success": False, "note": "Location and rotation are required"}


def focus_viewport_on_actor(params: dict) -> dict:
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

    unreal.EditorLevelLibrary.set_selected_level_actors([target])

    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "CAMERA ALIGN")

    return {"actorLabel": actor_label, "success": True}


HANDLERS = {
    "get_editor_performance_stats": get_editor_performance_stats,
    "run_stat_command": run_stat_command,
    "set_scalability": set_scalability,
    "capture_screenshot": capture_screenshot,
    "get_viewport_info": get_viewport_info,
    "set_viewport_camera": set_viewport_camera,
    "focus_viewport_on_actor": focus_viewport_on_actor,
}
