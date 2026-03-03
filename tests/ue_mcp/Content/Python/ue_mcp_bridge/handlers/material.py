"""
Material handlers — read and modify material parameters, inspect material graphs.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def read_material(params: dict) -> dict:
    """Read a material or material instance's parameters and structure."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Material not found: {asset_path}")

    class_name = asset.get_class().get_name()

    result = {
        "assetPath": asset_path,
        "class": class_name,
        "name": asset.get_name(),
    }

    if isinstance(asset, unreal.MaterialInstance) or class_name in ("MaterialInstanceConstant", "MaterialInstanceDynamic"):
        result.update(_read_material_instance(asset))
    elif isinstance(asset, unreal.Material) or class_name == "Material":
        result.update(_read_base_material(asset))
    else:
        result["note"] = f"Asset is {class_name}, not a recognized material type. Returning basic info."

    return result


def set_material_parameter(params: dict) -> dict:
    """Set a scalar, vector, or texture parameter on a material instance."""
    asset_path = params.get("assetPath", "")
    param_name = params.get("parameterName", "")
    value = params.get("value", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Material instance not found: {asset_path}")

    if not hasattr(asset, "set_scalar_parameter_value"):
        raise ValueError(f"Asset is {asset.get_class().get_name()}, not a MaterialInstance. Cannot set parameters on base Materials.")

    if isinstance(value, (int, float)):
        asset.set_scalar_parameter_value(param_name, float(value))
        return {"success": True, "parameterName": param_name, "type": "scalar", "value": value}

    elif isinstance(value, dict):
        if "r" in value:
            color = unreal.LinearColor(
                r=value.get("r", 0), g=value.get("g", 0),
                b=value.get("b", 0), a=value.get("a", 1))
            asset.set_vector_parameter_value(param_name, color)
            return {"success": True, "parameterName": param_name, "type": "vector", "value": value}
        else:
            raise ValueError("Dict value must have r/g/b/a keys for vector parameters")

    elif isinstance(value, str):
        texture = unreal.EditorAssetLibrary.load_asset(value)
        if texture is None:
            raise ValueError(f"Texture not found: {value}")
        asset.set_texture_parameter_value(param_name, texture)
        return {"success": True, "parameterName": param_name, "type": "texture", "value": value}

    else:
        raise ValueError(f"Unsupported value type: {type(value).__name__}. Use number (scalar), dict with r/g/b/a (vector), or string path (texture).")


def create_material_instance(params: dict) -> dict:
    """Create a new material instance from a parent material."""
    from ._util import resolve_asset_path, ensure_asset_cleared

    parent_path = params.get("parentPath", "")
    instance_path = params.get("instancePath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    parent = unreal.EditorAssetLibrary.load_asset(parent_path)
    if parent is None:
        raise ValueError(f"Parent material not found: {parent_path}")

    if instance_path and "/" in instance_path:
        package_path = "/".join(instance_path.split("/")[:-1])
        asset_name = instance_path.split("/")[-1]
        full_path = instance_path
    else:
        asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Materials")
        if not asset_name:
            raise ValueError("instancePath or name+packagePath is required")

    ensure_asset_cleared(full_path)

    factory = unreal.MaterialInstanceConstantFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    instance = asset_tools.create_asset(asset_name, package_path, unreal.MaterialInstanceConstant, factory)
    if instance is None:
        raise RuntimeError(f"Failed to create material instance at {full_path}")

    instance.set_editor_property("parent", parent)

    return {
        "success": True,
        "instancePath": full_path,
        "parentPath": parent_path,
        "name": instance.get_name(),
    }


def list_material_parameters(params: dict) -> dict:
    """List all overridable parameters on a material or material instance."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Material not found: {asset_path}")

    scalar_params = []
    vector_params = []
    texture_params = []

    try:
        if hasattr(unreal, "MaterialEditingLibrary"):
            lib = unreal.MaterialEditingLibrary

            scalar_names = lib.get_scalar_parameter_names(asset) if hasattr(lib, "get_scalar_parameter_names") else []
            for name in scalar_names:
                val = None
                try:
                    val = lib.get_scalar_parameter_value(asset, name)
                except Exception:
                    pass
                scalar_params.append({"name": name, "value": val})

            vector_names = lib.get_vector_parameter_names(asset) if hasattr(lib, "get_vector_parameter_names") else []
            for name in vector_names:
                val = None
                try:
                    v = lib.get_vector_parameter_value(asset, name)
                    val = {"r": v.r, "g": v.g, "b": v.b, "a": v.a}
                except Exception:
                    pass
                vector_params.append({"name": name, "value": val})

            texture_names = lib.get_texture_parameter_names(asset) if hasattr(lib, "get_texture_parameter_names") else []
            for name in texture_names:
                val = None
                try:
                    tex = lib.get_texture_parameter_value(asset, name)
                    val = tex.get_path_name() if tex else None
                except Exception:
                    pass
                texture_params.append({"name": name, "value": val})
    except Exception as e:
        return {
            "assetPath": asset_path,
            "error": f"MaterialEditingLibrary not available: {e}",
        }

    return {
        "assetPath": asset_path,
        "class": asset.get_class().get_name(),
        "scalarParameters": scalar_params,
        "vectorParameters": vector_params,
        "textureParameters": texture_params,
        "totalParameters": len(scalar_params) + len(vector_params) + len(texture_params),
    }


def _read_material_instance(asset) -> dict:
    """Extract parameter overrides from a material instance."""
    result = {"isMaterialInstance": True}

    try:
        parent = asset.get_editor_property("parent")
        result["parent"] = parent.get_path_name() if parent else None
    except Exception:
        result["parent"] = None

    scalar_overrides = []
    vector_overrides = []
    texture_overrides = []

    try:
        for override in asset.get_editor_property("scalar_parameter_values"):
            scalar_overrides.append({
                "name": str(override.get_editor_property("parameter_info").get_editor_property("name")),
                "value": override.get_editor_property("parameter_value"),
            })
    except Exception:
        pass

    try:
        for override in asset.get_editor_property("vector_parameter_values"):
            val = override.get_editor_property("parameter_value")
            vector_overrides.append({
                "name": str(override.get_editor_property("parameter_info").get_editor_property("name")),
                "value": {"r": val.r, "g": val.g, "b": val.b, "a": val.a},
            })
    except Exception:
        pass

    try:
        for override in asset.get_editor_property("texture_parameter_values"):
            tex = override.get_editor_property("parameter_value")
            texture_overrides.append({
                "name": str(override.get_editor_property("parameter_info").get_editor_property("name")),
                "value": tex.get_path_name() if tex else None,
            })
    except Exception:
        pass

    result["scalarOverrides"] = scalar_overrides
    result["vectorOverrides"] = vector_overrides
    result["textureOverrides"] = texture_overrides
    result["totalOverrides"] = len(scalar_overrides) + len(vector_overrides) + len(texture_overrides)

    return result


def _read_base_material(asset) -> dict:
    """Extract info from a base Material asset."""
    result = {"isMaterialInstance": False}

    try:
        result["shadingModel"] = str(asset.get_editor_property("shading_model"))
    except Exception:
        pass

    try:
        result["blendMode"] = str(asset.get_editor_property("blend_mode"))
    except Exception:
        pass

    try:
        result["twoSided"] = asset.get_editor_property("two_sided")
    except Exception:
        pass

    try:
        if hasattr(unreal, "MaterialEditingLibrary"):
            lib = unreal.MaterialEditingLibrary
            expressions = lib.get_material_expressions(asset) if hasattr(lib, "get_material_expressions") else []
            result["expressionCount"] = len(expressions)

            expr_types = {}
            for expr in expressions:
                t = expr.get_class().get_name()
                expr_types[t] = expr_types.get(t, 0) + 1
            result["expressionTypes"] = expr_types
    except Exception:
        pass

    return result


HANDLERS = {
    "read_material": read_material,
    "set_material_parameter": set_material_parameter,
    "create_material_instance": create_material_instance,
    "list_material_parameters": list_material_parameters,
}
