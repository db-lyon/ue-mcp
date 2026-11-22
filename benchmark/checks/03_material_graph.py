# Task 03: material graph. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "03_material_graph"
PATH = "/Game/Bench/M_Bench"


def near(a, b, tol=0.01):
    return abs(a - b) <= tol


def check():
    mat = unreal.load_asset(PATH)
    if not isinstance(mat, unreal.Material):
        return False, "%s is not a Material" % PATH
    mel = unreal.MaterialEditingLibrary
    base = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    if not isinstance(base, unreal.MaterialExpressionVectorParameter):
        return False, "Base Color is fed by %s, expected a Vector Parameter" % (type(base).__name__ if base else "nothing")
    if str(base.get_editor_property("parameter_name")) != "Tint":
        return False, "the Base Color parameter is named %s, expected Tint" % base.get_editor_property("parameter_name")
    tint = mel.get_material_default_vector_parameter_value(mat, "Tint")
    if not (near(tint.r, 1) and near(tint.g, 0) and near(tint.b, 0)):
        return False, "Tint default is %s, expected (1, 0, 0)" % tint
    rough = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_ROUGHNESS)
    if not isinstance(rough, unreal.MaterialExpressionScalarParameter):
        return False, "Roughness is fed by %s, expected a Scalar Parameter" % (type(rough).__name__ if rough else "nothing")
    if str(rough.get_editor_property("parameter_name")) != "Rough":
        return False, "the Roughness parameter is named %s, expected Rough" % rough.get_editor_property("parameter_name")
    value = mel.get_material_default_scalar_parameter_value(mat, "Rough")
    if not near(value, 0.25):
        return False, "Rough default is %s, expected 0.25" % value
    return True, "Tint (1,0,0) feeds Base Color and Rough (0.25) feeds Roughness"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
