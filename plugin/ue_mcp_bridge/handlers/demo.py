"""Demo scene builders — step-by-step so the viewport renders between spawns."""

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


def _load_demo_mat(name):
    path = f"{MAT_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    return None


# ── Step handlers ────────────────────────────────────────────────────
# Each step is a separate RPC call so UE renders between them.

STEP_ORDER = [
    "create_level",
    "materials",
    "floor",
    "pedestal",
    "hero_sphere",
    "pillars",
    "orbs",
    "neon_lights",
    "hero_light",
    "moonlight",
    "sky_light",
    "fog",
    "post_process",
    "save",
]


def demo_step(params: dict) -> dict:
    """Execute a single step of the Neon Shrine demo."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    step = params.get("step", "")
    fn = _STEPS.get(step)
    if fn is None:
        raise ValueError(f"Unknown demo step: {step}. Valid: {STEP_ORDER}")
    return fn()


def demo_get_steps(params: dict) -> dict:
    """Return the ordered list of steps for the demo."""
    return {"steps": STEP_ORDER, "count": len(STEP_ORDER)}


def _step_create_level():
    try:
        if hasattr(unreal, "LevelEditorSubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if subsys and hasattr(subsys, "new_level"):
                subsys.new_level(DEMO_LEVEL)
            elif subsys and hasattr(subsys, "new_level_from_template"):
                subsys.new_level_from_template(DEMO_LEVEL, "")
        else:
            world = unreal.EditorLevelLibrary.get_editor_world()
            unreal.SystemLibrary.execute_console_command(world, "MAP NEW")
    except Exception as e:
        return {"step": "create_level", "ok": False, "error": str(e)}
    return {"step": "create_level", "ok": True, "message": f"Level created: {DEMO_LEVEL}"}


def _step_materials():
    try:
        _make_material("M_Demo_Floor", base_color=(0.015, 0.015, 0.025, 1), roughness=0.12, metallic=0.9)
        _make_material("M_Demo_Glow", base_color=(0, 0, 0, 1), emissive=(5.0, 3.0, 0.4, 1))
        _make_material("M_Demo_Pillar", base_color=(0.06, 0.06, 0.08, 1), roughness=0.35, metallic=0.4)
        return {"step": "materials", "ok": True, "message": "3 materials created (floor, glow, pillar)"}
    except Exception as e:
        return {"step": "materials", "ok": False, "error": str(e)}


def _step_floor():
    floor = _spawn_mesh("Demo_Floor", CUBE, (0, 0, -5), scale=(60, 60, 0.1))
    _apply_mat(floor, _load_demo_mat("M_Demo_Floor"))
    return {"step": "floor", "ok": True, "message": "60m x 60m dark reflective floor"}


def _step_pedestal():
    ped = _spawn_mesh("Demo_Pedestal", CYLINDER, (0, 0, 75), scale=(2.5, 2.5, 1.5))
    _apply_mat(ped, _load_demo_mat("M_Demo_Pillar"))
    return {"step": "pedestal", "ok": True, "message": "Central pedestal"}


def _step_hero_sphere():
    hero = _spawn_mesh("Demo_HeroSphere", SPHERE, (0, 0, 260), scale=(1.8, 1.8, 1.8))
    _apply_mat(hero, _load_demo_mat("M_Demo_Glow"))
    return {"step": "hero_sphere", "ok": True, "message": "Glowing golden sphere"}


def _step_pillars():
    mat = _load_demo_mat("M_Demo_Pillar")
    for i, (x, y) in enumerate([(600, 600), (-600, 600), (-600, -600), (600, -600)]):
        p = _spawn_mesh(f"Demo_Pillar_{i+1}", CYLINDER, (x, y, 200), scale=(0.6, 0.6, 4))
        _apply_mat(p, mat)
    return {"step": "pillars", "ok": True, "message": "4 pillars placed"}


def _step_orbs():
    mat = _load_demo_mat("M_Demo_Glow")
    for i, (x, y) in enumerate([(600, 600), (-600, 600), (-600, -600), (600, -600)]):
        orb = _spawn_mesh(f"Demo_Orb_{i+1}", SPHERE, (x, y, 30), scale=(0.4, 0.4, 0.4))
        _apply_mat(orb, mat)
    return {"step": "orbs", "ok": True, "message": "4 accent orbs at pillar bases"}


def _step_neon_lights():
    lights = [
        ("Demo_Light_Cyan",    (420, 420, 300),  (0, 220, 255),  80000),
        ("Demo_Light_Magenta", (-420, 420, 300),  (255, 0, 180),  80000),
        ("Demo_Light_Amber",   (-420, -420, 300), (255, 170, 0),  80000),
        ("Demo_Light_Violet",  (420, -420, 300),  (130, 0, 255),  80000),
    ]
    for label, pos, color, intensity in lights:
        _spawn_point_light(label, pos, color, intensity)
    return {"step": "neon_lights", "ok": True, "message": "Cyan, magenta, amber, violet neon lights"}


def _step_hero_light():
    _spawn_point_light("Demo_HeroLight", (80, -80, 500), (255, 225, 190), 120000)
    return {"step": "hero_light", "ok": True, "message": "Warm hero key light"}


def _step_moonlight():
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
    return {"step": "moonlight", "ok": True, "message": "Cool directional moonlight"}


def _step_sky_light():
    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0, 0, 500)
    )
    sky.set_actor_label("Demo_SkyLight")
    sky.set_folder_path(FOLDER)
    comp = sky.get_component_by_class(unreal.SkyLightComponent)
    if comp:
        comp.set_editor_property("intensity", 0.3)
        try:
            comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        except Exception:
            pass
    return {"step": "sky_light", "ok": True, "message": "Dim ambient sky light"}


def _step_fog():
    fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0, 0, 50)
    )
    fog.set_actor_label("Demo_Fog")
    fog.set_folder_path(FOLDER)
    comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if comp:
        comp.set_editor_property("fog_density", 0.035)
        comp.set_editor_property("fog_height_falloff", 0.5)
        comp.set_editor_property("fog_max_opacity", 0.85)
        try:
            comp.set_editor_property("fog_inscattering_color", unreal.LinearColor(0.02, 0.02, 0.06, 1))
        except Exception:
            pass
    return {"step": "fog", "ok": True, "message": "Dark blue-purple fog"}


def _step_post_process():
    ppv = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0, 0, 0)
    )
    ppv.set_actor_label("Demo_PostProcess")
    ppv.set_folder_path(FOLDER)
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    for prop, val in [("bloom_intensity", 2.0), ("vignette_intensity", 0.6), ("auto_exposure_bias", -1.0)]:
        try:
            settings.set_editor_property(prop, val)
            settings.set_editor_property(f"override_{prop}", True)
        except Exception:
            pass
    ppv.set_editor_property("settings", settings)
    return {"step": "post_process", "ok": True, "message": "Bloom + vignette + exposure"}


def _step_save():
    try:
        if hasattr(unreal, "LevelEditorSubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if subsys and hasattr(subsys, "save_current_level"):
                subsys.save_current_level()
    except Exception:
        pass
    return {"step": "save", "ok": True, "message": "Level saved"}


_STEPS = {
    "create_level": _step_create_level,
    "materials": _step_materials,
    "floor": _step_floor,
    "pedestal": _step_pedestal,
    "hero_sphere": _step_hero_sphere,
    "pillars": _step_pillars,
    "orbs": _step_orbs,
    "neon_lights": _step_neon_lights,
    "hero_light": _step_hero_light,
    "moonlight": _step_moonlight,
    "sky_light": _step_sky_light,
    "fog": _step_fog,
    "post_process": _step_post_process,
    "save": _step_save,
}


# ── Cleanup ─────────────────────────────────────────────────────────

def demo_cleanup(params: dict) -> dict:
    """Remove demo level, all Demo_ actors, and demo material assets."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    log = []

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

    removed = 0
    for a in list(unreal.EditorLevelLibrary.get_all_level_actors()):
        if a.get_actor_label().startswith("Demo_"):
            unreal.EditorLevelLibrary.destroy_actor(a)
            removed += 1
    if removed:
        log.append(f"Removed {removed} demo actors")

    mats_removed = 0
    for name in ["M_Demo_Floor", "M_Demo_Glow", "M_Demo_Pillar"]:
        path = f"{MAT_DIR}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
            mats_removed += 1
    if mats_removed:
        log.append(f"Deleted {mats_removed} demo materials")

    if unreal.EditorAssetLibrary.does_asset_exist(DEMO_LEVEL):
        unreal.EditorAssetLibrary.delete_asset(DEMO_LEVEL)
        log.append(f"Deleted demo level: {DEMO_LEVEL}")

    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        remaining = registry.get_assets_by_path(MAT_DIR, recursive=True)
        if len(remaining) == 0:
            unreal.EditorAssetLibrary.delete_directory(MAT_DIR)
            log.append("Deleted /Game/Demo directory")
    except Exception:
        pass

    return {"actorsRemoved": removed, "materialsRemoved": mats_removed, "log": log}


HANDLERS = {
    "demo_step": demo_step,
    "demo_get_steps": demo_get_steps,
    "demo_cleanup": demo_cleanup,
}
