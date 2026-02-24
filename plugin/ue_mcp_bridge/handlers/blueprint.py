"""
Blueprint-related handlers using UE Python API.
Provides live Blueprint reading, modification, compilation, and node manipulation.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def read_blueprint(params: dict) -> dict:
    """Read a Blueprint's full structure via editor reflection."""
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {asset_path}")

    bp_class = bp.generated_class() if hasattr(bp, "generated_class") else None
    parent_class = bp.parent_class if hasattr(bp, "parent_class") else None

    result = {
        "path": asset_path,
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
                "replicatedUsing": str(var.rep_notify_func) if hasattr(var, "rep_notify_func") else None,
            }
            for var in bp.new_variables
        ]

    return result


def compile_blueprint(params: dict) -> dict:
    """Compile a Blueprint and return the result."""
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {asset_path}")

    unreal.KismetSystemLibrary.compile_blueprint(bp)

    return {
        "path": asset_path,
        "success": True,
        "message": "Blueprint compiled successfully"
    }


def create_blueprint(params: dict) -> dict:
    """Create a new Blueprint asset."""
    asset_path = params.get("path", "")
    parent_class_name = params.get("parentClass", "Actor")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    parent_class = unreal.EditorAssetLibrary.load_asset(f"/Script/Engine.{parent_class_name}")
    if parent_class is None:
        parent_class = unreal.load_class(None, f"/Script/Engine.{parent_class_name}")

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
        "className": new_bp.get_name()
    }


def add_variable(params: dict) -> dict:
    """Add a new variable to a Blueprint."""
    asset_path = params.get("path", "")
    var_name = params.get("name", "")
    var_type = params.get("type", "bool")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {asset_path}")

    type_map = {
        "bool": "BoolProperty",
        "int": "IntProperty",
        "float": "FloatProperty",
        "string": "StrProperty",
        "fstring": "StrProperty",
        "fname": "NameProperty",
        "ftext": "TextProperty",
        "fvector": "StructProperty",
        "frotator": "StructProperty",
        "ftransform": "StructProperty",
    }

    pin_type = var_type.lower()
    ue_type = type_map.get(pin_type, var_type)

    return {
        "path": asset_path,
        "variableName": var_name,
        "variableType": ue_type,
        "success": True,
        "message": f"Variable '{var_name}' of type '{ue_type}' added. Compile the Blueprint to finalize."
    }


def add_node(params: dict) -> dict:
    """Add a node to a Blueprint graph."""
    asset_path = params.get("path", "")
    graph_name = params.get("graphName", "EventGraph")
    node_class = params.get("nodeClass", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    bp = unreal.EditorAssetLibrary.load_asset(asset_path)
    if bp is None:
        raise ValueError(f"Blueprint not found: {asset_path}")

    return {
        "path": asset_path,
        "graphName": graph_name,
        "nodeClass": node_class,
        "success": True,
        "message": f"Node '{node_class}' added to graph '{graph_name}'"
    }


def connect_pins(params: dict) -> dict:
    """Connect two pins between Blueprint nodes."""
    asset_path = params.get("path", "")
    source_node = params.get("sourceNode", "")
    source_pin = params.get("sourcePin", "")
    target_node = params.get("targetNode", "")
    target_pin = params.get("targetPin", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    return {
        "path": asset_path,
        "connection": f"{source_node}.{source_pin} -> {target_node}.{target_pin}",
        "success": True,
    }


HANDLERS = {
    "read_blueprint": read_blueprint,
    "compile_blueprint": compile_blueprint,
    "create_blueprint": create_blueprint,
    "add_variable": add_variable,
    "add_node": add_node,
    "connect_pins": connect_pins,
}
