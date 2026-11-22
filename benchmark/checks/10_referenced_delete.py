# Task 10: delete an asset that another asset references. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "10_referenced_delete"
DELETED = "/Game/Bench/M_Bench_Ref"
REFERENCER = "/Game/Bench/MI_Bench_Ref"


def check():
    if unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_package_name(DELETED):
        return False, "%s still exists" % DELETED
    mi = unreal.load_asset(REFERENCER)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        return False, "%s does not exist; it should remain after the delete" % REFERENCER
    parent = mi.get_editor_property("parent")
    if parent is not None and parent.get_path_name().startswith(DELETED):
        return False, "MI_Bench_Ref still points at the deleted material"
    return True, "M_Bench_Ref is deleted, MI_Bench_Ref remains with its reference cleared or replaced"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
