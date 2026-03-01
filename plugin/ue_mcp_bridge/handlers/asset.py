"""
Asset-related handlers using UE Python API.
These provide the live-mode counterparts to the offline UAssetAPI readers.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _discover_plugin_mount_points() -> list[str]:
    """Find plugin content mount points by scanning the Plugins directory."""
    import os
    if not HAS_UNREAL:
        return []
    try:
        project_dir = str(unreal.Paths.project_dir())
        plugins_dir = os.path.join(project_dir, "Plugins")
        if not os.path.isdir(plugins_dir):
            return []
        mount_points = []
        def scan(d):
            for entry in os.scandir(d):
                if not entry.is_dir():
                    continue
                has_uplugin = any(f.endswith(".uplugin") for f in os.listdir(entry.path))
                content_dir = os.path.join(entry.path, "Content")
                if has_uplugin and os.path.isdir(content_dir):
                    mount_points.append(f"/{entry.name}/")
                elif not has_uplugin:
                    scan(entry.path)
        scan(plugins_dir)
        return sorted(mount_points)
    except Exception:
        return []


def read_asset(params: dict) -> dict:
    """Read an asset via the editor's asset registry and reflection."""
    asset_path = params.get("path", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_data = registry.get_asset_by_object_path(asset_path)

    if not asset_data.is_valid():
        asset_path_clean = asset_path.replace(".uasset", "")
        asset_data = registry.get_asset_by_object_path(asset_path_clean)

    if not asset_data.is_valid():
        raise ValueError(f"Asset not found: {asset_path}")

    asset = asset_data.get_asset()
    class_name = asset.get_class().get_name()

    result = {
        "path": str(asset_data.package_name),
        "className": class_name,
        "objectName": asset.get_name(),
    }

    props = _get_object_properties(asset)
    result["properties"] = props

    return result


def _get_object_properties(obj) -> list[dict]:
    """Extract properties from a UObject using editor reflection."""
    if not HAS_UNREAL:
        return []

    props = []
    for prop_name in dir(obj):
        if prop_name.startswith("_"):
            continue
        try:
            val = getattr(obj, prop_name)
            if callable(val):
                continue
            props.append({
                "name": prop_name,
                "type": type(val).__name__,
                "value": _serialize_value(val),
            })
        except Exception:
            continue

    return props


def _serialize_value(val):
    """Convert UE Python values to JSON-serializable types."""
    if val is None:
        return None
    if isinstance(val, (bool, int, float, str)):
        return val
    if isinstance(val, (list, tuple)):
        return [_serialize_value(v) for v in val]
    if isinstance(val, dict):
        return {str(k): _serialize_value(v) for k, v in val.items()}

    if HAS_UNREAL:
        if isinstance(val, unreal.Vector):
            return {"x": val.x, "y": val.y, "z": val.z}
        if isinstance(val, unreal.Rotator):
            return {"pitch": val.pitch, "yaw": val.yaw, "roll": val.roll}
        if isinstance(val, unreal.LinearColor):
            return {"r": val.r, "g": val.g, "b": val.b, "a": val.a}
        if isinstance(val, unreal.Transform):
            return {
                "translation": _serialize_value(val.translation),
                "rotation": _serialize_value(val.rotation.rotator()),
                "scale": _serialize_value(val.scale3d),
            }

    return str(val)


def read_datatable(params: dict) -> dict:
    """Read a DataTable via the editor API."""
    asset_path = params.get("path", "")
    row_filter = params.get("rowFilter")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    dt = unreal.EditorAssetLibrary.load_asset(asset_path)
    if dt is None:
        raise ValueError(f"DataTable not found: {asset_path}")

    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    rows = []

    for name in row_names:
        name_str = str(name)
        if row_filter and row_filter.lower() not in name_str.lower():
            continue
        rows.append({
            "rowName": name_str,
        })

    return {
        "path": asset_path,
        "rowCount": len(rows),
        "rows": rows,
    }


def reimport_datatable(params: dict) -> dict:
    """Reimport a DataTable from a JSON file or JSON string."""
    asset_path = params.get("path", "")
    json_path = params.get("jsonPath", "")
    json_string = params.get("jsonString", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    dt = unreal.EditorAssetLibrary.load_asset(asset_path)
    if dt is None:
        raise ValueError(f"DataTable not found: {asset_path}")

    if json_path:
        import os
        if not os.path.isfile(json_path):
            raise ValueError(f"JSON file not found: {json_path}")
        with open(json_path, "r", encoding="utf-8") as f:
            json_string = f.read()

    if not json_string:
        raise ValueError("Provide either jsonPath or jsonString")

    result = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json_string)

    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)

    if result:
        unreal.EditorAssetLibrary.save_asset(asset_path)

    return {
        "path": asset_path,
        "success": result,
        "rowCount": len(row_names),
    }


def create_datatable(params: dict) -> dict:
    """Create a new DataTable asset with a given row struct."""
    asset_name = params.get("name", "DT_New")
    package_path = params.get("packagePath", "/Game/Data")
    row_struct = params.get("rowStruct", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not row_struct:
        raise ValueError("rowStruct is required (e.g. 'InventoryItem', '/Script/MyGame.FMyRowStruct')")

    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "DataTableFactory"):
        factory = unreal.DataTableFactory()
        struct_obj = unreal.EditorAssetLibrary.load_asset(row_struct)
        if struct_obj is None and hasattr(unreal, "find_object"):
            struct_obj = unreal.find_object(None, row_struct)
        if struct_obj is None:
            try:
                struct_obj = unreal.EditorAssetLibrary.load_asset(f"/Script/Engine.{row_struct}")
            except Exception:
                pass
        if struct_obj is not None:
            try:
                factory.set_editor_property("struct", struct_obj)
            except Exception:
                pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        asset = tools.create_asset(asset_name, package_path, unreal.DataTable, None)

    if asset is None:
        raise RuntimeError(f"Failed to create DataTable at {package_path}/{asset_name}")

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
        "rowStruct": row_struct,
    }


def search_assets(params: dict) -> dict:
    """Search for assets by name, class, or path using the Asset Registry."""
    query = params.get("query", "")
    directory = params.get("directory", "/Game/")
    max_results = params.get("maxResults", 50)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    results = []

    try:
        assets = unreal.EditorAssetLibrary.list_assets(directory, recursive=True)
        query_lower = query.lower()

        for asset_path in assets:
            if len(results) >= max_results:
                break
            path_str = str(asset_path)
            if query_lower not in path_str.lower():
                continue

            info = {"path": path_str, "name": path_str.split("/")[-1].split(".")[0]}

            try:
                asset_data = registry.get_asset_by_object_path(asset_path)
                if asset_data and asset_data.is_valid():
                    try:
                        class_path = asset_data.get_editor_property("asset_class_path")
                        info["className"] = str(class_path.get_editor_property("asset_name"))
                    except Exception:
                        pass
            except Exception:
                pass

            results.append(info)
    except Exception as e:
        return {"error": str(e), "query": query, "results": []}

    response = {"query": query, "directory": directory, "resultCount": len(results), "results": results}

    if len(results) == 0 and directory == "/Game/":
        plugin_dirs = _discover_plugin_mount_points()
        if plugin_dirs:
            response["suggestion"] = (
                f"No results in /Game/. This project has plugin content — "
                f"try searching in: {', '.join(plugin_dirs)}"
            )
            response["availablePlugins"] = plugin_dirs

    return response


def read_asset_properties(params: dict) -> dict:
    """Read specific properties from an asset, optionally filtered by export or property name."""
    asset_path = params.get("assetPath", params.get("path", ""))
    export_name = params.get("exportName", None)
    property_name = params.get("propertyName", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Asset not found: {asset_path}")

    if property_name:
        try:
            val = asset.get_editor_property(property_name)
            return {
                "path": asset_path,
                "propertyName": property_name,
                "type": type(val).__name__,
                "value": _serialize_value(val),
            }
        except Exception as e:
            raise ValueError(f"Property '{property_name}' not found on asset: {e}")

    props = _get_object_properties(asset)
    return {
        "path": asset_path,
        "className": asset.get_class().get_name(),
        "propertyCount": len(props),
        "properties": props,
    }


HANDLERS = {
    "read_asset": read_asset,
    "read_datatable": read_datatable,
    "reimport_datatable": reimport_datatable,
    "create_datatable": create_datatable,
    "search_assets": search_assets,
    "read_asset_properties": read_asset_properties,
}
