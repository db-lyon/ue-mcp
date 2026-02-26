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


def create_editor_utility_widget(params: dict) -> dict:
    """Create an Editor Utility Widget Blueprint — a UMG panel that runs inside the editor."""
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not asset_path:
        raise ValueError("path is required (e.g. '/Game/EditorTools/EUW_TuningPanel')")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "EditorUtilityWidgetBlueprintFactory"):
        factory = unreal.EditorUtilityWidgetBlueprintFactory()
    elif hasattr(unreal, "WidgetBlueprintFactory"):
        factory = unreal.WidgetBlueprintFactory()
        parent = getattr(unreal, "EditorUtilityWidget", None)
        if parent and hasattr(factory, "set_editor_property"):
            try:
                factory.set_editor_property("parent_class", parent)
            except Exception:
                pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        raise RuntimeError("EditorUtilityWidgetBlueprintFactory not available")

    if asset is None:
        raise RuntimeError(f"Failed to create Editor Utility Widget at {asset_path}")

    unreal.EditorAssetLibrary.save_asset(asset_path)

    return {
        "path": asset_path,
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


def run_editor_utility_widget(params: dict) -> dict:
    """Open an Editor Utility Widget as a docked tab in the editor."""
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not asset_path:
        raise ValueError("path is required")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Editor Utility Widget not found: {asset_path}")

    subsys = None
    if hasattr(unreal, "EditorUtilitySubsystem"):
        subsys = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)

    if subsys is None:
        raise RuntimeError("EditorUtilitySubsystem not available")

    if hasattr(subsys, "spawn_and_register_tab"):
        subsys.spawn_and_register_tab(asset)
        return {"path": asset_path, "success": True, "action": "opened as tab"}
    elif hasattr(subsys, "spawn_registered_tab_by_id"):
        subsys.spawn_registered_tab_by_id(asset.get_name())
        return {"path": asset_path, "success": True, "action": "opened by id"}

    raise RuntimeError("Could not open EUW — no spawn method available")


def run_editor_utility_blueprint(params: dict) -> dict:
    """Execute an Editor Utility Blueprint (headless editor script)."""
    asset_path = params.get("path", "")
    function_name = params.get("functionName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not asset_path:
        raise ValueError("path is required")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Editor Utility Blueprint not found: {asset_path}")

    subsys = None
    if hasattr(unreal, "EditorUtilitySubsystem"):
        subsys = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)

    if subsys is None:
        raise RuntimeError("EditorUtilitySubsystem not available")

    if function_name and hasattr(subsys, "try_run_editor_utility_widget_with_function"):
        result = subsys.try_run_editor_utility_widget_with_function(asset, function_name)
        return {"path": asset_path, "functionName": function_name, "success": result}

    if hasattr(subsys, "try_run"):
        subsys.try_run(asset)
        return {"path": asset_path, "success": True, "action": "executed"}

    gen_class = None
    try:
        gen_class = asset.get_editor_property("generated_class")
    except Exception:
        pass

    if gen_class:
        cdo = unreal.get_default_object(gen_class) if hasattr(unreal, "get_default_object") else None
        if cdo and function_name:
            fn = getattr(cdo, function_name, None)
            if fn and callable(fn):
                fn()
                return {"path": asset_path, "functionName": function_name, "success": True, "action": "called via CDO"}

    raise RuntimeError("Could not execute EUB — no run method available")


def create_editor_utility_blueprint(params: dict) -> dict:
    """Create an Editor Utility Blueprint — a headless editor automation script."""
    asset_path = params.get("path", "")
    parent_class = params.get("parentClass", "EditorUtilityObject")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not asset_path:
        raise ValueError("path is required (e.g. '/Game/EditorTools/EUB_BatchRenamer')")

    package_path = "/".join(asset_path.split("/")[:-1])
    asset_name = asset_path.split("/")[-1]

    parent = getattr(unreal, parent_class, None)
    if parent is None:
        parent = unreal.EditorAssetLibrary.load_asset(parent_class)
    if parent is None:
        for name in ("EditorUtilityObject", "ActorActionUtility", "AssetActionUtility"):
            parent = getattr(unreal, name, None)
            if parent:
                break

    if parent is None:
        raise RuntimeError(f"Parent class not found: {parent_class}")

    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "EditorUtilityBlueprintFactory"):
        factory = unreal.EditorUtilityBlueprintFactory()
        try:
            factory.set_editor_property("parent_class", parent)
        except Exception:
            pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        raise RuntimeError("EditorUtilityBlueprintFactory not available")

    if asset is None:
        raise RuntimeError(f"Failed to create Editor Utility Blueprint at {asset_path}")

    unreal.EditorAssetLibrary.save_asset(asset_path)

    return {
        "path": asset_path,
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
        "parentClass": parent_class,
    }


HANDLERS = {
    "create_widget_blueprint": create_widget_blueprint,
    "list_widget_blueprints": list_widget_blueprints,
    "get_widget_tree": get_widget_tree,
    "create_editor_utility_widget": create_editor_utility_widget,
    "run_editor_utility_widget": run_editor_utility_widget,
    "run_editor_utility_blueprint": run_editor_utility_blueprint,
    "create_editor_utility_blueprint": create_editor_utility_blueprint,
}
