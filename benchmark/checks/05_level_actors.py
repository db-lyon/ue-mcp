# Task 05: level and actors. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "05_level_actors"
LEVEL = "/Game/Bench/L_Bench"
CUBE = "/Engine/BasicShapes/Cube.Cube"
MI = "/Game/Bench/MI_Bench_Blue.MI_Bench_Blue"


def editor_world():
    # The subsystem answers None while PIE runs; the editor's copy of L_Bench is then the loaded map itself.
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world() or unreal.find_object(None, LEVEL + ".L_Bench")


def by_label(world, label):
    return [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if a.get_actor_label() == label]


def near(a, b, tol=0.5):
    return abs(a - b) <= tol


def check():
    if not unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_package_name(LEVEL):
        return False, "no saved level at %s" % LEVEL
    world = editor_world()
    if world.get_outermost().get_name() != LEVEL:
        return False, "the open level is %s, expected %s" % (world.get_outermost().get_name(), LEVEL)
    cubes = by_label(world, "Bench_Cube")
    if len(cubes) != 1 or not isinstance(cubes[0], unreal.StaticMeshActor):
        return False, "expected one StaticMeshActor labelled Bench_Cube, found %d actors with that label" % len(cubes)
    cube = cubes[0]
    smc = cube.static_mesh_component
    mesh = smc.get_editor_property("static_mesh")
    if mesh is None or mesh.get_path_name() != CUBE:
        return False, "Bench_Cube mesh is %s, expected %s" % (mesh.get_path_name() if mesh else None, CUBE)
    mat = smc.get_material(0)
    if mat is None or mat.get_path_name() != MI:
        return False, "Bench_Cube material slot 0 is %s, expected %s" % (mat.get_path_name() if mat else None, MI)
    loc = cube.get_actor_location()
    if not (near(loc.x, 100) and near(loc.y, 200) and near(loc.z, 50)):
        return False, "Bench_Cube is at %s, expected (100, 200, 50)" % loc
    scale = cube.get_actor_scale3d()
    if not (near(scale.x, 2, 0.01) and near(scale.y, 2, 0.01) and near(scale.z, 2, 0.01)):
        return False, "Bench_Cube scale is %s, expected (2, 2, 2)" % scale
    lights = by_label(world, "Bench_Light")
    if len(lights) != 1 or not isinstance(lights[0], unreal.PointLight):
        return False, "expected one PointLight labelled Bench_Light, found %d actors with that label" % len(lights)
    light = lights[0]
    if light.get_attach_parent_actor() != cube:
        return False, "Bench_Light is not attached to Bench_Cube"
    intensity = light.point_light_component.get_editor_property("intensity")
    if not near(intensity, 5000, 1):
        return False, "Bench_Light intensity is %s, expected 5000" % intensity
    return True, "L_Bench is open with Bench_Cube (placed, scaled, MI_Bench_Blue) and Bench_Light attached to it"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
