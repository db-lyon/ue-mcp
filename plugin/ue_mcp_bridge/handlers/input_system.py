"""Enhanced Input handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def create_input_action(params: dict) -> dict:
    asset_path = params.get("path", "")
    value_type = params.get("valueType", "Bool")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    if not hasattr(unreal, "InputAction"):
        raise RuntimeError("Enhanced Input not available")

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = None

    if hasattr(unreal, "InputActionFactory"):
        factory = unreal.InputActionFactory()

    if factory:
        action = asset_tools.create_asset(asset_name, package_path, unreal.InputAction, factory)
    else:
        action = asset_tools.create_asset(asset_name, package_path, unreal.InputAction, None)

    if action is None:
        raise RuntimeError(f"Failed to create InputAction at {asset_path}")

    type_map = {
        "Bool": unreal.InputActionValueType.BOOLEAN if hasattr(unreal, "InputActionValueType") else None,
        "Axis1D": unreal.InputActionValueType.AXIS1D if hasattr(unreal, "InputActionValueType") else None,
        "Axis2D": unreal.InputActionValueType.AXIS2D if hasattr(unreal, "InputActionValueType") else None,
        "Axis3D": unreal.InputActionValueType.AXIS3D if hasattr(unreal, "InputActionValueType") else None,
    }

    vt = type_map.get(value_type)
    if vt and hasattr(action, "value_type"):
        action.set_editor_property("value_type", vt)

    return {"path": asset_path, "valueType": value_type, "success": True}


def create_input_mapping_context(params: dict) -> dict:
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    if not hasattr(unreal, "InputMappingContext"):
        raise RuntimeError("Enhanced Input not available")

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    ctx = asset_tools.create_asset(asset_name, package_path, unreal.InputMappingContext, None)

    if ctx is None:
        raise RuntimeError(f"Failed to create InputMappingContext at {asset_path}")

    return {"path": asset_path, "success": True}


def list_input_assets(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    input_assets = []
    input_types = {"InputAction", "InputMappingContext"}
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in input_types:
            input_assets.append({
                "path": str(ad.package_name),
                "name": str(ad.asset_name),
                "type": class_name,
            })

    return {"directory": directory, "count": len(input_assets), "assets": input_assets}


HANDLERS = {
    "create_input_action": create_input_action,
    "create_input_mapping_context": create_input_mapping_context,
    "list_input_assets": list_input_assets,
}
