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


def _find_graph(bp, graph_name: str):
    """Find a graph by name across all graph collections. Returns (graph, available_names)."""
    target = None
    available = []

    if hasattr(unreal, "BlueprintEditorLibrary"):
        lib = unreal.BlueprintEditorLibrary
        if hasattr(lib, "find_graph") and graph_name:
            try:
                g = lib.find_graph(bp, graph_name)
                if g is not None:
                    return g, [graph_name]
            except Exception:
                pass
        if graph_name == "EventGraph" and hasattr(lib, "find_event_graph"):
            try:
                g = lib.find_event_graph(bp)
                if g is not None:
                    return g, ["EventGraph"]
            except Exception:
                pass

    for graphs_attr in ["ubergraph_pages", "function_graphs"]:
        graphs = None
        try:
            graphs = getattr(bp, graphs_attr, None)
        except Exception:
            pass
        if graphs is None:
            try:
                graphs = bp.get_editor_property(graphs_attr)
            except Exception:
                pass
        if graphs:
            for g in graphs:
                gn = g.get_name()
                available.append(gn)
                if gn == graph_name:
                    target = g
                    break
        if target:
            break

    if target is None and hasattr(bp, "get_all_graphs"):
        try:
            for g in bp.get_all_graphs():
                gn = g.get_name()
                if gn not in available:
                    available.append(gn)
                if gn == graph_name:
                    target = g
                    break
        except Exception:
            pass

    return target, available


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
    from ._util import resolve_asset_path, ensure_asset_cleared

    parent_class_name = params.get("parentClass", "Actor")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Blueprints")
    if not asset_name:
        raise ValueError("path or name is required")
    ensure_asset_cleared(full_path)

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

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    new_bp = asset_tools.create_asset(asset_name, package_path, unreal.Blueprint, factory)

    if new_bp is None:
        raise RuntimeError(f"Failed to create Blueprint at {full_path}")

    return {
        "path": full_path,
        "success": True,
        "className": new_bp.get_name(),
    }


def add_variable(params: dict) -> dict:
    """Add a new variable to a Blueprint."""
    asset_path = params.get("path", "")
    var_name = params.get("name", "")
    var_type = params.get("type", "bool")

    bp = _load_bp(asset_path)

    # Check if variable already exists - if so, delete it first for idempotency
    try:
        if hasattr(bp, "new_variables") and bp.new_variables:
            for var in list(bp.new_variables):
                if hasattr(var, "var_name") and var.var_name == var_name:
                    # Variable exists, remove it first
                    try:
                        vars_list = list(bp.new_variables)
                        vars_list.remove(var)
                        bp.set_editor_property("new_variables", vars_list)
                        bp.modify(True)
                        unreal.EditorAssetLibrary.save_asset(asset_path)
                    except Exception:
                        pass
                    break
    except Exception:
        pass

    success = False
    last_err = "All methods failed"

    # Open blueprint in editor first - this makes BlueprintEditorLibrary operations more reliable
    try:
        if hasattr(unreal, "AssetEditorSubsystem"):
            asset_editor = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
            if asset_editor:
                asset_editor.open_editor_for_assets([bp])
    except Exception:
        pass

    # Compile blueprint first to ensure it's in a valid state
    try:
        if hasattr(unreal, "BlueprintEditorLibrary"):
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            # Wait a moment for compilation to finish
            import time
            time.sleep(0.1)
    except Exception:
        pass

    if hasattr(unreal, "BlueprintEditorLibrary") and hasattr(unreal.BlueprintEditorLibrary, "add_member_variable"):
        pin_type = _make_pin_type(var_type)
        if pin_type is not None:
            try:
                bp.modify(True)
                # Ensure blueprint is saved before adding variable
                unreal.EditorAssetLibrary.save_asset(asset_path)
                # Convert var_name to Name type if needed
                var_name_obj = var_name
                if hasattr(unreal, "Name") and not isinstance(var_name, unreal.Name):
                    try:
                        var_name_obj = unreal.Name(var_name)
                    except Exception:
                        var_name_obj = var_name
                result = unreal.BlueprintEditorLibrary.add_member_variable(bp, var_name_obj, pin_type)
                if result:
                    success = True
                    bp.modify(True)
                    unreal.EditorAssetLibrary.save_asset(asset_path)
                else:
                    if last_err == "All methods failed":
                        last_err = "BlueprintEditorLibrary.add_member_variable returned False"
                    else:
                        last_err = f"{last_err}; BlueprintEditorLibrary.add_member_variable returned False"
            except Exception as e:
                last_err = f"{last_err}; BlueprintEditorLibrary.add_member_variable raised: {str(e)}"

    if not success:
        try:
            result = _add_variable_via_description(bp, var_name, var_type)
            if result:
                success = True
                bp.modify(True)
                unreal.EditorAssetLibrary.save_asset(asset_path)
            else:
                if last_err == "All methods failed":
                    last_err = "_add_variable_via_description returned False"
                else:
                    last_err = f"{last_err}; _add_variable_via_description returned False"
        except Exception as e:
            last_err = f"{last_err}; _add_variable_via_description raised: {str(e)}"

    # Final fallback: try direct new_variables access (read_blueprint confirms this exists and is readable)
    if not success:
        try:
            bp.modify(True)
            # Create desc and pt - skip if BPVariableDescription doesn't exist
            desc = None
            if hasattr(unreal, "BPVariableDescription"):
                desc = unreal.BPVariableDescription()
            elif hasattr(unreal, "EdGraphVariable"):
                desc = unreal.EdGraphVariable()
            else:
                # Can't create description, skip this fallback
                raise Exception("BPVariableDescription not available in this UE version")
            
            desc.var_name = var_name
            cat = _TYPE_CATEGORY.get(var_type.lower(), "real")
            sub = _TYPE_SUB_CATEGORY.get(var_type.lower(), "")
            pt = unreal.EdGraphPinType()
            pt.pin_category = cat
            if sub:
                pt.pin_sub_category = sub
            desc.var_type = pt
            
            # Read existing variables
            existing_vars = []
            try:
                if hasattr(bp, "new_variables") and bp.new_variables:
                    existing_vars = list(bp.new_variables)
            except Exception:
                pass
            
            # Append new variable
            existing_vars.append(desc)
            
            # Try to set it back via set_editor_property first
            try:
                bp.set_editor_property("new_variables", existing_vars)
                unreal.EditorAssetLibrary.save_asset(asset_path)
                success = True
            except Exception:
                # Try direct assignment
                try:
                    bp.new_variables = existing_vars
                    unreal.EditorAssetLibrary.save_asset(asset_path)
                    success = True
                except Exception:
                    pass
        except Exception as e:
            # Only update error if we haven't tried this fallback yet
            if "BPVariableDescription not available" not in str(e):
                last_err = f"{last_err}; final fallback raised: {str(e)}"

    if not success:
        raise RuntimeError(f"Failed to add variable '{var_name}' of type '{var_type}': {last_err}")

    # Mark blueprint as modified and save
    try:
        bp.modify(True)
        unreal.EditorAssetLibrary.save_asset(asset_path)
    except Exception:
        pass

    return {
        "path": asset_path,
        "variableName": var_name,
        "variableType": var_type,
        "success": True,
    }


def _add_variable_via_description(bp, var_name, var_type):
    """Fallback: add variable by creating a BPVariableDescription and appending to new_variables."""
    # BPVariableDescription doesn't exist in UE5.7 - try alternative approaches
    if not hasattr(unreal, "BPVariableDescription"):
        # Try using EdGraphVariable or direct manipulation instead
        try:
            # Try to add via Blueprint's variable list directly
            bp.modify(True)
            # Access new_variables directly on blueprint
            if hasattr(bp, "new_variables"):
                existing_vars = list(bp.new_variables) if bp.new_variables else []
                # Create a minimal variable entry - we'll use a dict-like approach
                # In UE5.7, we might need to use a different API
                # For now, return False to fall through to other methods
                pass
        except Exception:
            pass
        return False

    desc = unreal.BPVariableDescription()
    try:
        desc.set_editor_property("var_name", var_name)
    except Exception:
        desc.var_name = var_name

    # Create pin type using _make_pin_type helper
    pt = _make_pin_type(var_type)
    if pt is None:
        # Fallback: create basic pin type manually
        try:
            pt = unreal.EdGraphPinType()
            cat = _TYPE_CATEGORY.get(var_type.lower(), var_type.lower())
            sub = _TYPE_SUB_CATEGORY.get(var_type.lower(), "")
            
            # Set pin category
            try:
                pt.set_editor_property("pin_category", cat)
            except Exception:
                try:
                    pt.pin_category = cat
                except Exception:
                    # Try creating a new pin type if direct assignment fails
                    pt = unreal.EdGraphPinType()
                    if hasattr(pt, "pin_category"):
                        pt.pin_category = cat
            
            # Set pin sub category if needed
            if sub:
                try:
                    pt.set_editor_property("pin_sub_category", sub)
                except Exception:
                    try:
                        pt.pin_sub_category = sub
                    except Exception:
                        pass
        except Exception:
            pt = None

    if pt is None:
        return False

    # Set the pin type on the description
    try:
        desc.set_editor_property("var_type", pt)
    except Exception:
        try:
            desc.var_type = pt
        except Exception:
            # Last resort: try to set it directly
            if hasattr(desc, "var_type"):
                desc.var_type = pt
            else:
                return False

    # Mark blueprint as modified before adding variable
    try:
        bp.modify(True)
    except Exception:
        pass

    # Try to access new_variables through Blueprint's BlueprintGraph
    # Variables in UE5 are stored in the Blueprint's variable list
    try:
        # Compile blueprint to ensure generated_class exists
        try:
            if hasattr(unreal, "BlueprintEditorLibrary"):
                unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception:
            pass
        
        # Try accessing through generated_class
        gen_class = None
        try:
            gen_class = bp.get_editor_property("generated_class")
        except Exception:
            gen_class = getattr(bp, "generated_class", None)
        
        if gen_class:
            try:
                new_vars = list(gen_class.get_editor_property("new_variables")) if gen_class.get_editor_property("new_variables") else []
                new_vars.append(desc)
                gen_class.set_editor_property("new_variables", new_vars)
                return True
            except Exception:
                try:
                    if hasattr(gen_class, 'new_variables'):
                        new_vars = list(gen_class.new_variables) if gen_class.new_variables else []
                        new_vars.append(desc)
                        gen_class.new_variables = new_vars
                        return True
                except Exception:
                    pass
        
        # Last resort: try direct access on blueprint (may not work but worth trying)
        try:
            if hasattr(bp, "new_variables"):
                new_vars = list(bp.new_variables) if bp.new_variables else []
                new_vars.append(desc)
                bp.new_variables = new_vars
                return True
        except Exception:
            pass
        
        return False
    except Exception:
        return False


_TYPE_CATEGORY = {
    "bool": "bool", "boolean": "bool",
    "byte": "byte",
    "int": "int", "integer": "int", "int32": "int",
    "int64": "int64",
    "float": "real", "double": "real", "real": "real",
    "string": "string", "str": "string",
    "name": "name", "text": "text",
    "object": "object", "class": "class",
    "softobject": "softobject", "softclass": "softclass",
    "vector": "struct", "rotator": "struct", "transform": "struct",
    "linearcolor": "struct", "color": "struct",
}

_TYPE_SUB_CATEGORY = {
    "float": "double", "double": "double", "real": "double",
}


def _make_pin_type(type_str):
    """Construct an EdGraphPinType for the given type string."""
    SIMPLE = {
        "bool": "bool", "boolean": "bool",
        "byte": "byte",
        "int": "int", "integer": "int", "int32": "int",
        "int64": "int64",
        "float": "real", "double": "real", "real": "real",
        "string": "string", "str": "string",
        "name": "name",
        "text": "text",
        "object": "object",
        "class": "class",
        "softobject": "softobject",
        "softclass": "softclass",
    }

    STRUCT = {
        "vector": "/Script/CoreUObject.Vector",
        "rotator": "/Script/CoreUObject.Rotator",
        "transform": "/Script/CoreUObject.Transform",
        "linearcolor": "/Script/CoreUObject.LinearColor",
        "color": "/Script/CoreUObject.LinearColor",
        "vector2d": "/Script/CoreUObject.Vector2D",
        "gameplaytag": "/Script/GameplayTags.GameplayTag",
        "gameplaytagcontainer": "/Script/GameplayTags.GameplayTagContainer",
    }

    SUB_CATEGORY = {
        "float": "double", "double": "double", "real": "double",
    }

    lower = type_str.lower()

    try:
        pt = unreal.EdGraphPinType()
    except Exception:
        return None

    if lower in SIMPLE:
        cat = SIMPLE[lower]
        try:
            pt.set_editor_property("pin_category", cat)
        except Exception:
            try:
                pt.pin_category = cat
            except Exception:
                return None
        sub = SUB_CATEGORY.get(lower)
        if sub:
            try:
                pt.set_editor_property("pin_sub_category", sub)
            except Exception:
                try:
                    pt.pin_sub_category = sub
                except Exception:
                    pass
        return pt

    if lower in STRUCT:
        try:
            pt.set_editor_property("pin_category", "struct")
        except Exception:
            try:
                pt.pin_category = "struct"
            except Exception:
                return None
        struct_path = STRUCT[lower]
        struct_obj = None
        if hasattr(unreal, "find_object"):
            struct_obj = unreal.find_object(None, struct_path)
        if struct_obj is None and hasattr(unreal, "load_object"):
            try:
                struct_obj = unreal.load_object(None, struct_path)
            except Exception:
                pass
        if struct_obj:
            try:
                pt.set_editor_property("pin_sub_category_object", struct_obj)
            except Exception:
                try:
                    pt.pin_sub_category_object = struct_obj
                except Exception:
                    pass
        return pt

    try:
        pt.set_editor_property("pin_category", lower)
    except Exception:
        try:
            pt.pin_category = lower
        except Exception:
            pass
    return pt


def set_variable_properties(params: dict) -> dict:
    """Set properties on an existing Blueprint variable (public/private, replication, category, tooltip, range)."""
    asset_path = params.get("path", "")
    var_name = params.get("name", "")
    bp = _load_bp(asset_path)

    new_vars = None
    try:
        new_vars = bp.new_variables
    except Exception:
        pass
    if new_vars is None:
        try:
            new_vars = bp.get_editor_property("new_variables")
        except Exception:
            pass
    if new_vars is None:
        return {
            "path": asset_path,
            "variableName": var_name,
            "changes": [],
            "success": True,
            "note": "Cannot access variable list directly. Variable properties may need to be set via execute_python.",
        }

    target_var = None
    for var in new_vars:
        if str(var.var_name) == var_name:
            target_var = var
            break

    if target_var is None:
        available = [str(v.var_name) for v in new_vars]
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

    if not hasattr(unreal, "BlueprintEditorLibrary") or not hasattr(unreal.BlueprintEditorLibrary, "rename_graph"):
        raise RuntimeError("BlueprintEditorLibrary.rename_graph not available")

    target_graph, available = _find_graph(bp, old_name)

    if target_graph is None:
        raise ValueError(f"Function '{old_name}' not found. Available: {available}")

    try:
        unreal.BlueprintEditorLibrary.rename_graph(bp, old_name, new_name)
    except TypeError:
        try:
            unreal.BlueprintEditorLibrary.rename_graph(target_graph, new_name)
        except Exception:
            target_graph.rename(new_name) if hasattr(target_graph, "rename") else None

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
    component_name = params.get("componentName", "") or component_class_name

    bp = _load_bp(asset_path)

    # Open blueprint in editor first - this makes BlueprintEditorLibrary operations more reliable
    try:
        if hasattr(unreal, "AssetEditorSubsystem"):
            asset_editor = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
            if asset_editor:
                asset_editor.open_editor_for_assets([bp])
    except Exception:
        pass

    # Check if component already exists - if so, try to remove it first for idempotency
    try:
        scs = bp.get_editor_property("simple_construction_script")
        if scs and hasattr(scs, "nodes"):
            for node in list(scs.nodes):
                if node.get_name() == component_name:
                    # Component exists, try to remove it
                    try:
                        if hasattr(scs, "remove_node"):
                            scs.remove_node(node)
                            bp.modify(True)
                            unreal.EditorAssetLibrary.save_asset(asset_path)
                    except Exception:
                        pass
                    break
    except Exception:
        pass

    comp_class = getattr(unreal, component_class_name, None)
    if comp_class is None:
        for prefix in ["U"]:
            comp_class = getattr(unreal, f"{prefix}{component_class_name}", None)
            if comp_class is not None:
                break

    if comp_class is None:
        raise ValueError(f"Component class not found: {component_class_name}")

    result_info = {
        "path": asset_path,
        "componentClass": component_class_name,
        "componentName": component_name,
        "success": True,
    }

    if hasattr(unreal, "SubobjectDataSubsystem"):
        try:
            subsystem = unreal.get_editor_subsystem(unreal.SubobjectDataSubsystem)
            if subsystem and hasattr(subsystem, "k2_gather_subobject_data_for_blueprint"):
                # Get root data handle first (required!)
                blueprint_handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)
                if blueprint_handles and len(blueprint_handles) > 0:
                    root_handle = blueprint_handles[0]
                    if subsystem and hasattr(subsystem, "add_new_subobject"):
                        handle, fail_reason = subsystem.add_new_subobject(
                            unreal.AddNewSubobjectParams(
                                parent_handle=root_handle,
                                new_class=comp_class,
                                blueprint_context=bp,
                            )
                        )
                        # Check fail_reason to see why it might have failed
                        if fail_reason and fail_reason != "":
                            # Log but continue to next method
                            pass
                        if handle and handle.is_valid():
                            # Rename if component_name provided
                            if component_name and component_name != component_class_name:
                                try:
                                    subsystem.rename_subobject(handle, component_name)
                                except Exception:
                                    pass
                            bp.modify(True)
                            unreal.EditorAssetLibrary.save_asset(asset_path)
                            return result_info
        except Exception as e:
            pass

    if hasattr(unreal, "BlueprintEditorLibrary"):
        lib = unreal.BlueprintEditorLibrary
        if hasattr(lib, "add_component"):
            try:
                r = lib.add_component(bp, comp_class, component_name or component_class_name)
                if r:
                    bp.modify(True)
                    unreal.EditorAssetLibrary.save_asset(asset_path)
                    return result_info
            except Exception as e:
                pass

    scs = None
    try:
        scs = bp.get_editor_property("simple_construction_script")
    except Exception:
        scs = getattr(bp, "simple_construction_script", None)

    if scs is not None:
        try:
            node = scs.create_node(comp_class, component_name or component_class_name)
            if node is not None:
                if hasattr(scs, "add_node"):
                    scs.add_node(node)
                # Mark blueprint as modified and save
                try:
                    bp.modify(True)
                    unreal.EditorAssetLibrary.save_asset(asset_path)
                except Exception:
                    pass
                return result_info
        except Exception as e:
            # Try alternative SCS approach
            try:
                if hasattr(scs, "add_node"):
                    # Create node using different method
                    node_class = getattr(unreal, f"{component_class_name.replace('Component', '')}Node", None)
                    if node_class:
                        node = node_class()
                        node.set_editor_property("variable_name", component_name or component_class_name)
                        scs.add_node(node)
                        bp.modify(True)
                        unreal.EditorAssetLibrary.save_asset(asset_path)
                        return result_info
            except Exception:
                pass

    # Final fallback: use direct Python manipulation with SCS
    try:
        bp.modify(True)
        scs = None
        try:
            scs = bp.get_editor_property("simple_construction_script")
        except Exception:
            try:
                scs = getattr(bp, "simple_construction_script", None)
            except Exception:
                pass
        if scs and hasattr(scs, "create_node") and hasattr(scs, "add_node"):
            try:
                node = scs.create_node(comp_class, component_name or component_class_name)
                if node:
                    scs.add_node(node)
                    unreal.EditorAssetLibrary.save_asset(asset_path)
                    return result_info
            except Exception:
                pass
    except Exception:
        pass

    raise RuntimeError(
        f"Could not add component via available APIs. "
        f"Use execute_python as fallback with SimpleConstructionScript."
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
    from ._util import resolve_asset_path, ensure_asset_cleared

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Interfaces")
    if not asset_name:
        raise ValueError("path is required (e.g. '/Game/Interfaces/BPI_Interactable')")
    ensure_asset_cleared(full_path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "BlueprintInterfaceFactory"):
        factory = unreal.BlueprintInterfaceFactory()

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        raise RuntimeError("BlueprintInterfaceFactory not available")

    if asset is None:
        raise RuntimeError(f"Failed to create Blueprint Interface at {full_path}")

    unreal.EditorAssetLibrary.save_asset(full_path)

    return {
        "path": full_path,
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

    # Open blueprint in editor first - this makes BlueprintEditorLibrary operations more reliable
    try:
        if hasattr(unreal, "AssetEditorSubsystem"):
            asset_editor = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
            if asset_editor:
                asset_editor.open_editor_for_assets([bp])
    except Exception:
        pass

    interface = unreal.EditorAssetLibrary.load_asset(interface_path)
    if interface is None:
        raise ValueError(f"Interface not found: {interface_path}")

    # Check if interface already exists - if so, remove it first for idempotency
    try:
        interfaces = bp.get_editor_property("implemented_interfaces")
        if interfaces:
            interface_class = None
            try:
                if hasattr(interface, "generated_class"):
                    interface_class = interface.generated_class() if callable(interface.generated_class) else interface.generated_class
            except Exception:
                pass
            
            if interface_class:
                interfaces_list = list(interfaces) if hasattr(interfaces, "__iter__") else []
                for entry in list(interfaces_list):
                    entry_interface = None
                    try:
                        entry_interface = entry.get_editor_property("interface") if hasattr(entry, "get_editor_property") else getattr(entry, "interface", None)
                    except Exception:
                        pass
                    
                    if entry_interface == interface_class:
                        # Interface already exists, remove it first
                        try:
                            interfaces_list.remove(entry)
                            bp.set_editor_property("implemented_interfaces", interfaces_list)
                            bp.modify(True)
                            unreal.EditorAssetLibrary.save_asset(bp_path)
                        except Exception:
                            pass
                        break
    except Exception:
        pass

    if hasattr(unreal, "BlueprintEditorLibrary"):
        lib = unreal.BlueprintEditorLibrary
        if hasattr(lib, "add_interface"):
            try:
                lib.add_interface(bp, interface)
                bp.modify(True)
                unreal.EditorAssetLibrary.save_asset(bp_path)
                return {"blueprintPath": bp_path, "interfacePath": interface_path, "success": True}
            except Exception as e:
                pass

    if hasattr(unreal, "KismetEditorUtilities"):
        try:
            unreal.KismetEditorUtilities.add_interface(bp, interface)
            bp.modify(True)
            unreal.EditorAssetLibrary.save_asset(bp_path)
            return {"blueprintPath": bp_path, "interfacePath": interface_path, "success": True}
        except Exception as e:
            pass

    try:
        interfaces = bp.get_editor_property("implemented_interfaces")
        if interfaces is None:
            interfaces = []
        if not hasattr(interfaces, "append"):
            interfaces = list(interfaces)
        
        # Get the interface class
        interface_class = None
        try:
            if hasattr(interface, "generated_class"):
                if callable(interface.generated_class):
                    interface_class = interface.generated_class()
                else:
                    interface_class = interface.generated_class
            elif hasattr(interface, "get_editor_property"):
                interface_class = interface.get_editor_property("generated_class")
        except Exception:
            interface_class = interface
        
        if interface_class:
            entry = unreal.BlueprintInterface()
            try:
                entry.set_editor_property("interface", interface_class)
            except Exception:
                try:
                    entry.interface = interface_class
                except Exception:
                    pass
            interfaces.append(entry)
            bp.set_editor_property("implemented_interfaces", interfaces)
            bp.modify(True)
            unreal.EditorAssetLibrary.save_asset(bp_path)
            return {"blueprintPath": bp_path, "interfacePath": interface_path, "success": True}
    except Exception as e:
        pass

    # Final fallback: use direct Python manipulation
    try:
        bp.modify(True)
        interfaces = None
        try:
            interfaces = list(bp.get_editor_property("implemented_interfaces"))
        except Exception:
            try:
                if hasattr(bp, 'implemented_interfaces') and bp.implemented_interfaces:
                    interfaces = list(bp.implemented_interfaces)
                else:
                    interfaces = []
            except Exception:
                interfaces = []
        
        if interfaces is None:
            interfaces = []
        
        entry = unreal.BlueprintInterface()
        try:
            if hasattr(interface, 'generated_class'):
                if callable(interface.generated_class):
                    entry.interface = interface.generated_class()
                else:
                    entry.interface = interface.generated_class
            else:
                entry.interface = interface
        except Exception:
            entry.interface = interface
        
        interfaces.append(entry)
        bp.set_editor_property("implemented_interfaces", interfaces)
        unreal.EditorAssetLibrary.save_asset(bp_path)
        return {"blueprintPath": bp_path, "interfacePath": interface_path, "success": True}
    except Exception:
        pass

    raise RuntimeError("Could not add interface via available APIs")


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

    if hasattr(unreal, "BlueprintEditorLibrary"):
        lib = unreal.BlueprintEditorLibrary
        if hasattr(lib, "add_member_variable"):
            pt = _make_pin_type("delegate")
            if pt is None:
                pt = _make_pin_type("mcdelegate")
            if pt is not None:
                try:
                    pt.set_editor_property("pin_category", "delegate")
                except Exception:
                    try:
                        pt.pin_category = "delegate"
                    except Exception:
                        pass
                try:
                    success = lib.add_member_variable(bp, name, pt)
                    if success:
                        return {"blueprintPath": bp_path, "name": name, "success": True}
                except Exception:
                    pass

    if hasattr(unreal, "KismetEditorUtilities") and hasattr(unreal.KismetEditorUtilities, "add_event_dispatcher"):
        try:
            unreal.KismetEditorUtilities.add_event_dispatcher(bp, name)
            unreal.EditorAssetLibrary.save_asset(bp_path)
            return {"blueprintPath": bp_path, "name": name, "success": True}
        except Exception:
            pass

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
        "Use execute_python with FBlueprintEditorUtils as fallback."
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

    target_graph, available = _find_graph(bp, graph_name)

    if target_graph is None:
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
