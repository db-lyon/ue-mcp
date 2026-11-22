# Task 08: PCG scatter. Prints one line: PASS or FAIL.
# Run in the editor (Output Log, Python mode) or through benchmark/run_checks.py.
import unreal

ID = "08_pcg_scatter"
GRAPH = "/Game/Bench/PCG_Bench.PCG_Bench"
CUBE = "/Engine/BasicShapes/Cube.Cube"


def editor_world():
    # The subsystem answers None while PIE runs; the editor's copy of L_Bench is then the loaded map itself.
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world() or unreal.find_object(None, "/Game/Bench/L_Bench.L_Bench")


def graph_of(component):
    try:
        graph = component.get_graph()
    except Exception:
        graph = component.get_editor_property("graph_instance").get_editor_property("graph")
    return graph


def check():
    if not isinstance(unreal.load_asset(GRAPH), unreal.PCGGraph):
        return False, "no PCG graph at %s" % GRAPH
    world = editor_world()
    found = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if a.get_actor_label() == "Bench_PCG"]
    if len(found) != 1:
        return False, "expected one actor labelled Bench_PCG in the open level, found %d" % len(found)
    actor = found[0]
    pcgs = actor.get_components_by_class(unreal.PCGComponent)
    if not pcgs:
        return False, "Bench_PCG has no PCG component"
    graph = graph_of(pcgs[0])
    if graph is None or graph.get_path_name() != GRAPH:
        return False, "Bench_PCG runs %s, expected %s" % (graph.get_path_name() if graph else None, GRAPH)
    instances = 0
    for ism in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
        mesh = ism.get_editor_property("static_mesh")
        if mesh is not None and mesh.get_path_name() == CUBE:
            instances += ism.get_instance_count()
    if instances < 1:
        return False, "no generated Cube instances on Bench_PCG; the graph has not been generated or spawns nothing"
    return True, "Bench_PCG runs PCG_Bench and generated %d Cube instances" % instances


try:
    ok, detail = check()
except Exception as e:
    ok, detail = False, "check raised %s: %s" % (type(e).__name__, e)
print("%s %s: %s" % ("PASS" if ok else "FAIL", ID, detail))
