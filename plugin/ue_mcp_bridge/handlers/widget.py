"""Widget / UMG authoring handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def create_widget_blueprint(params: dict) -> dict:
    asset_path = params.get("path", "")
    parent_class = params.get("parentClass", "UserWidget")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    if hasattr(unreal, "WidgetBlueprintFactory"):
        factory = unreal.WidgetBlueprintFactory()
        wb = asset_tools.create_asset(asset_name, package_path, None, factory)
    else:
        wb = asset_tools.create_asset(asset_name, package_path, unreal.WidgetBlueprint, None)

    if wb is None:
        raise RuntimeError(f"Failed to create WidgetBlueprint at {asset_path}")

    return {"path": asset_path, "success": True}


def list_widget_blueprints(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    widgets = []
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in ("WidgetBlueprint",):
            widgets.append({
                "path": str(ad.package_name),
                "name": str(ad.asset_name),
            })

    return {"directory": directory, "count": len(widgets), "widgets": widgets}


def get_widget_tree(params: dict) -> dict:
    """Get the widget hierarchy of a widget blueprint."""
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    wb = unreal.EditorAssetLibrary.load_asset(asset_path)
    if wb is None:
        raise ValueError(f"Widget blueprint not found: {asset_path}")

    info = {
        "path": asset_path,
        "name": wb.get_name(),
        "className": wb.get_class().get_name(),
    }

    if hasattr(wb, "widget_tree") and wb.get_editor_property("widget_tree"):
        tree = wb.get_editor_property("widget_tree")
        root = tree.get_editor_property("root_widget") if hasattr(tree, "root_widget") else None
        if root:
            info["rootWidget"] = {
                "name": root.get_name(),
                "className": root.get_class().get_name(),
            }

    return info


HANDLERS = {
    "create_widget_blueprint": create_widget_blueprint,
    "list_widget_blueprints": list_widget_blueprints,
    "get_widget_tree": get_widget_tree,
}
