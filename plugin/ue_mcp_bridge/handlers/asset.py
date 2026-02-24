"""
Asset-related handlers using UE Python API.
These provide the live-mode counterparts to the offline UAssetAPI readers.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


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


HANDLERS = {
    "read_asset": read_asset,
    "read_datatable": read_datatable,
}
