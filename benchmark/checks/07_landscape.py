# Task 07: landscape. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "07_landscape"


def editor_world():
    # The subsystem answers None while PIE runs; the editor's copy of L_Bench is then the loaded map itself.
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world() or unreal.find_object(None, "/Game/Bench/L_Bench.L_Bench")


def check():
    world = editor_world()
    found = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape) if a.get_actor_label() == "Bench_Landscape"]
    if len(found) != 1:
        return False, "expected one Landscape labelled Bench_Landscape in the open level, found %d" % len(found)
    landscape = found[0]
    comps = landscape.get_components_by_class(unreal.LandscapeComponent)
    if len(comps) < 4:
        return False, "Bench_Landscape has %d components, expected at least 4 (2 x 2)" % len(comps)
    origin, extent = landscape.get_actor_bounds(False)
    if abs(origin.x) > extent.x or abs(origin.y) > extent.y:
        return False, "the world origin is not inside Bench_Landscape (bounds centre %s, extent %s)" % (origin, extent)
    relief = 2 * extent.z
    if relief < 290:
        return False, "the surface spans %.0f units from lowest to highest point, expected a hill of at least 300" % relief
    return True, "Bench_Landscape covers the origin with %d components and %.0f units of relief" % (len(comps), relief)


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
