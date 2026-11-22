# Task 11: Play In Editor. Prints one line: PASS or FAIL.
# Run it while the PIE session is still running, in the editor (Output Log, Python mode)
# or through benchmark/run_checks.py.
import unreal

ID = "11_pie_session"


def check():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if world is None:
        return False, "no Play In Editor session is running"
    if not world.get_outermost().get_name().endswith("L_Bench"):
        return False, "PIE is playing %s, expected L_Bench" % world.get_outermost().get_name()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return False, "the PIE session has no player pawn"
    loc = pawn.get_actor_location()
    if abs(loc.x - 1000) > 50 or abs(loc.y) > 50:
        return False, "the player pawn is at %s, expected X=1000, Y=0 (within 50 units)" % loc
    return True, "PIE is running L_Bench with the player pawn moved to X=1000"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
