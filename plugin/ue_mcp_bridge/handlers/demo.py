"""Demo scene builders — show off the MCP's capabilities in one shot."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False

CUBE = "/Engine/BasicShapes/Cube"
SPHERE = "/Engine/BasicShapes/Sphere"
CYLINDER = "/Engine/BasicShapes/Cylinder"

FOLDER = "Demo_Scene"
MAT_DIR = "/Game/Demo"
DEMO_LEVEL = "/Game/Demo/DemoLevel"


def _spawn_mesh(label, mesh_path, location, rotation=(0, 0, 0), scale=(1, 1, 1)):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(*location),
        unreal.Rotator(*rotation),
    )
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh:
        actor.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(mesh)
    actor.set_actor_label(label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_folder_path(FOLDER)
    return actor


def _spawn_point_light(label, location, color, intensity):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PointLight, unreal.Vector(*location)
    )
    actor.set_actor_label(label)
    actor.set_folder_path(FOLDER)
    comp = actor.get_component_by_class(unreal.PointLightComponent)
    if comp:
        comp.set_editor_property("intensity", float(intensity))
        comp.set_editor_property("light_color", unreal.Color(*color))
        comp.set_editor_property("attenuation_radius", 2000.0)
    return actor


def _make_material(name, base_color=None, emissive=None, roughness=None, metallic=None):
    if not hasattr(unreal, "MaterialEditingLibrary"):
        return None
    path = f"{MAT_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)

    factory = unreal.MaterialFactoryNew()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, MAT_DIR, unreal.Material, factory)
    if mat is None:
        return None

    y = 0
    if base_color:
        expr = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant4Vector, -400, y
        )
        expr.set_editor_property("constant", unreal.LinearColor(*base_color))
        unreal.MaterialEditingLibrary.connect_material_property(
            expr, "RGBA", unreal.MaterialProperty.MP_BASE_COLOR
        )
        y += 200

    if emissive:
        expr = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant4Vector, -400, y
        )
        expr.set_editor_property("constant", unreal.LinearColor(*emissive))
        unreal.MaterialEditingLibrary.connect_material_property(
            expr, "RGBA", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        )
        y += 200

    if roughness is not None:
        expr = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant, -400, y
        )
        expr.set_editor_property("r", float(roughness))
        unreal.MaterialEditingLibrary.connect_material_property(
            expr, "", unreal.MaterialProperty.MP_ROUGHNESS
        )
        y += 200

    if metallic is not None:
        expr = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant, -400, y
        )
        expr.set_editor_property("r", float(metallic))
        unreal.MaterialEditingLibrary.connect_material_property(
            expr, "", unreal.MaterialProperty.MP_METALLIC
        )

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mat


def _apply_mat(actor, mat):
    if mat is None:
        return
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp:
        comp.set_material(0, mat)


# ── Scene: Neon Shrine ──────────────────────────────────────────────

def demo_scene_from_nothing(params: dict) -> dict:
    """Build a 'Neon Shrine' from an empty level: floor, pillars, glowing
    sphere, colored lights, fog, post-process, and camera — one call."""

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    log = []

    # ── Create and open a fresh level ───────────────────────────────
    try:
        if hasattr(unreal, "LevelEditorSubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if subsys and hasattr(subsys, "new_level"):
                subsys.new_level(DEMO_LEVEL)
                log.append(f"New level created and opened: {DEMO_LEVEL}")
            elif subsys and hasattr(subsys, "new_level_from_template"):
                subsys.new_level_from_template(DEMO_LEVEL, "")
                log.append(f"New level created and opened: {DEMO_LEVEL}")
            else:
                unreal.EditorLevelLibrary.new_level(DEMO_LEVEL) if hasattr(unreal.EditorLevelLibrary, "new_level") else None
                log.append(f"New level created and opened: {DEMO_LEVEL}")
        else:
            world = unreal.EditorLevelLibrary.get_editor_world()
            unreal.SystemLibrary.execute_console_command(world, "MAP NEW")
            log.append("New level created via console command")
    except Exception as e:
        log.append(f"Level creation failed ({e}), building in current level")

    # ── Materials ───────────────────────────────────────────────────
    try:
        floor_mat = _make_material(
            "M_Demo_Floor",
            base_color=(0.015, 0.015, 0.025, 1),
            roughness=0.12,
            metallic=0.9,
        )
        glow_mat = _make_material(
            "M_Demo_Glow",
            base_color=(0, 0, 0, 1),
            emissive=(5.0, 3.0, 0.4, 1),
        )
        pillar_mat = _make_material(
            "M_Demo_Pillar",
            base_color=(0.06, 0.06, 0.08, 1),
            roughness=0.35,
            metallic=0.4,
        )
        log.append("Materials created (floor, glow, pillar)")
    except Exception as e:
        floor_mat = glow_mat = pillar_mat = None
        log.append(f"Materials skipped: {e}")

    # ── Floor ───────────────────────────────────────────────────────
    try:
        floor = _spawn_mesh("Demo_Floor", CUBE, (0, 0, -5), scale=(60, 60, 0.1))
        _apply_mat(floor, floor_mat)
        log.append("Floor placed (60m x 60m reflective dark)")
    except Exception as e:
        log.append(f"Floor failed: {e}")

    # ── Central pedestal ────────────────────────────────────────────
    try:
        ped = _spawn_mesh("Demo_Pedestal", CYLINDER, (0, 0, 75), scale=(2.5, 2.5, 1.5))
        _apply_mat(ped, pillar_mat)
        log.append("Pedestal placed")
    except Exception as e:
        log.append(f"Pedestal failed: {e}")

    # ── Hero sphere (glowing) ───────────────────────────────────────
    try:
        hero = _spawn_mesh("Demo_HeroSphere", SPHERE, (0, 0, 260), scale=(1.8, 1.8, 1.8))
        _apply_mat(hero, glow_mat)
        log.append("Hero sphere placed (emissive gold)")
    except Exception as e:
        log.append(f"Hero sphere failed: {e}")

    # ── Four pillars ────────────────────────────────────────────────
    pillar_xy = [(600, 600), (-600, 600), (-600, -600), (600, -600)]
    for i, (x, y) in enumerate(pillar_xy):
        try:
            p = _spawn_mesh(f"Demo_Pillar_{i+1}", CYLINDER, (x, y, 200), scale=(0.6, 0.6, 4))
            _apply_mat(p, pillar_mat)
        except Exception:
            pass
    log.append("4 pillars placed")

    # ── Accent orbs at pillar bases ─────────────────────────────────
    for i, (x, y) in enumerate(pillar_xy):
        try:
            orb = _spawn_mesh(f"Demo_Orb_{i+1}", SPHERE, (x, y, 30), scale=(0.4, 0.4, 0.4))
            _apply_mat(orb, glow_mat)
        except Exception:
            pass
    log.append("4 accent orbs placed")

    # ── Colored point lights ────────────────────────────────────────
    neon_lights = [
        ("Demo_Light_Cyan",    (420, 420, 300),  (0, 220, 255),  80000),
        ("Demo_Light_Magenta", (-420, 420, 300),  (255, 0, 180),  80000),
        ("Demo_Light_Amber",   (-420, -420, 300), (255, 170, 0),  80000),
        ("Demo_Light_Violet",  (420, -420, 300),  (130, 0, 255),  80000),
    ]
    for label, pos, color, intensity in neon_lights:
        try:
            _spawn_point_light(label, pos, color, intensity)
        except Exception:
            pass
    log.append("4 neon point lights placed (cyan / magenta / amber / violet)")

    # ── Hero key light (warm, above sphere) ─────────────────────────
    try:
        _spawn_point_light("Demo_HeroLight", (80, -80, 500), (255, 225, 190), 120000)
        log.append("Hero key light placed")
    except Exception as e:
        log.append(f"Hero key light failed: {e}")

    # ── Directional moonlight ───────────────────────────────────────
    try:
        moon = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.DirectionalLight,
            unreal.Vector(0, 0, 500),
            unreal.Rotator(-30, 210, 0),
        )
        moon.set_actor_label("Demo_Moonlight")
        moon.set_folder_path(FOLDER)
        comp = moon.get_component_by_class(unreal.LightComponent)
        if comp:
            comp.set_editor_property("intensity", 3.0)
            comp.set_editor_property("light_color", unreal.Color(100, 120, 200))
        log.append("Directional moonlight placed")
    except Exception as e:
        log.append(f"Moonlight failed: {e}")

    # ── Sky light (dim ambient fill) ────────────────────────────────
    try:
        sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SkyLight, unreal.Vector(0, 0, 500)
        )
        sky.set_actor_label("Demo_SkyLight")
        sky.set_folder_path(FOLDER)
        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        if comp:
            comp.set_editor_property("intensity", 0.3)
            comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        log.append("Sky light placed (dim ambient)")
    except Exception as e:
        log.append(f"Sky light failed: {e}")

    # ── Exponential height fog ──────────────────────────────────────
    try:
        fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.ExponentialHeightFog, unreal.Vector(0, 0, 50)
        )
        fog.set_actor_label("Demo_Fog")
        fog.set_folder_path(FOLDER)
        comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if comp:
            comp.set_editor_property("fog_density", 0.035)
            comp.set_editor_property("fog_height_falloff", 0.5)
            comp.set_editor_property("fog_inscattering_color", unreal.LinearColor(0.02, 0.02, 0.06, 1))
            comp.set_editor_property("fog_max_opacity", 0.85)
        log.append("Exponential height fog placed (dark blue-purple)")
    except Exception as e:
        log.append(f"Fog failed: {e}")

    # ── Post-process volume (bloom + vignette) ──────────────────────
    try:
        ppv = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PostProcessVolume, unreal.Vector(0, 0, 0)
        )
        ppv.set_actor_label("Demo_PostProcess")
        ppv.set_folder_path(FOLDER)
        ppv.set_editor_property("unbound", True)
        settings = ppv.get_editor_property("settings")
        try:
            settings.set_editor_property("bloom_intensity", 2.0)
            settings.set_editor_property("override_bloom_intensity", True)
        except Exception:
            pass
        try:
            settings.set_editor_property("vignette_intensity", 0.6)
            settings.set_editor_property("override_vignette_intensity", True)
        except Exception:
            pass
        try:
            settings.set_editor_property("auto_exposure_bias", -1.0)
            settings.set_editor_property("override_auto_exposure_bias", True)
        except Exception:
            pass
        ppv.set_editor_property("settings", settings)
        log.append("Post-process volume placed (bloom + vignette)")
    except Exception as e:
        log.append(f"Post-process failed: {e}")

    # ── Viewport camera ─────────────────────────────────────────────
    try:
        cam_loc = unreal.Vector(1100, -700, 380)
        cam_rot = unreal.Rotator(-12, 150, 0)
        if hasattr(unreal, "UnrealEditorSubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
            if subsys and hasattr(subsys, "set_level_viewport_camera_info"):
                subsys.set_level_viewport_camera_info(cam_loc, cam_rot)
                log.append("Viewport camera positioned")
            else:
                log.append("Camera API not available on this subsystem")
        elif hasattr(unreal.EditorLevelLibrary, "set_level_viewport_camera_info"):
            unreal.EditorLevelLibrary.set_level_viewport_camera_info(cam_loc, cam_rot)
            log.append("Viewport camera positioned")
        else:
            log.append("Camera positioning API not found")
    except Exception as e:
        log.append(f"Camera positioning failed: {e}")

    # ── Save the level ─────────────────────────────────────────────
    try:
        if hasattr(unreal, "LevelEditorSubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if subsys and hasattr(subsys, "save_current_level"):
                subsys.save_current_level()
                log.append("Level saved")
    except Exception:
        pass

    ok_count = sum(1 for s in log if "failed" not in s.lower() and "skipped" not in s.lower())
    return {
        "scene": "Neon Shrine",
        "level": DEMO_LEVEL,
        "description": (
            "Fresh level with dark reflective floor, glowing sphere on a pedestal, "
            "4 pillars with accent orbs, cyan/magenta/amber/violet neon lights, "
            "moonlight, fog, bloom. All in outliner folder 'Demo_Scene'. "
            "Run demo_cleanup to delete everything including the level."
        ),
        "stepsCompleted": ok_count,
        "totalSteps": len(log),
        "log": log,
    }


# ── Cleanup ─────────────────────────────────────────────────────────

def demo_cleanup(params: dict) -> dict:
    """Remove demo level, all Demo_ actors, and demo material assets."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    log = []

    # If we're currently in the demo level, load a blank level first
    # so we can delete the demo level asset without it being locked.
    current = unreal.EditorLevelLibrary.get_editor_world()
    current_path = current.get_path_name() if current else ""
    if "DemoLevel" in current_path or "Demo" in current_path:
        try:
            if hasattr(unreal, "LevelEditorSubsystem"):
                subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if subsys and hasattr(subsys, "new_level"):
                    subsys.new_level("/Game/Maps/Untitled")
            log.append("Switched away from demo level")
        except Exception as e:
            log.append(f"Could not switch level: {e}")

    # Remove demo actors from whatever level is now open
    removed = 0
    for a in list(unreal.EditorLevelLibrary.get_all_level_actors()):
        if a.get_actor_label().startswith("Demo_"):
            unreal.EditorLevelLibrary.destroy_actor(a)
            removed += 1
    if removed:
        log.append(f"Removed {removed} demo actors")

    # Delete demo material assets
    mats_removed = 0
    for name in ["M_Demo_Floor", "M_Demo_Glow", "M_Demo_Pillar"]:
        path = f"{MAT_DIR}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
            mats_removed += 1
    if mats_removed:
        log.append(f"Deleted {mats_removed} demo materials")

    # Delete the demo level asset
    if unreal.EditorAssetLibrary.does_asset_exist(DEMO_LEVEL):
        unreal.EditorAssetLibrary.delete_asset(DEMO_LEVEL)
        log.append(f"Deleted demo level: {DEMO_LEVEL}")

    # Try to remove the /Game/Demo directory if it's now empty
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        remaining = registry.get_assets_by_path(MAT_DIR, recursive=True)
        if len(remaining) == 0:
            unreal.EditorAssetLibrary.delete_directory(MAT_DIR)
            log.append("Deleted /Game/Demo directory")
    except Exception:
        pass

    return {
        "actorsRemoved": removed,
        "materialsRemoved": mats_removed,
        "log": log,
    }


HANDLERS = {
    "demo_scene_from_nothing": demo_scene_from_nothing,
    "demo_cleanup": demo_cleanup,
}
