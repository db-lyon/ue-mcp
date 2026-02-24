"""
PCG (Procedural Content Generation) handlers — read and author PCG graphs,
inspect PCG components, trigger generation.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def list_pcg_graphs(params: dict) -> dict:
    """List PCG graph assets in a directory."""
    directory = params.get("directory", "/Game/")
    recursive = params.get("recursive", True)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    graphs = []

    try:
        assets = unreal.EditorAssetLibrary.list_assets(directory, recursive=recursive)
        for asset_path in assets:
            try:
                asset_data = registry.get_asset_by_object_path(asset_path)
                if asset_data:
                    class_name = ""
                    try:
                        class_name = str(asset_data.get_editor_property("asset_class_path").get_editor_property("asset_name"))
                    except Exception:
                        try:
                            class_name = str(asset_data.asset_class)
                        except Exception:
                            pass

                    if "pcggraph" in class_name.lower():
                        graphs.append({
                            "path": str(asset_path),
                            "name": str(asset_path).split("/")[-1].split(".")[0],
                        })
            except Exception:
                continue
    except Exception as e:
        return {"error": str(e), "directory": directory}

    return {
        "directory": directory,
        "recursive": recursive,
        "count": len(graphs),
        "graphs": graphs,
    }


def read_pcg_graph(params: dict) -> dict:
    """Read a PCG graph's full structure: nodes, edges, parameters."""
    graph_path = params.get("graphPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(graph_path)
    if asset is None:
        raise ValueError(f"PCG graph not found: {graph_path}")

    result = {
        "graphPath": graph_path,
        "class": asset.get_class().get_name(),
        "name": asset.get_name(),
    }

    nodes = []
    edges = []

    try:
        graph = asset
        if hasattr(asset, "get_editor_property"):
            try:
                graph = asset.get_editor_property("graph") or asset
            except Exception:
                pass

        if hasattr(graph, "get_editor_property"):
            graph_nodes = None
            for attr in ["nodes", "graph_nodes", "extra_nodes"]:
                try:
                    graph_nodes = graph.get_editor_property(attr)
                    if graph_nodes:
                        break
                except Exception:
                    continue

            if graph_nodes:
                for node in graph_nodes:
                    node_info = {
                        "name": node.get_name(),
                        "class": node.get_class().get_name(),
                    }
                    try:
                        settings = node.get_editor_property("settings") if hasattr(node, "get_editor_property") else None
                        if settings:
                            node_info["settingsClass"] = settings.get_class().get_name()
                    except Exception:
                        pass
                    try:
                        node_info["comment"] = str(node.get_editor_property("node_comment")) if hasattr(node, "get_editor_property") else None
                    except Exception:
                        pass
                    nodes.append(node_info)

            try:
                graph_edges = graph.get_editor_property("edges") if hasattr(graph, "get_editor_property") else []
                for edge in (graph_edges or []):
                    edge_info = {}
                    try:
                        edge_info["source"] = edge.get_editor_property("input_node").get_name() if hasattr(edge, "get_editor_property") else None
                    except Exception:
                        pass
                    try:
                        edge_info["target"] = edge.get_editor_property("output_node").get_name() if hasattr(edge, "get_editor_property") else None
                    except Exception:
                        pass
                    try:
                        edge_info["sourcePin"] = str(edge.get_editor_property("input_pin_label"))
                    except Exception:
                        pass
                    try:
                        edge_info["targetPin"] = str(edge.get_editor_property("output_pin_label"))
                    except Exception:
                        pass
                    edges.append(edge_info)
            except Exception:
                pass
    except Exception as e:
        result["parseError"] = str(e)

    result["nodeCount"] = len(nodes)
    result["edgeCount"] = len(edges)
    result["nodes"] = nodes
    result["edges"] = edges

    return result


def read_pcg_node_settings(params: dict) -> dict:
    """Read detailed settings of a specific PCG node."""
    graph_path = params.get("graphPath", "")
    node_name = params.get("nodeName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(graph_path)
    if asset is None:
        raise ValueError(f"PCG graph not found: {graph_path}")

    target_node = _find_pcg_node(asset, node_name)
    if target_node is None:
        raise ValueError(f"Node not found: {node_name}")

    result = {
        "graphPath": graph_path,
        "nodeName": node_name,
        "class": target_node.get_class().get_name(),
    }

    settings = {}
    try:
        settings_obj = target_node.get_editor_property("settings") if hasattr(target_node, "get_editor_property") else None
        if settings_obj:
            result["settingsClass"] = settings_obj.get_class().get_name()
            for prop_name in dir(settings_obj):
                if prop_name.startswith("_"):
                    continue
                try:
                    val = getattr(settings_obj, prop_name)
                    if callable(val):
                        continue
                    if isinstance(val, (bool, int, float, str)):
                        settings[prop_name] = val
                    else:
                        settings[prop_name] = str(val)
                except Exception:
                    continue
    except Exception:
        pass

    result["settings"] = settings
    return result


def get_pcg_components(params: dict) -> dict:
    """List all PCG components in the current level."""
    graph_filter = params.get("graphFilter", None)
    name_filter = params.get("nameFilter", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    components = []

    for actor in all_actors:
        try:
            pcg_comps = actor.get_components_by_class(unreal.PCGComponent) if hasattr(unreal, "PCGComponent") else []
            for comp in pcg_comps:
                comp_info = {
                    "actorName": actor.get_name(),
                    "actorLabel": actor.get_actor_label(),
                    "componentName": comp.get_name(),
                }

                try:
                    graph = comp.get_editor_property("graph")
                    comp_info["graph"] = graph.get_path_name() if graph else None
                except Exception:
                    comp_info["graph"] = None

                try:
                    comp_info["seed"] = comp.get_editor_property("seed")
                except Exception:
                    pass

                try:
                    comp_info["generationTrigger"] = str(comp.get_editor_property("generation_trigger"))
                except Exception:
                    pass

                if graph_filter and comp_info.get("graph"):
                    if graph_filter.lower() not in comp_info["graph"].lower():
                        continue
                if name_filter:
                    if name_filter.lower() not in comp_info["actorName"].lower() and name_filter.lower() not in comp_info["actorLabel"].lower():
                        continue

                components.append(comp_info)
        except Exception:
            continue

    return {
        "count": len(components),
        "graphFilter": graph_filter,
        "nameFilter": name_filter,
        "components": components,
    }


def get_pcg_component_details(params: dict) -> dict:
    """Deep inspect a specific PCG component."""
    actor_name = params.get("actorName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    target = None

    for actor in all_actors:
        if actor.get_name() == actor_name or actor.get_actor_label() == actor_name:
            target = actor
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_name}")

    pcg_comps = target.get_components_by_class(unreal.PCGComponent) if hasattr(unreal, "PCGComponent") else []
    if not pcg_comps:
        raise ValueError(f"No PCG component found on actor: {actor_name}")

    comp = pcg_comps[0]
    result = {
        "actorName": actor_name,
        "componentName": comp.get_name(),
    }

    try:
        graph = comp.get_editor_property("graph")
        result["graph"] = graph.get_path_name() if graph else None
    except Exception:
        pass

    properties = {}
    for prop_name in dir(comp):
        if prop_name.startswith("_"):
            continue
        try:
            val = getattr(comp, prop_name)
            if callable(val):
                continue
            if isinstance(val, (bool, int, float, str)):
                properties[prop_name] = val
        except Exception:
            continue
    result["properties"] = properties

    return result


def create_pcg_graph(params: dict) -> dict:
    """Create a new PCG graph asset."""
    graph_path = params.get("graphPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    package_path = "/".join(graph_path.split("/")[:-1])
    asset_name = graph_path.split("/")[-1]

    factory = None
    if hasattr(unreal, "PCGGraphFactory"):
        factory = unreal.PCGGraphFactory()
    elif hasattr(unreal, "PCGGraphAssetFactory"):
        factory = unreal.PCGGraphAssetFactory()

    if factory:
        asset = asset_tools.create_asset(asset_name, package_path, None, factory)
    else:
        asset = asset_tools.create_asset(asset_name, package_path, unreal.PCGGraphInterface if hasattr(unreal, "PCGGraphInterface") else unreal.PCGGraph, None)

    if asset is None:
        raise RuntimeError(f"Failed to create PCG graph at {graph_path}")

    return {
        "success": True,
        "graphPath": graph_path,
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


def add_pcg_node(params: dict) -> dict:
    """Add a node to a PCG graph."""
    graph_path = params.get("graphPath", "")
    node_type = params.get("nodeType", "")
    settings = params.get("settings", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(graph_path)
    if asset is None:
        raise ValueError(f"PCG graph not found: {graph_path}")

    node_class = getattr(unreal, node_type, None)
    if node_class is None:
        for prefix in ["PCG", "PCGSettings_", "PCGSettings", ""]:
            candidate = getattr(unreal, f"{prefix}{node_type}", None)
            if candidate:
                node_class = candidate
                break

    if node_class is None:
        raise ValueError(f"PCG node type not found: {node_type}. Try the full class name.")

    graph = asset
    try:
        graph = asset.get_editor_property("graph") or asset
    except Exception:
        pass

    new_node = None
    if hasattr(graph, "add_node"):
        new_node = graph.add_node(node_class)
    elif hasattr(graph, "create_node"):
        new_node = graph.create_node(node_class)

    if new_node is None:
        raise RuntimeError(f"Failed to add node of type {node_type}")

    if settings and isinstance(settings, dict):
        try:
            settings_obj = new_node.get_editor_property("settings")
            if settings_obj:
                for key, value in settings.items():
                    try:
                        settings_obj.set_editor_property(key, value)
                    except Exception:
                        pass
        except Exception:
            pass

    return {
        "success": True,
        "nodeName": new_node.get_name(),
        "nodeClass": new_node.get_class().get_name(),
        "graphPath": graph_path,
    }


def connect_pcg_nodes(params: dict) -> dict:
    """Connect two PCG nodes via their pins."""
    graph_path = params.get("graphPath", "")
    source_node = params.get("sourceNode", "")
    source_pin = params.get("sourcePin", "Out")
    target_node = params.get("targetNode", "")
    target_pin = params.get("targetPin", "In")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(graph_path)
    if asset is None:
        raise ValueError(f"PCG graph not found: {graph_path}")

    src = _find_pcg_node(asset, source_node)
    tgt = _find_pcg_node(asset, target_node)

    if src is None:
        raise ValueError(f"Source node not found: {source_node}")
    if tgt is None:
        raise ValueError(f"Target node not found: {target_node}")

    graph = asset
    try:
        graph = asset.get_editor_property("graph") or asset
    except Exception:
        pass

    success = False
    if hasattr(graph, "add_edge"):
        success = graph.add_edge(src, source_pin, tgt, target_pin)
    elif hasattr(graph, "connect_nodes"):
        success = graph.connect_nodes(src, source_pin, tgt, target_pin)

    return {
        "success": bool(success),
        "sourceNode": source_node,
        "sourcePin": source_pin,
        "targetNode": target_node,
        "targetPin": target_pin,
    }


def set_pcg_node_settings(params: dict) -> dict:
    """Set parameters on an existing PCG node (partial update)."""
    graph_path = params.get("graphPath", "")
    node_name = params.get("nodeName", "")
    settings = params.get("settings", {})

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(graph_path)
    if asset is None:
        raise ValueError(f"PCG graph not found: {graph_path}")

    node = _find_pcg_node(asset, node_name)
    if node is None:
        raise ValueError(f"Node not found: {node_name}")

    updated = []
    failed = []
    try:
        settings_obj = node.get_editor_property("settings")
        if settings_obj:
            for key, value in settings.items():
                try:
                    settings_obj.set_editor_property(key, value)
                    updated.append(key)
                except Exception as e:
                    failed.append({"key": key, "error": str(e)})
    except Exception as e:
        raise RuntimeError(f"Cannot access node settings: {e}")

    return {
        "success": len(failed) == 0,
        "nodeName": node_name,
        "updated": updated,
        "failed": failed,
    }


def remove_pcg_node(params: dict) -> dict:
    """Remove a node from a PCG graph."""
    graph_path = params.get("graphPath", "")
    node_name = params.get("nodeName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(graph_path)
    if asset is None:
        raise ValueError(f"PCG graph not found: {graph_path}")

    node = _find_pcg_node(asset, node_name)
    if node is None:
        raise ValueError(f"Node not found: {node_name}")

    graph = asset
    try:
        graph = asset.get_editor_property("graph") or asset
    except Exception:
        pass

    success = False
    if hasattr(graph, "remove_node"):
        success = graph.remove_node(node)
    elif hasattr(graph, "delete_node"):
        success = graph.delete_node(node)

    return {
        "success": bool(success),
        "nodeName": node_name,
    }


def execute_pcg_graph(params: dict) -> dict:
    """Trigger regeneration of a PCG component in the level."""
    actor_name = params.get("actorName", "")
    seed = params.get("seed", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    target = None

    for actor in all_actors:
        if actor.get_name() == actor_name or actor.get_actor_label() == actor_name:
            target = actor
            break

    if target is None:
        raise ValueError(f"Actor not found: {actor_name}")

    pcg_comps = target.get_components_by_class(unreal.PCGComponent) if hasattr(unreal, "PCGComponent") else []
    if not pcg_comps:
        raise ValueError(f"No PCG component on actor: {actor_name}")

    comp = pcg_comps[0]

    if seed is not None:
        try:
            comp.set_editor_property("seed", seed)
        except Exception:
            pass

    try:
        if hasattr(comp, "generate"):
            comp.generate()
        elif hasattr(comp, "generate_local"):
            comp.generate_local(True)
        elif hasattr(comp, "notify_properties_changed_from_blueprint"):
            comp.notify_properties_changed_from_blueprint()
    except Exception as e:
        raise RuntimeError(f"Failed to execute PCG graph: {e}")

    return {
        "success": True,
        "actorName": actor_name,
        "seed": seed,
    }


def add_pcg_volume(params: dict) -> dict:
    """Place a PCG volume in the level with a graph and bounds."""
    graph_path = params.get("graphPath", "")
    location = params.get("location", {"x": 0, "y": 0, "z": 0})
    bounds = params.get("bounds", {"x": 1000, "y": 1000, "z": 500})
    seed = params.get("seed", 42)
    label = params.get("label", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    loc = unreal.Vector(location.get("x", 0), location.get("y", 0), location.get("z", 0))

    volume_class = getattr(unreal, "PCGVolume", None) or getattr(unreal, "APCGVolume", None)
    if volume_class is None:
        raise RuntimeError("PCGVolume class not found in this engine version")

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(volume_class, loc)
    if actor is None:
        raise RuntimeError("Failed to spawn PCG volume")

    if label:
        actor.set_actor_label(label)

    try:
        actor.set_actor_scale3d(unreal.Vector(
            bounds.get("x", 1000) / 100.0,
            bounds.get("y", 1000) / 100.0,
            bounds.get("z", 500) / 100.0))
    except Exception:
        pass

    try:
        pcg_comps = actor.get_components_by_class(unreal.PCGComponent)
        if pcg_comps:
            comp = pcg_comps[0]
            graph = unreal.EditorAssetLibrary.load_asset(graph_path)
            if graph:
                comp.set_editor_property("graph", graph)
            comp.set_editor_property("seed", seed)
    except Exception as e:
        return {"success": True, "warning": f"Volume placed but graph/seed setup failed: {e}",
                "actorName": actor.get_name()}

    return {
        "success": True,
        "actorName": actor.get_name(),
        "actorLabel": actor.get_actor_label(),
        "graphPath": graph_path,
        "seed": seed,
    }


def _find_pcg_node(asset, node_name: str):
    """Find a node in a PCG graph by name."""
    graph = asset
    try:
        graph = asset.get_editor_property("graph") or asset
    except Exception:
        pass

    for attr in ["nodes", "graph_nodes", "extra_nodes"]:
        try:
            nodes = graph.get_editor_property(attr)
            if nodes:
                for node in nodes:
                    if node.get_name() == node_name:
                        return node
        except Exception:
            continue

    return None


HANDLERS = {
    "list_pcg_graphs": list_pcg_graphs,
    "read_pcg_graph": read_pcg_graph,
    "read_pcg_node_settings": read_pcg_node_settings,
    "get_pcg_components": get_pcg_components,
    "get_pcg_component_details": get_pcg_component_details,
    "create_pcg_graph": create_pcg_graph,
    "add_pcg_node": add_pcg_node,
    "connect_pcg_nodes": connect_pcg_nodes,
    "set_pcg_node_settings": set_pcg_node_settings,
    "remove_pcg_node": remove_pcg_node,
    "execute_pcg_graph": execute_pcg_graph,
    "add_pcg_volume": add_pcg_volume,
}
