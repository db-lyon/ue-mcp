# Task 06: Sequencer animation. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "06_sequencer_animation"
PATH = "/Game/Bench/LS_Bench"


def near(a, b, tol=0.5):
    return abs(a - b) <= tol


def location_x_keys(binding):
    """(frame at display rate, value) for each key on the binding's Location.X channel."""
    tracks = binding.find_tracks_by_type(unreal.MovieScene3DTransformTrack)
    if not tracks:
        return None
    keys = []
    for section in tracks[0].get_sections():
        channels = section.get_all_channels()
        if not channels:
            continue
        # A 3D transform section's channels start Location.X, Location.Y, Location.Z.
        for key in channels[0].get_keys():
            frame = key.get_time(unreal.MovieSceneTimeUnit.DISPLAY_RATE).frame_number.value
            keys.append((frame, key.get_value()))
    return keys


def check():
    seq = unreal.load_asset(PATH)
    if not isinstance(seq, unreal.LevelSequence):
        return False, "%s is not a Level Sequence" % PATH
    bindings = [b for b in seq.get_bindings() if str(b.get_display_name()) == "Bench_Cube"]
    if not bindings:
        return False, "no binding named Bench_Cube (bindings: %s)" % [str(b.get_display_name()) for b in seq.get_bindings()]
    keys = location_x_keys(bindings[0])
    if keys is None:
        return False, "the Bench_Cube binding has no Transform track"
    at0 = [v for f, v in keys if f == 0]
    at60 = [v for f, v in keys if f == 60]
    if not at0 or not near(at0[0], 100):
        return False, "no Location.X key of 100 at frame 0 (keys: %s)" % keys
    if not at60 or not near(at60[0], 600):
        return False, "no Location.X key of 600 at frame 60 (keys: %s)" % keys
    return True, "LS_Bench moves Bench_Cube from X=100 at frame 0 to X=600 at frame 60"


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
