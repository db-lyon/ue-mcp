"""Material graph authoring: expression nodes, connections, and layout."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def _require_mat_lib():
    if not hasattr(unreal, "MaterialEditingLibrary"):
        raise RuntimeError("MaterialEditingLibrary not available")
    return unreal.MaterialEditingLibrary


def _load_material(path):
    mat = unreal.EditorAssetLibrary.load_asset(path)
    if mat is None:
        raise ValueError(f"Material not found: {path}")
    return mat


EXPRESSION_MAP = {
    "Constant": "MaterialExpressionConstant",
    "Constant2Vector": "MaterialExpressionConstant2Vector",
    "Constant3Vector": "MaterialExpressionConstant3Vector",
    "Constant4Vector": "MaterialExpressionConstant4Vector",
    "TextureSample": "MaterialExpressionTextureSample",
    "TextureCoordinate": "MaterialExpressionTextureCoordinate",
    "Multiply": "MaterialExpressionMultiply",
    "Add": "MaterialExpressionAdd",
    "Subtract": "MaterialExpressionSubtract",
    "Divide": "MaterialExpressionDivide",
    "Lerp": "MaterialExpressionLinearInterpolate",
    "Clamp": "MaterialExpressionClamp",
    "Power": "MaterialExpressionPower",
    "Abs": "MaterialExpressionAbs",
    "OneMinus": "MaterialExpressionOneMinus",
    "Dot": "MaterialExpressionDotProduct",
    "Cross": "MaterialExpressionCrossProduct",
    "Normalize": "MaterialExpressionNormalize",
    "Fresnel": "MaterialExpressionFresnel",
    "Panner": "MaterialExpressionPanner",
    "Rotator": "MaterialExpressionRotator",
    "Time": "MaterialExpressionTime",
    "WorldPosition": "MaterialExpressionWorldPosition",
    "VertexNormalWS": "MaterialExpressionVertexNormalWS",
    "CameraPositionWS": "MaterialExpressionCameraPositionWS",
    "PixelDepth": "MaterialExpressionPixelDepth",
    "SceneDepth": "MaterialExpressionSceneDepth",
    "Noise": "MaterialExpressionNoise",
    "ComponentMask": "MaterialExpressionComponentMask",
    "AppendVector": "MaterialExpressionAppendVector",
    "If": "MaterialExpressionIf",
    "StaticSwitch": "MaterialExpressionStaticSwitch",
    "ScalarParameter": "MaterialExpressionScalarParameter",
    "VectorParameter": "MaterialExpressionVectorParameter",
    "TextureSampleParameter2D": "MaterialExpressionTextureSampleParameter2D",
    "StaticBoolParameter": "MaterialExpressionStaticBoolParameter",
    "VertexColor": "MaterialExpressionVertexColor",
    "ParticleColor": "MaterialExpressionParticleColor",
    "ScreenPosition": "MaterialExpressionScreenPosition",
    "Custom": "MaterialExpressionCustom",
}

MATERIAL_PROPERTY_MAP = {
    "BaseColor": "MP_BASE_COLOR",
    "Metallic": "MP_METALLIC",
    "Specular": "MP_SPECULAR",
    "Roughness": "MP_ROUGHNESS",
    "Anisotropy": "MP_ANISOTROPY",
    "EmissiveColor": "MP_EMISSIVE_COLOR",
    "Opacity": "MP_OPACITY",
    "OpacityMask": "MP_OPACITY_MASK",
    "Normal": "MP_NORMAL",
    "Tangent": "MP_TANGENT",
    "WorldPositionOffset": "MP_WORLD_POSITION_OFFSET",
    "SubsurfaceColor": "MP_SUBSURFACE_COLOR",
    "AmbientOcclusion": "MP_AMBIENT_OCCLUSION",
    "Refraction": "MP_REFRACTION",
    "PixelDepthOffset": "MP_PIXEL_DEPTH_OFFSET",
    "ShadingModel": "MP_SHADING_MODEL",
}


def add_expression(params: dict) -> dict:
    """Add a material expression node to a material graph."""
    _require_unreal()
    mel = _require_mat_lib()
    mat = _load_material(params["materialPath"])

    expr_type = params["expressionType"]
    x = params.get("x", 0)
    y = params.get("y", 0)

    class_name = EXPRESSION_MAP.get(expr_type, expr_type)
    expr_class = getattr(unreal, class_name, None)
    if expr_class is None:
        if not class_name.startswith("MaterialExpression"):
            class_name = "MaterialExpression" + class_name
        expr_class = getattr(unreal, class_name, None)
    if expr_class is None:
        raise ValueError(f"Unknown expression type: {expr_type}. Use list_expression_types to see available types.")

    expr = mel.create_material_expression(mat, expr_class, x, y)
    if expr is None:
        raise RuntimeError(f"Failed to create expression: {expr_type}")

    props = params.get("properties", {})
    for key, val in props.items():
        if hasattr(expr, key):
            if key == "constant" and isinstance(val, dict):
                expr.set_editor_property(key, unreal.LinearColor(val.get("r", 0), val.get("g", 0), val.get("b", 0), val.get("a", 1)))
            elif key == "texture" and isinstance(val, str):
                tex = unreal.EditorAssetLibrary.load_asset(val)
                if tex:
                    expr.set_editor_property(key, tex)
            else:
                try:
                    expr.set_editor_property(key, val)
                except Exception:
                    pass

    return {
        "materialPath": params["materialPath"],
        "expressionType": expr_type,
        "nodeId": expr.get_name(),
        "position": {"x": x, "y": y},
        "success": True,
    }


def connect_expressions(params: dict) -> dict:
    """Connect two material expressions together."""
    _require_unreal()
    mel = _require_mat_lib()
    mat = _load_material(params["materialPath"])

    source_name = params["sourceExpression"]
    source_output = params.get("sourceOutput", "")
    target_name = params["targetExpression"]
    target_input = params.get("targetInput", "")

    expressions = mel.get_material_expressions(mat) if hasattr(mel, "get_material_expressions") else []

    source_expr = None
    target_expr = None
    for expr in expressions:
        if expr.get_name() == source_name:
            source_expr = expr
        if expr.get_name() == target_name:
            target_expr = expr

    if source_expr is None:
        raise ValueError(f"Source expression not found: {source_name}")
    if target_expr is None:
        raise ValueError(f"Target expression not found: {target_name}")

    if hasattr(mel, "connect_material_expressions"):
        mel.connect_material_expressions(source_expr, source_output, target_expr, target_input)

    return {
        "materialPath": params["materialPath"],
        "source": source_name,
        "target": target_name,
        "success": True,
    }


def connect_to_material_property(params: dict) -> dict:
    """Connect an expression output to a material property (BaseColor, Normal, etc.)."""
    _require_unreal()
    mel = _require_mat_lib()
    mat = _load_material(params["materialPath"])

    expr_name = params["expressionName"]
    output_name = params.get("outputName", "")
    property_name = params["property"]

    expressions = mel.get_material_expressions(mat) if hasattr(mel, "get_material_expressions") else []
    expr = None
    for e in expressions:
        if e.get_name() == expr_name:
            expr = e
            break

    if expr is None:
        raise ValueError(f"Expression not found: {expr_name}")

    prop_key = MATERIAL_PROPERTY_MAP.get(property_name)
    if prop_key is None:
        raise ValueError(f"Unknown material property: {property_name}. Available: {list(MATERIAL_PROPERTY_MAP.keys())}")

    mat_prop = getattr(unreal.MaterialProperty, prop_key, None)
    if mat_prop is not None:
        mel.connect_material_property(expr, output_name, mat_prop)

    return {"materialPath": params["materialPath"], "expression": expr_name, "property": property_name, "success": True}


def list_expressions(params: dict) -> dict:
    """List all expression nodes in a material graph."""
    _require_unreal()
    mel = _require_mat_lib()
    mat = _load_material(params["materialPath"])

    expressions = mel.get_material_expressions(mat) if hasattr(mel, "get_material_expressions") else []
    results = []
    for expr in expressions:
        info = {"name": expr.get_name(), "class": expr.get_class().get_name()}
        if hasattr(expr, "material_expression_editor_x"):
            info["x"] = expr.get_editor_property("material_expression_editor_x")
            info["y"] = expr.get_editor_property("material_expression_editor_y")
        results.append(info)

    return {"materialPath": params["materialPath"], "count": len(results), "expressions": results}


def delete_expression(params: dict) -> dict:
    """Delete a material expression node."""
    _require_unreal()
    mel = _require_mat_lib()
    mat = _load_material(params["materialPath"])

    expr_name = params["expressionName"]
    expressions = mel.get_material_expressions(mat) if hasattr(mel, "get_material_expressions") else []

    for expr in expressions:
        if expr.get_name() == expr_name:
            if hasattr(mel, "delete_material_expression"):
                mel.delete_material_expression(mat, expr)
                return {"materialPath": params["materialPath"], "deleted": expr_name, "success": True}

    raise ValueError(f"Expression not found: {expr_name}")


def list_expression_types(params: dict) -> dict:
    """List available material expression types."""
    return {
        "expressionTypes": sorted(EXPRESSION_MAP.keys()),
        "materialProperties": sorted(MATERIAL_PROPERTY_MAP.keys()),
        "count": len(EXPRESSION_MAP),
    }


def recompile_material(params: dict) -> dict:
    """Recompile a material after graph changes."""
    _require_unreal()
    mel = _require_mat_lib()
    mat = _load_material(params["materialPath"])
    mel.recompile_material(mat)
    return {"materialPath": params["materialPath"], "success": True}


HANDLERS = {
    "add_material_expression": add_expression,
    "connect_material_expressions": connect_expressions,
    "connect_to_material_property": connect_to_material_property,
    "list_material_expressions": list_expressions,
    "delete_material_expression": delete_expression,
    "list_expression_types": list_expression_types,
    "recompile_material": recompile_material,
}
