# Task 02: Blueprint components. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "02_blueprint_components"
PATH = "/Game/Bench/BP_Bench_Lamp"
CUBE = "/Engine/BasicShapes/Cube.Cube"


def components(bp):
    """Component templates by variable name, read the way the Blueprint editor's Components panel does."""
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    found = {}
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(handle)
        obj = lib.get_object(data)
        if isinstance(obj, unreal.ActorComponent):
            found[str(lib.get_variable_name(data))] = (obj, data, handle)
    return found


def near(a, b, tol=0.5):
    return abs(a - b) <= tol


def check():
    bp = unreal.load_asset(PATH)
    if not isinstance(bp, unreal.Blueprint):
        return False, "%s is not a Blueprint asset" % PATH
    comps = components(bp)
    base, _, base_handle = comps.get("Base", (None, None, None))
    bulb, bulb_data, _ = comps.get("Bulb", (None, None, None))
    if not isinstance(base, unreal.StaticMeshComponent):
        return False, "no StaticMeshComponent named Base (found %s)" % sorted(comps)
    mesh = base.get_editor_property("static_mesh")
    if mesh is None or mesh.get_path_name() != CUBE:
        return False, "Base mesh is %s, expected %s" % (mesh.get_path_name() if mesh else None, CUBE)
    if not isinstance(bulb, unreal.PointLightComponent):
        return False, "no PointLightComponent named Bulb (found %s)" % sorted(comps)
    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_attached_to(bulb_data, base_handle):
        return False, "Bulb is not attached to Base"
    loc =bulb.get_editor_property("relative_location")
    if not (near(loc.x, 0) and near(loc.y, 0) and near(loc.z, 120)):
        return False, "Bulb relative location is %s, expected (0, 0, 120)" % loc
    intensity = bulb.get_editor_property("intensity")
    if not near(intensity, 8000, 1):
        return False, "Bulb intensity is %s, expected 8000" % intensity
    return True, "Base (Cube) and Bulb (point light under Base, z=120, 8000) are on the Blueprint"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
