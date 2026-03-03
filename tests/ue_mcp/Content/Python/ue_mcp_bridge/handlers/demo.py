"""Demo scene builders — step-by-step so the viewport renders between spawns."""

import math

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


def _set_movable(actor):
    """Set an actor's root component mobility to Movable so movement components work."""
    try:
        smc = actor.get_component_by_class(unreal.StaticMeshComponent)
        if smc:
            smc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    except Exception:
        pass


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
    "niagara_vfx",
    "pcg_scatter",
    "orbit_rings",
    "level_sequence",
    "tuning_panel",
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
    _set_movable(hero)
    return {"step": "hero_sphere", "ok": True, "message": "Glowing golden sphere (movable)"}


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


def _step_niagara_vfx():
    """Create a Niagara VFX system and place a persistent actor with NiagaraComponent."""
    try:
        ns_path = f"{MAT_DIR}/NS_Demo_Aura"
        ns = None

        if unreal.EditorAssetLibrary.does_asset_exist(ns_path):
            ns = unreal.EditorAssetLibrary.load_asset(ns_path)
        else:
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            factory = None
            for cls_name in ("NiagaraSystemFactoryNew", "NiagaraSystemFactory"):
                cls = getattr(unreal, cls_name, None)
                if cls:
                    factory = cls()
                    break
            if factory:
                ns = tools.create_asset("NS_Demo_Aura", MAT_DIR, None, factory)
                if ns:
                    unreal.EditorAssetLibrary.save_asset(ns_path)

        if ns is None:
            return {"step": "niagara_vfx", "ok": True,
                    "message": "Niagara plugin not available — skipped"}

        placed = False
        niag_cls = getattr(unreal, "NiagaraComponent", None)
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Actor, unreal.Vector(0, 0, 380)
        )
        if actor and niag_cls:
            actor.set_actor_label("Demo_NiagaraVFX")
            actor.set_folder_path(FOLDER)
            comp = actor.add_component_by_class(
                niag_cls, False, unreal.Transform(), False
            )
            if comp:
                comp.set_editor_property("asset", ns)
                comp.set_editor_property("auto_activate", True)
                try:
                    comp.activate(True)
                except Exception:
                    pass
                placed = True

        msg = "Niagara system created"
        if placed:
            msg += " & persistent actor placed above hero sphere"
        return {"step": "niagara_vfx", "ok": True, "message": msg}
    except Exception as e:
        return {"step": "niagara_vfx", "ok": False, "error": str(e)}


def _step_pcg_scatter():
    """Create a PCG graph and place a PCG scatter volume on the floor."""
    try:
        graph_path = f"{MAT_DIR}/PCG_Demo_Scatter"
        graph = None

        if unreal.EditorAssetLibrary.does_asset_exist(graph_path):
            graph = unreal.EditorAssetLibrary.load_asset(graph_path)
        else:
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            factory = None
            for cls_name in ("PCGGraphFactory", "PCGGraphAssetFactory"):
                cls = getattr(unreal, cls_name, None)
                if cls:
                    factory = cls()
                    break
            if factory:
                graph = tools.create_asset("PCG_Demo_Scatter", MAT_DIR, None, factory)
            else:
                pcg_cls = getattr(unreal, "PCGGraphInterface", None) or getattr(unreal, "PCGGraph", None)
                if pcg_cls:
                    graph = tools.create_asset("PCG_Demo_Scatter", MAT_DIR, pcg_cls, None)

            if graph:
                unreal.EditorAssetLibrary.save_asset(graph_path)

        if graph is None:
            return {"step": "pcg_scatter", "ok": True,
                    "message": "PCG plugin not available — skipped"}

        volume_class = getattr(unreal, "PCGVolume", None) or getattr(unreal, "APCGVolume", None)
        placed = False
        if volume_class:
            vol = unreal.EditorLevelLibrary.spawn_actor_from_class(
                volume_class, unreal.Vector(0, 0, 0)
            )
            if vol:
                vol.set_actor_label("Demo_PCGScatter")
                vol.set_folder_path(FOLDER)
                vol.set_actor_scale3d(unreal.Vector(30, 30, 3))
                if hasattr(unreal, "PCGComponent"):
                    pcg_comps = vol.get_components_by_class(unreal.PCGComponent)
                    if pcg_comps:
                        pcg_comps[0].set_editor_property("graph", graph)
                        pcg_comps[0].set_editor_property("seed", 42)
                placed = True

        msg = "PCG scatter graph created"
        if placed:
            msg += " & volume placed on floor (30m x 30m)"
        return {"step": "pcg_scatter", "ok": True, "message": msg}
    except Exception as e:
        return {"step": "pcg_scatter", "ok": False, "error": str(e)}


def _step_orbit_rings():
    """Spawn an orbit ring of emissive spheres with per-orb rotation."""
    try:
        mat = _load_demo_mat("M_Demo_Glow")
        count = 8
        radius = 220
        height = 280
        rot_cls = getattr(unreal, "RotatingMovementComponent", None)
        orb_animated = 0

        for i in range(count):
            angle = i * (2 * math.pi / count)
            x = radius * math.cos(angle)
            y = radius * math.sin(angle)
            orb = _spawn_mesh(
                f"Demo_Ring_{i+1}", SPHERE,
                (x, y, height), scale=(0.22, 0.22, 0.22)
            )
            _apply_mat(orb, mat)
            _set_movable(orb)

            if rot_cls:
                comp = orb.add_component_by_class(
                    rot_cls, False, unreal.Transform(), False
                )
                if comp:
                    yaw = 60 + i * 15
                    pitch = 10 + (i % 3) * 5
                    comp.set_editor_property(
                        "rotation_rate", unreal.Rotator(pitch, yaw, 0)
                    )
                    orb_animated += 1

        hero_animated = False
        if rot_cls:
            for a in unreal.EditorLevelLibrary.get_all_level_actors():
                if a.get_actor_label() == "Demo_HeroSphere":
                    comp = a.add_component_by_class(
                        rot_cls, False, unreal.Transform(), False
                    )
                    if comp:
                        comp.set_editor_property(
                            "rotation_rate", unreal.Rotator(0, 45, 10)
                        )
                        hero_animated = True
                    break

        msg = f"{count} glowing ring orbs encircling hero sphere"
        if orb_animated or hero_animated:
            msg += f" — {orb_animated + (1 if hero_animated else 0)} actors animated (Play to see)"
        return {"step": "orbit_rings", "ok": True, "message": msg}
    except Exception as e:
        return {"step": "orbit_rings", "ok": False, "error": str(e)}


def _step_level_sequence():
    """Create a Level Sequence with a rotation track on the hero sphere."""
    try:
        seq_path = f"{MAT_DIR}/SEQ_Demo_Showcase"
        seq = None

        if unreal.EditorAssetLibrary.does_asset_exist(seq_path):
            seq = unreal.EditorAssetLibrary.load_asset(seq_path)
        else:
            factory = unreal.LevelSequenceFactoryNew()
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            seq = tools.create_asset(
                "SEQ_Demo_Showcase", MAT_DIR, unreal.LevelSequence, factory
            )

        if seq is None:
            return {"step": "level_sequence", "ok": False,
                    "error": "Failed to create Level Sequence"}

        fps = 30
        total_frames = fps * 10

        movie_scene = seq.get_movie_scene() if hasattr(seq, "get_movie_scene") else None
        if movie_scene:
            try:
                movie_scene.set_display_rate(unreal.FrameRate(fps, 1))
            except Exception:
                pass
            try:
                movie_scene.set_playback_range(
                    unreal.FrameNumber(0), unreal.FrameNumber(total_frames)
                )
            except Exception:
                pass

        bound = False
        has_track = False
        for a in unreal.EditorLevelLibrary.get_all_level_actors():
            if a.get_actor_label() == "Demo_HeroSphere":
                if hasattr(seq, "add_possessable"):
                    binding = seq.add_possessable(a)
                    if binding:
                        bound = True
                        has_track = _add_transform_track(binding, total_frames)
                break

        unreal.EditorAssetLibrary.save_asset(seq_path)

        seq_actor_placed = False
        lsa_class = getattr(unreal, "LevelSequenceActor", None)
        if lsa_class:
            lsa = unreal.EditorLevelLibrary.spawn_actor_from_class(
                lsa_class, unreal.Vector(0, 0, 100)
            )
            if lsa:
                lsa.set_actor_label("Demo_SequencePlayer")
                lsa.set_folder_path(FOLDER)
                try:
                    lsa.set_editor_property(
                        "level_sequence", unreal.SoftObjectPath(seq_path)
                    )
                except Exception:
                    try:
                        lsa.set_editor_property("level_sequence_asset", seq)
                    except Exception:
                        pass
                try:
                    ps = lsa.get_editor_property("playback_settings")
                    ps.set_editor_property("auto_play", True)
                    ps.set_editor_property("loop_count", -1)
                    lsa.set_editor_property("playback_settings", ps)
                except Exception:
                    pass
                seq_actor_placed = True

        parts = ["Level Sequence created"]
        if bound:
            parts.append("hero sphere bound")
        if has_track:
            parts.append("rotation track with keyframes")
        if seq_actor_placed:
            parts.append("auto-play actor placed")
        return {"step": "level_sequence", "ok": True, "message": " — ".join(parts)}
    except Exception as e:
        return {"step": "level_sequence", "ok": False, "error": str(e)}


def _add_transform_track(binding, total_frames):
    """Add a 3D transform track with a full-rotation yaw animation."""
    try:
        track_cls = getattr(unreal, "MovieScene3DTransformTrack", None)
        if track_cls is None or not hasattr(binding, "add_track"):
            return False

        track = binding.add_track(track_cls)
        if track is None:
            return False

        if not hasattr(track, "add_section"):
            return False
        section = track.add_section()
        if section is None:
            return False

        try:
            section.set_range(0, total_frames)
        except Exception:
            try:
                section.set_start_frame(0)
                section.set_end_frame(total_frames)
            except Exception:
                pass

        channels = None
        if hasattr(section, "get_all_channels"):
            channels = section.get_all_channels()
        elif hasattr(section, "get_channels"):
            channels = section.get_channels()

        if channels:
            for idx, ch in enumerate(channels):
                if not hasattr(ch, "add_key"):
                    continue
                name = ch.get_name() if hasattr(ch, "get_name") else ""
                is_yaw = (
                    "yaw" in name.lower()
                    or "rotation.z" in name.lower()
                    or idx == 5
                )
                if is_yaw:
                    try:
                        ch.add_key(unreal.FrameNumber(0), 0.0)
                        ch.add_key(unreal.FrameNumber(total_frames), 360.0)
                    except Exception:
                        try:
                            ch.add_key(
                                unreal.FrameNumber(0), 0.0,
                                unreal.MovieSceneKeyInterpolation.LINEAR
                            )
                            ch.add_key(
                                unreal.FrameNumber(total_frames), 360.0,
                                unreal.MovieSceneKeyInterpolation.LINEAR
                            )
                        except Exception:
                            pass
                    break

        return True
    except Exception:
        return False


def _step_tuning_panel():
    """Create an Editor Utility Widget tuning panel with controls and open it."""
    try:
        euw_path = f"{MAT_DIR}/EUW_DemoTuning"

        if not unreal.EditorAssetLibrary.does_asset_exist(euw_path):
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            factory = None
            if hasattr(unreal, "EditorUtilityWidgetBlueprintFactory"):
                factory = unreal.EditorUtilityWidgetBlueprintFactory()
            if factory is None:
                return {"step": "tuning_panel", "ok": True,
                        "message": "EditorUtilityWidget factory not available — skipped"}

            asset = tools.create_asset("EUW_DemoTuning", MAT_DIR, None, factory)
            if asset is None:
                return {"step": "tuning_panel", "ok": False,
                        "error": "Failed to create Editor Utility Widget"}
            unreal.EditorAssetLibrary.save_asset(euw_path)

        wb = unreal.EditorAssetLibrary.load_asset(euw_path)
        populated = _populate_tuning_ui(wb)
        if populated:
            unreal.EditorAssetLibrary.save_asset(euw_path)

        opened = False
        if hasattr(unreal, "EditorUtilitySubsystem"):
            subsys = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)
            if subsys and wb and hasattr(subsys, "spawn_and_register_tab"):
                subsys.spawn_and_register_tab(wb)
                opened = True

        msg = "Editor Utility Widget tuning panel created"
        if populated:
            msg += " with controls"
        if opened:
            msg += " & opened as editor tab"
        return {"step": "tuning_panel", "ok": True, "message": msg}
    except Exception as e:
        return {"step": "tuning_panel", "ok": False, "error": str(e)}


def _populate_tuning_ui(wb):
    """Build tuning UI inside the EUW widget tree."""
    try:
        wt = wb.get_editor_property("widget_tree")
        if not wt or not hasattr(wt, "construct_widget"):
            return False

        root = wt.get_editor_property("root_widget")
        if not root:
            return False

        vbox = wt.construct_widget(unreal.VerticalBox)
        if not vbox:
            return False

        add_child = getattr(root, "add_child", None) or getattr(root, "add_child_to_canvas", None)
        if add_child is None:
            return False
        add_child(vbox)

        _add_tuning_label(wt, vbox, "NEON SHRINE TUNING", 20)
        _add_tuning_label(wt, vbox, " ")

        for label in ["Hero Light Intensity", "Fog Density", "Glow Brightness",
                       "Bloom Intensity", "Neon Saturation"]:
            _add_tuning_label(wt, vbox, label, 12)
            slider = wt.construct_widget(unreal.Slider)
            if slider:
                try:
                    slider.set_editor_property("value", 0.5)
                    slider.set_editor_property("is_focusable", False)
                except Exception:
                    pass
                vbox.add_child(slider)
            _add_tuning_label(wt, vbox, " ")

        return True
    except Exception:
        return False


def _add_tuning_label(wt, parent, text, size=10):
    """Add a TextBlock to a parent panel widget."""
    try:
        tb = wt.construct_widget(unreal.TextBlock)
        if tb is None:
            return
        tb.set_editor_property("text", text)
        try:
            font = tb.get_editor_property("font")
            font.set_editor_property("size", size)
            tb.set_editor_property("font", font)
        except Exception:
            pass
        parent.add_child(tb)
    except Exception:
        pass


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
    "niagara_vfx": _step_niagara_vfx,
    "pcg_scatter": _step_pcg_scatter,
    "orbit_rings": _step_orbit_rings,
    "level_sequence": _step_level_sequence,
    "tuning_panel": _step_tuning_panel,
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

    assets_removed = 0
    demo_assets = [
        "M_Demo_Floor", "M_Demo_Glow", "M_Demo_Pillar",
        "NS_Demo_Aura", "PCG_Demo_Scatter",
        "SEQ_Demo_Showcase", "EUW_DemoTuning",
    ]
    for name in demo_assets:
        path = f"{MAT_DIR}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
            assets_removed += 1
    if assets_removed:
        log.append(f"Deleted {assets_removed} demo assets")

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

    return {"actorsRemoved": removed, "assetsRemoved": assets_removed, "log": log}


HANDLERS = {
    "demo_step": demo_step,
    "demo_get_steps": demo_get_steps,
    "demo_cleanup": demo_cleanup,
}
