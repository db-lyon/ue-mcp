"""Material authoring handlers - create materials from scratch, not just edit instances."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def create_material(params: dict) -> dict:
    from ._util import resolve_asset_path, ensure_asset_cleared

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Materials")
    if not asset_name:
        raise ValueError("name or path is required")

    ensure_asset_cleared(full_path)

    factory = unreal.MaterialFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = asset_tools.create_asset(asset_name, package_path, unreal.Material, factory)

    if mat is None:
        raise RuntimeError(f"Failed to create material at {full_path}")

    return {"path": full_path, "name": mat.get_name(), "success": True}


def set_material_shading_model(params: dict) -> dict:
    asset_path = params.get("path", "") or params.get("assetPath", "")
    shading_model = params.get("shadingModel", "DefaultLit")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    mat = unreal.EditorAssetLibrary.load_asset(asset_path)
    if mat is None:
        raise ValueError(f"Material not found: {asset_path}")

    if hasattr(unreal, "MaterialShadingModel"):
        model_map = {
            "DefaultLit": unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
            "Unlit": unreal.MaterialShadingModel.MSM_UNLIT,
            "Subsurface": unreal.MaterialShadingModel.MSM_SUBSURFACE,
            "ClearCoat": unreal.MaterialShadingModel.MSM_CLEAR_COAT,
            "SubsurfaceProfile": unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE,
            "TwoSidedFoliage": unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE,
        }
        enum_val = model_map.get(shading_model)
        if enum_val is not None:
            mat.set_editor_property("shading_model", enum_val)
    elif hasattr(unreal, "EMaterialShadingModel"):
        model_map = {
            "DefaultLit": unreal.EMaterialShadingModel.MSM_DEFAULT_LIT,
            "Unlit": unreal.EMaterialShadingModel.MSM_UNLIT,
        }
        enum_val = model_map.get(shading_model)
        if enum_val is not None:
            mat.set_editor_property("shading_model", enum_val)

    return {"path": asset_path, "shadingModel": shading_model, "success": True}


def set_material_base_color(params: dict) -> dict:
    """Set the base color of a material to a constant value."""
    asset_path = params.get("path", "") or params.get("assetPath", "")
    color_raw = params.get("color", [1.0, 1.0, 1.0])
    if isinstance(color_raw, dict):
        color = [color_raw.get("r", 1.0), color_raw.get("g", 1.0), color_raw.get("b", 1.0), color_raw.get("a", 1.0)]
    else:
        color = color_raw

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
