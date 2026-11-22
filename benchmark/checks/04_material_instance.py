# Task 04: material instance. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "04_material_instance"
PATH = "/Game/Bench/MI_Bench_Blue"
PARENT = "/Game/Bench/M_Bench.M_Bench"


def near(a, b, tol=0.01):
    return abs(a - b) <= tol


def check():
    mi = unreal.load_asset(PATH)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        return False, "%s is not a Material Instance" % PATH
    parent = mi.get_editor_property("parent")
    if parent is None or parent.get_path_name() != PARENT:
        return False, "parent is %s, expected %s" % (parent.get_path_name() if parent else None, PARENT)
    mel = unreal.MaterialEditingLibrary
    tint = mel.get_material_instance_vector_parameter_value(mi, "Tint")
    if not (near(tint.r, 0) and near(tint.g, 0) and near(tint.b, 1)):
        return False, "Tint is %s, expected (0, 0, 1)" % tint
    rough = mel.get_material_instance_scalar_parameter_value(mi, "Rough")
    if not near(rough, 0.8):
        return False, "Rough is %s, expected 0.8" % rough
    return True, "MI_Bench_Blue overrides Tint (0,0,1) and Rough 0.8 on M_Bench"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
