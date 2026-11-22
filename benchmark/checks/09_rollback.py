# Task 09: rollback of a failed multi-step edit. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "09_rollback"


def editor_world():
    # The subsystem answers None while PIE runs; the editor's copy of L_Bench is then the loaded map itself.
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world() or unreal.find_object(None, "/Game/Bench/L_Bench.L_Bench")


def labelled(world, label):
    return [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if a.get_actor_label() == label]


def check():
    world = editor_world()
    keep = labelled(world, "Bench_Keep")
    if len(keep) != 1 or not isinstance(keep[0], unreal.PointLight):
        return False, "expected one PointLight labelled Bench_Keep, found %d actors with that label" % len(keep)
    left = labelled(world, "Bench_Rollback")
    if left:
        return False, "Bench_Rollback is still in the level; the failed operation was not rolled back"
    return True, "Bench_Keep is present and nothing from the failed operation remains"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
