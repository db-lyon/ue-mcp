"""Material authoring handlers - create materials from scratch, not just edit instances."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def create_material(params: dict) -> dict:
    asset_path = params.get("path", "")
    shading_model = params.get("shadingModel", "DefaultLit")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    factory = unreal.MaterialFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = asset_tools.create_asset(asset_name, package_path, unreal.Material, factory)

    if mat is None:
        raise RuntimeError(f"Failed to create material at {asset_path}")

    return {"path": asset_path, "name": mat.get_name(), "success": True}


def set_material_shading_model(params: dict) -> dict:
    asset_path = params.get("path", "")
    shading_model = params.get("shadingModel", "DefaultLit")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    mat = unreal.EditorAssetLibrary.load_asset(asset_path)
    if mat is None:
        raise ValueError(f"Material not found: {asset_path}")

    model_map = {
        "DefaultLit": 1,
        "Unlit": 0,
        "Subsurface": 2,
        "ClearCoat": 4,
        "SubsurfaceProfile": 5,
        "TwoSidedFoliage": 6,
    }

    if shading_model in model_map:
        mat.set_editor_property("shading_model", model_map[shading_model])

    return {"path": asset_path, "shadingModel": shading_model, "success": True}


def set_material_base_color(params: dict) -> dict:
    """Set the base color of a material to a constant value."""
    asset_path = params.get("path", "")
    color = params.get("color", [1.0, 1.0, 1.0])

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    mat = unreal.EditorAssetLibrary.load_asset(asset_path)
    if mat is None:
        raise ValueError(f"Material not found: {asset_path}")

    if hasattr(unreal, "MaterialEditingLibrary"):
        r, g, b = float(color[0]), float(color[1]), float(color[2])
        a = float(color[3]) if len(color) > 3 else 1.0
        expr = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionConstant4Vector, -300, 0
        )
        if expr:
            expr.set_editor_property("constant", unreal.LinearColor(r, g, b, a))
            unreal.MaterialEditingLibrary.connect_material_property(
                expr, "RGBA", unreal.MaterialProperty.MP_BASE_COLOR
            )
        unreal.MaterialEditingLibrary.recompile_material(mat)
        return {"path": asset_path, "color": color, "success": True}

    raise RuntimeError("MaterialEditingLibrary not available")


def connect_texture_to_material(params: dict) -> dict:
    material_path = params.get("materialPath", "")
    texture_path = params.get("texturePath", "")
    property_name = params.get("property", "BaseColor")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    mat = unreal.EditorAssetLibrary.load_asset(material_path)
    if mat is None:
        raise ValueError(f"Material not found: {material_path}")

    texture = unreal.EditorAssetLibrary.load_asset(texture_path)
    if texture is None:
        raise ValueError(f"Texture not found: {texture_path}")

    if not hasattr(unreal, "MaterialEditingLibrary"):
        raise RuntimeError("MaterialEditingLibrary not available")

    expr = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionTextureSample, -400, 0
    )
    if expr:
        expr.set_editor_property("texture", texture)

        prop_map = {
            "BaseColor": unreal.MaterialProperty.MP_BASE_COLOR,
            "Normal": unreal.MaterialProperty.MP_NORMAL,
            "Roughness": unreal.MaterialProperty.MP_ROUGHNESS,
            "Metallic": unreal.MaterialProperty.MP_METALLIC,
            "Emissive": unreal.MaterialProperty.MP_EMISSIVE_COLOR,
            "Opacity": unreal.MaterialProperty.MP_OPACITY,
            "AO": unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,
        }

        mat_prop = prop_map.get(property_name)
        if mat_prop:
            out_pin = "RGB" if property_name != "Normal" else "RGB"
            unreal.MaterialEditingLibrary.connect_material_property(expr, out_pin, mat_prop)

    unreal.MaterialEditingLibrary.recompile_material(mat)

    return {"materialPath": material_path, "texturePath": texture_path, "property": property_name, "success": True}


HANDLERS = {
    "create_material": create_material,
    "set_material_shading_model": set_material_shading_model,
    "set_material_base_color": set_material_base_color,
    "connect_texture_to_material": connect_texture_to_material,
}
