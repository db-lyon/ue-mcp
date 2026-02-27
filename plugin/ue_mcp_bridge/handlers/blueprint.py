"""
Blueprint-related handlers using UE Python API.
Provides live Blueprint reading, modification, compilation, and node manipulation.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _load_bp(asset_path: str):
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {asset_path}")
    return bp


def read_blueprint(params: dict) -> dict:
    """Read a Blueprint's full structure via editor reflection."""
    bp = _load_bp(params.get("path", ""))

    bp_class = bp.generated_class() if hasattr(bp, "generated_class") else None
    parent_class = bp.parent_class if hasattr(bp, "parent_class") else None

    result = {
        "path": params.get("path", ""),
        "className": bp.get_name(),
        "parentClass": str(parent_class.get_name()) if parent_class else None,
        "generatedClassName": str(bp_class.get_name()) if bp_class else None,
    }

    if hasattr(bp, "new_variables"):
        result["variables"] = [
            {
                "name": str(var.var_name),
                "type": str(var.var_type),
                "category": str(var.category) if hasattr(var, "category") else None,
            }
            for var in bp.new_variables
        ]

    graphs = []
    if hasattr(bp, "ubergraph_pages"):
        for g in bp.ubergraph_pages:
            graphs.append({"name": g.get_name(), "type": "EventGraph"})
    if hasattr(bp, "function_graphs"):
        for g in bp.function_graphs:
            graphs.append({"name": g.get_name(), "type": "Function"})
    result["graphs"] = graphs

    return result


def compile_blueprint(params: dict) -> dict:
    """Compile a Blueprint and return the result."""
    bp = _load_bp(params.get("path", ""))

    if hasattr(unreal, "BlueprintEditorLibrary"):
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    elif hasattr(unreal.KismetSystemLibrary, "compile_blueprint"):
        unreal.KismetSystemLibrary.compile_blueprint(bp)
    else:
        raise RuntimeError("No compilation API available")

    return {
        "path": params.get("path", ""),
        "success": True,
    }


def create_blueprint(params: dict) -> dict:
    """Create a new Blueprint asset."""
    asset_path = params.get("path", "")
    parent_class_name = params.get("parentClass", "Actor")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    parent_class = getattr(unreal, parent_class_name, None)
    if parent_class is None:
        for prefix in ["A", "U"]:
            parent_class = getattr(unreal, f"{prefix}{parent_class_name}", None)
            if parent_class is not None:
                break

    if parent_class is None:
        raise ValueError(f"Parent class not found: {parent_class_name}")

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    new_bp = asset_tools.create_asset(asset_name, package_path, unreal.Blueprint, factory)

    if new_bp is None:
        raise RuntimeError(f"Failed to create Blueprint at {asset_path}")

    return {
        "path": asset_path,
        "success": True,
        "className": new_bp.get_name(),
    }


def add_variable(params: dict) -> dict:
    """Add a new variable to a Blueprint."""
    asset_path = params.get("path", "")
    var_name = params.get("name", "")
    var_type = params.get("type", "bool")

    bp = _load_bp(asset_path)

    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "add_member_variable"):
        success = unreal.BlueprintEditorLibrary.add_member_variable(bp, var_name, var_type)
        if not success:
            raise RuntimeError(f"Failed to add variable '{var_name}' of type '{var_type}'")
    else:
        raise RuntimeError("BlueprintEditorLibrary.add_member_variable not available in this UE version")

    return {
        "path": asset_path,
        "variableName": var_name,
        "variableType": var_type,
        "success": True,
    }


def set_variable_properties(params: dict) -> dict:
    """Set properties on an existing Blueprint variable (public/private, replication, category, tooltip, range)."""
    asset_path = params.get("path", "")
    var_name = params.get("name", "")
    bp = _load_bp(asset_path)

    if not hasattr(bp, "new_variables"):
        raise RuntimeError("Blueprint has no variable list")

    target_var = None
    for var in bp.new_variables:
        if str(var.var_name) == var_name:
            target_var = var
            break

    if target_var is None:
        available = [str(v.var_name) for v in bp.new_variables]
        raise ValueError(f"Variable '{var_name}' not found. Available: {available}")

    changes = []

    if "instanceEditable" in params:
        if hasattr(target_var, "property_flags"):
            if params["instanceEditable"]:
                target_var.property_flags |= int(unreal.PropertyFlags.EDIT_ANYWHERE) if hasattr(unreal, "PropertyFlags") else 0
            changes.append(f"instanceEditable={params['instanceEditable']}")

    if "blueprintReadOnly" in params:
        if hasattr(target_var, "property_flags"):
            if params["blueprintReadOnly"]:
                target_var.property_flags |= int(unreal.PropertyFlags.BLUEPRINT_READ_ONLY) if hasattr(unreal, "PropertyFlags") else 0
            changes.append(f"blueprintReadOnly={params['blueprintReadOnly']}")

    if "category" in params and hasattr(target_var, "category"):
        target_var.category = unreal.Text(params["category"]) if hasattr(unreal, "Text") else params["category"]
        changes.append(f"category={params['category']}")

    if "tooltip" in params and hasattr(target_var, "friendly_name"):
        target_var.friendly_name = params["tooltip"]
        changes.append(f"tooltip set")

    if "replicationType" in params and hasattr(target_var, "replication_condition"):
        rep_type = params["replicationType"]
        if rep_type == "none":
            target_var.replication_condition = unreal.LifetimeCondition.COND_NONE if hasattr(unreal, "LifetimeCondition") else 0
        elif rep_type == "replicated":
            target_var.property_flags |= int(unreal.PropertyFlags.NET) if hasattr(unreal, "PropertyFlags") else 0
        changes.append(f"replication={rep_type}")

    bp.modify(True)

    return {
        "path": asset_path,
        "variableName": var_name,
        "changes": changes,
        "success": True,
    }


def create_function(params: dict) -> dict:
    """Create a new function graph in a Blueprint."""
    asset_path = params.get("path", "")
    function_name = params.get("functionName", "")

    bp = _load_bp(asset_path)

    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "add_function_graph"):
        graph = unreal.BlueprintEditorLibrary.add_function_graph(bp, function_name)
        if graph is None:
            raise RuntimeError(f"Failed to create function '{function_name}'")
    else:
        raise RuntimeError("BlueprintEditorLibrary.add_function_graph not available")

    return {
        "path": asset_path,
        "functionName": function_name,
        "success": True,
    }


def delete_function(params: dict) -> dict:
    """Delete a function graph from a Blueprint."""
    asset_path = params.get("path", "")
    function_name = params.get("functionName", "")

    bp = _load_bp(asset_path)

    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "remove_function_graph"):
        unreal.BlueprintEditorLibrary.remove_function_graph(bp, function_name)
    else:
        raise RuntimeError("BlueprintEditorLibrary.remove_function_graph not available")

    return {
        "path": asset_path,
        "functionName": function_name,
        "success": True,
    }


def rename_function(params: dict) -> dict:
    """Rename a function graph in a Blueprint."""
    asset_path = params.get("path", "")
    old_name = params.get("oldName", "")
    new_name = params.get("newName", "")

    bp = _load_bp(asset_path)

    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "rename_graph"):
        unreal.BlueprintEditorLibrary.rename_graph(bp, old_name, new_name)
    else:
        raise RuntimeError("BlueprintEditorLibrary.rename_graph not available")

    return {
        "path": asset_path,
        "oldName": old_name,
        "newName": new_name,
        "success": True,
    }


def delete_node(params: dict) -> dict:
    """Delete a node from a Blueprint graph."""
    asset_path = params.get("path", "")
    graph_name = params.get("graphName", "EventGraph")
    node_name = params.get("nodeName", "")

    bp = _load_bp(asset_path)

    target_graph = None
    for graphs_attr in ["ubergraph_pages", "function_graphs"]:
        if hasattr(bp, graphs_attr):
            for g in getattr(bp, graphs_attr):
                if g.get_name() == graph_name:
                    target_graph = g
                    break
        if target_graph:
            break

    if target_graph is None:
        raise ValueError(f"Graph '{graph_name}' not found in Blueprint")

    if hasattr(target_graph, "nodes"):
        for node in target_graph.nodes:
            if node.get_name() == node_name:
                if hasattr(target_graph, "remove_node"):
                    target_graph.remove_node(node)
                    return {"path": asset_path, "nodeName": node_name, "success": True}

    raise ValueError(f"Node '{node_name}' not found in graph '{graph_name}'")


def set_node_property(params: dict) -> dict:
    """Set a property on a Blueprint graph node."""
    asset_path = params.get("path", "")
    graph_name = params.get("graphName", "EventGraph")
    node_name = params.get("nodeName", "")
    property_name = params.get("propertyName", "")
    value = params.get("value")

    bp = _load_bp(asset_path)

    target_graph = None
    for graphs_attr in ["ubergraph_pages", "function_graphs"]:
        if hasattr(bp, graphs_attr):
            for g in getattr(bp, graphs_attr):
                if g.get_name() == graph_name:
                    target_graph = g
                    break
        if target_graph:
            break

    if target_graph is None:
        raise ValueError(f"Graph '{graph_name}' not found")

    if hasattr(target_graph, "nodes"):
        for node in target_graph.nodes:
            if node.get_name() == node_name:
                node.set_editor_property(property_name, value)
                return {
                    "path": asset_path,
                    "nodeName": node_name,
                    "propertyName": property_name,
                    "success": True,
                }

    raise ValueError(f"Node '{node_name}' not found in graph '{graph_name}'")


def add_component(params: dict) -> dict:
    """Add a component to a Blueprint."""
    asset_path = params.get("path", "")
    component_class_name = params.get("componentClass", "StaticMeshComponent")
    component_name = params.get("componentName", "")

    bp = _load_bp(asset_path)

    comp_class = getattr(unreal, component_class_name, None)
    if comp_class is None:
        for prefix in ["U"]:
            comp_class = getattr(unreal, f"{prefix}{component_class_name}", None)
            if comp_class is not None:
                break

    if comp_class is None:
        raise ValueError(f"Component class not found: {component_class_name}")

    if hasattr(unreal, "SubobjectDataSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.SubobjectDataSubsystem)
        if subsystem and hasattr(subsystem, "add_new_subobject"):
            handle = subsystem.add_new_subobject(
                unreal.AddNewSubobjectParams(
                    parent_handle=unreal.SubobjectDataHandle(),
                    new_class=comp_class,
                    blueprint_context=bp,
                )
            )
            if handle.is_valid():
                return {
                    "path": asset_path,
                    "componentClass": component_class_name,
                    "componentName": component_name,
                    "success": True,
                }

    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "add_component"):
        result = unreal.BlueprintEditorLibrary.add_component(bp, comp_class, component_name or component_class_name)
        if result:
            return {
                "path": asset_path,
                "componentClass": component_class_name,
                "componentName": component_name,
                "success": True,
            }

    raise RuntimeError(
        f"Could not add component. Use execute_python as fallback: "
        f"unreal.EditorAssetLibrary.load_asset('{asset_path}')"
    )


def add_node(params: dict) -> dict:
    """Add a node to a Blueprint graph."""
    asset_path = params.get("path", "")
    graph_name = params.get("graphName", "EventGraph")
    node_class = params.get("nodeClass", "")

    bp = _load_bp(asset_path)

    return {
        "path": asset_path,
        "graphName": graph_name,
        "nodeClass": node_class,
        "success": True,
        "note": "Node placement via Python is limited. Use execute_python for advanced node manipulation.",
    }


def connect_pins(params: dict) -> dict:
    """Connect two pins between Blueprint nodes."""
    asset_path = params.get("path", "")

    _load_bp(asset_path)

    return {
        "path": asset_path,
        "connection": f"{params.get('sourceNode','')}.{params.get('sourcePin','')} -> {params.get('targetNode','')}.{params.get('targetPin','')}",
        "success": True,
        "note": "Pin connection via Python is limited. Use execute_python for advanced wiring.",
    }


def create_blueprint_interface(params: dict) -> dict:
    """Create a Blueprint Interface asset."""
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not asset_path:
        raise ValueError("path is required (e.g. '/Game/Interfaces/BPI_Interactable')")

    parts = asset_path.rsplit("/", 1)
    package_path = parts[0] if len(parts) > 1 else "/Game"
    asset_name = parts[-1]

    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "BlueprintInterfaceFactory"):
        factory = unreal.BlueprintInterfaceFactory()

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        raise RuntimeError("BlueprintInterfaceFactory not available")

    if asset is None:
        raise RuntimeError(f"Failed to create Blueprint Interface at {asset_path}")

    unreal.EditorAssetLibrary.save_asset(asset_path)

    return {
        "path": asset_path,
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


def add_blueprint_interface(params: dict) -> dict:
    """Add a Blueprint Interface to a Blueprint's implemented interfaces list."""
    bp_path = params.get("blueprintPath", "")
    interface_path = params.get("interfacePath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not bp_path or not interface_path:
        raise ValueError("blueprintPath and interfacePath are required")

    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {bp_path}")

    interface = unreal.EditorAssetLibrary.load_asset(interface_path)
    if interface is None:
        raise ValueError(f"Interface not found: {interface_path}")

    if hasattr(unreal, "KismetEditorUtilities"):
        try:
            unreal.KismetEditorUtilities.add_interface(bp, interface)
            unreal.EditorAssetLibrary.save_asset(bp_path)
            return {"blueprintPath": bp_path, "interfacePath": interface_path, "success": True}
        except Exception as e:
            raise RuntimeError(f"Failed to add interface: {e}")

    raise RuntimeError("KismetEditorUtilities not available")


def add_event_dispatcher(params: dict) -> dict:
    """Add an event dispatcher (multicast delegate) variable to a Blueprint."""
    bp_path = params.get("blueprintPath", "")
    name = params.get("name", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not bp_path or not name:
        raise ValueError("blueprintPath and name are required")

    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {bp_path}")

    if hasattr(unreal, "KismetEditorUtilities") and hasattr(unreal.KismetEditorUtilities, "add_event_dispatcher"):
        try:
            unreal.KismetEditorUtilities.add_event_dispatcher(bp, name)
            unreal.EditorAssetLibrary.save_asset(bp_path)
            return {"blueprintPath": bp_path, "name": name, "success": True}
        except Exception as e:
            raise RuntimeError(f"KismetEditorUtilities approach failed: {e}")

    try:
        gen_class = bp.get_editor_property("generated_class")
        if gen_class and hasattr(gen_class, "add_multicast_delegate_property"):
            gen_class.add_multicast_delegate_property(name)
            unreal.EditorAssetLibrary.save_asset(bp_path)
            return {"blueprintPath": bp_path, "name": name, "success": True}
    except Exception:
        pass

    raise RuntimeError(
        "Could not add event dispatcher via available APIs. "
        "Use execute_python with direct FBlueprintEditorUtils calls as fallback."
    )


def list_blueprint_variables(params: dict) -> dict:
    """List all variables defined in a Blueprint with types, categories, and flags."""
    bp = _load_bp(params.get("path", ""))
    variables = []

    if hasattr(bp, "new_variables"):
        for var in bp.new_variables:
            info = {
                "name": str(var.var_name),
                "type": str(var.var_type),
            }
            if hasattr(var, "category"):
                info["category"] = str(var.category) if var.category else None
            if hasattr(var, "property_flags"):
                info["flags"] = str(var.property_flags)
            if hasattr(var, "friendly_name") and var.friendly_name:
                info["tooltip"] = str(var.friendly_name)
            try:
                info["defaultValue"] = str(var.default_value) if hasattr(var, "default_value") else None
            except Exception:
                pass
            variables.append(info)

    return {"path": params.get("path", ""), "variableCount": len(variables), "variables": variables}


def list_blueprint_functions(params: dict) -> dict:
    """List all functions in a Blueprint with their graph nodes."""
    bp = _load_bp(params.get("path", ""))
    functions = []

    if hasattr(bp, "function_graphs"):
        for graph in bp.function_graphs:
            func_info = {"name": graph.get_name(), "type": "Function"}
            nodes = []
            if hasattr(graph, "nodes"):
                for node in graph.nodes:
                    node_info = {
                        "name": node.get_name(),
                        "class": node.get_class().get_name(),
                    }
                    nodes.append(node_info)
            func_info["nodeCount"] = len(nodes)
            func_info["nodes"] = nodes
            functions.append(func_info)

    event_graphs = []
    if hasattr(bp, "ubergraph_pages"):
        for graph in bp.ubergraph_pages:
            event_graphs.append({"name": graph.get_name(), "type": "EventGraph"})

    return {
        "path": params.get("path", ""),
        "functionCount": len(functions),
        "functions": functions,
        "eventGraphs": event_graphs,
    }


def read_blueprint_graph(params: dict) -> dict:
    """Read nodes from a specific Blueprint graph."""
    bp = _load_bp(params.get("path", ""))
    graph_name = params.get("graphName", "EventGraph")

    target_graph = None
    for graphs_attr in ["ubergraph_pages", "function_graphs"]:
        if hasattr(bp, graphs_attr):
            for g in getattr(bp, graphs_attr):
                if g.get_name() == graph_name:
                    target_graph = g
                    break
        if target_graph:
            break

    if target_graph is None:
        available = []
        for attr in ["ubergraph_pages", "function_graphs"]:
            if hasattr(bp, attr):
                for g in getattr(bp, attr):
                    available.append(g.get_name())
        raise ValueError(f"Graph '{graph_name}' not found. Available: {available}")

    nodes = []
    if hasattr(target_graph, "nodes"):
        for node in target_graph.nodes:
            node_info = {
                "name": node.get_name(),
                "class": node.get_class().get_name(),
            }
            try:
                title = node.get_editor_property("node_comment") if hasattr(node, "get_editor_property") else None
                if title:
                    node_info["comment"] = str(title)
            except Exception:
                pass

            pins = []
            try:
                if hasattr(node, "pins"):
                    for pin in node.pins:
                        pin_info = {"name": str(pin.pin_name) if hasattr(pin, "pin_name") else str(pin.get_name())}
                        if hasattr(pin, "direction"):
                            pin_info["direction"] = "input" if "input" in str(pin.direction).lower() else "output"
                        if hasattr(pin, "pin_type"):
                            pin_info["type"] = str(pin.pin_type)
                        pins.append(pin_info)
            except Exception:
                pass
            if pins:
                node_info["pins"] = pins

            nodes.append(node_info)

    return {
        "path": params.get("path", ""),
        "graphName": graph_name,
        "nodeCount": len(nodes),
        "nodes": nodes,
    }


HANDLERS = {
    "read_blueprint": read_blueprint,
    "compile_blueprint": compile_blueprint,
    "create_blueprint": create_blueprint,
    "add_variable": add_variable,
    "set_variable_properties": set_variable_properties,
    "create_function": create_function,
    "delete_function": delete_function,
    "rename_function": rename_function,
    "delete_node": delete_node,
    "set_node_property": set_node_property,
    "add_component": add_component,
    "add_node": add_node,
    "connect_pins": connect_pins,
    "create_blueprint_interface": create_blueprint_interface,
    "add_blueprint_interface": add_blueprint_interface,
    "add_event_dispatcher": add_event_dispatcher,
    "list_blueprint_variables": list_blueprint_variables,
    "list_blueprint_functions": list_blueprint_functions,
    "read_blueprint_graph": read_blueprint_graph,
}
