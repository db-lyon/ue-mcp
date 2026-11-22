# Task 01: Blueprint function graph. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "01_blueprint_function"
PATH = "/Game/Bench/BP_Bench_Math"


def check():
    bp = unreal.load_asset(PATH)
    if not isinstance(bp, unreal.Blueprint):
        return False, "%s is not a Blueprint asset" % PATH
    cls = unreal.load_class(None, PATH + ".BP_Bench_Math_C")
    if cls is None:
        return False, "no generated class; the Blueprint may not compile"
    cdo = unreal.get_default_object(cls)
    if not isinstance(cdo, unreal.Actor):
        return False, "parent class is not Actor"
    for a, b in ((2, 3), (-4, 10), (1000, 234)):
        try:
            got = cdo.call_method("AddBench", (a, b))
        except Exception as e:  # missing function or wrong signature
            return False, "calling AddBench(%d, %d) failed: %s" % (a, b, e)
        if isinstance(got, tuple):
            got = got[0] if got else None
        if got != a + b:
            return False, "AddBench(%d, %d) returned %r, expected %d" % (a, b, got, a + b)
    return True, "AddBench returns A + B for three input pairs"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
