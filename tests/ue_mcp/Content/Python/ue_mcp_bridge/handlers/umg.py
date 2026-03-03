"""
Widget / UMG handlers — read widget trees, inspect widget properties,
modify widget Blueprints, and manage UI bindings.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def read_widget_tree(params: dict) -> dict:
    """Read the widget hierarchy of a Widget Blueprint."""
    asset_path = params.get("assetPath", "")
    max_depth = params.get("maxDepth", 20)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Widget Blueprint not found: {asset_path}")

    result = {
        "assetPath": asset_path,
        "class": asset.get_class().get_name(),
        "name": asset.get_name(),
    }

    try:
        parent = asset.get_editor_property("parent_class")
        result["parentClass"] = parent.get_name() if parent else None
    except Exception:
        result["parentClass"] = None

    try:
        widget_tree = asset.get_editor_property("widget_tree")
        if widget_tree:
            root = widget_tree.get_editor_property("root_widget")
            result["widgetTree"] = _serialize_widget(root, 0, max_depth) if root else None
        else:
            result["widgetTree"] = None
    except Exception as e:
        result["widgetTree"] = None
        result["treeError"] = str(e)

    animations = []
    try:
        anims = asset.get_editor_property("animations") or []
        for anim in anims:
            anim_info = {"name": anim.get_name()}
            try:
                anim_info["length"] = anim.get_editor_property("animation_length") if hasattr(anim, "get_editor_property") else None
            except Exception:
                pass
            animations.append(anim_info)
    except Exception:
        pass
    result["animations"] = animations

    variables = []
    try:
        gen_class = asset.get_editor_property("generated_class")
        if gen_class:
            cdo = unreal.get_default_object(gen_class) if hasattr(unreal, "get_default_object") else None
            if cdo:
                for prop_name in dir(cdo):
                    if prop_name.startswith("_"):
                        continue
                    try:
                        val = getattr(cdo, prop_name)
                        if callable(val):
                            continue
                        variables.append({
                            "name": prop_name,
                            "type": type(val).__name__,
                        })
                    except Exception:
                        continue
    except Exception:
        pass
    result["variables"] = variables

    return result


def get_widget_details(params: dict) -> dict:
    """Get detailed information about a specific widget in a Widget Blueprint."""
    asset_path = params.get("assetPath", "")
    widget_name = params.get("widgetName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Widget Blueprint not found: {asset_path}")

    try:
        widget_tree = asset.get_editor_property("widget_tree")
        if not widget_tree:
            raise ValueError("Widget tree is empty")

        target = _find_widget_by_name(widget_tree.get_editor_property("root_widget"), widget_name)
        if target is None:
            all_widgets = widget_tree.get_editor_property("all_widgets") if hasattr(widget_tree, "get_editor_property") else []
            for w in (all_widgets or []):
                if w.get_name() == widget_name:
                    target = w
                    break

        if target is None:
            raise ValueError(f"Widget not found: {widget_name}")

        return _get_widget_properties(target)
    except ValueError:
        raise
    except Exception as e:
        raise RuntimeError(f"Failed to read widget details: {e}")


def set_widget_property(params: dict) -> dict:
    """Set a property on a widget in a Widget Blueprint."""
    asset_path = params.get("assetPath", "")
    widget_name = params.get("widgetName", "")
    property_name = params.get("propertyName", "")
    value = params.get("value")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Widget Blueprint not found: {asset_path}")

    try:
        widget_tree = asset.get_editor_property("widget_tree")
        target = _find_widget_by_name(widget_tree.get_editor_property("root_widget"), widget_name)

        if target is None:
            all_widgets = widget_tree.get_editor_property("all_widgets") if hasattr(widget_tree, "get_editor_property") else []
            for w in (all_widgets or []):
                if w.get_name() == widget_name:
                    target = w
                    break

        if target is None:
            raise ValueError(f"Widget not found: {widget_name}")

        target.set_editor_property(property_name, value)

        return {
            "success": True,
            "widgetName": widget_name,
            "propertyName": property_name,
        }
    except ValueError:
        raise
    except Exception as e:
        raise RuntimeError(f"Failed to set widget property: {e}")


def list_widget_blueprints(params: dict) -> dict:
    """List Widget Blueprints in a directory."""
    directory = params.get("directory", "/Game/")
    recursive = params.get("recursive", True)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    widgets = []

    try:
        assets = unreal.EditorAssetLibrary.list_assets(directory, recursive=recursive)
        for asset_path in assets:
            try:
                asset_data = registry.get_asset_by_object_path(asset_path)
                if asset_data:
                    class_name = ""
                    try:
                        class_name = str(asset_data.get_editor_property("asset_class_path").get_editor_property("asset_name"))
                    except Exception:
                        try:
                            class_name = str(asset_data.asset_class)
                        except Exception:
                            pass

                    if "widgetblueprint" in class_name.lower():
                        widgets.append({
                            "path": str(asset_path),
                            "name": str(asset_path).split("/")[-1].split(".")[0],
                        })
            except Exception:
                continue
    except Exception as e:
        return {"error": str(e), "directory": directory}

    return {
        "directory": directory,
        "recursive": recursive,
        "count": len(widgets),
        "widgets": widgets,
    }


def read_widget_animations(params: dict) -> dict:
    """Read all UMG animations in a Widget Blueprint with their tracks and keyframes."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Widget Blueprint not found: {asset_path}")

    animations = []
    try:
        anims = asset.get_editor_property("animations") or []
        for anim in anims:
            anim_info = {
                "name": anim.get_name(),
            }
            try:
                anim_info["length"] = anim.get_editor_property("animation_length")
            except Exception:
                pass
            try:
                anim_info["isLooping"] = anim.get_editor_property("is_looping") if hasattr(anim, "get_editor_property") else None
            except Exception:
                pass

            bindings = []
            try:
                anim_bindings = anim.get_editor_property("animation_bindings") if hasattr(anim, "get_editor_property") else []
                for binding in (anim_bindings or []):
                    b_info = {}
                    try:
                        b_info["widgetName"] = str(binding.get_editor_property("widget_name"))
                    except Exception:
                        pass
                    try:
                        b_info["propertyName"] = str(binding.get_editor_property("property_name"))
                    except Exception:
                        pass
                    bindings.append(b_info)
            except Exception:
                pass
            anim_info["bindings"] = bindings

            animations.append(anim_info)
    except Exception as e:
        return {"assetPath": asset_path, "error": str(e)}

    return {
        "assetPath": asset_path,
        "animationCount": len(animations),
        "animations": animations,
    }


def _serialize_widget(widget, depth: int, max_depth: int) -> dict | None:
    """Recursively serialize a widget and its children."""
    if widget is None or depth > max_depth:
        return None

    info = {
        "name": widget.get_name(),
        "class": widget.get_class().get_name(),
    }

    try:
        info["isVariable"] = widget.get_editor_property("is_variable") if hasattr(widget, "get_editor_property") else None
    except Exception:
        pass

    try:
        visibility = widget.get_editor_property("visibility")
        info["visibility"] = str(visibility)
    except Exception:
        pass

    try:
        slot = widget.get_editor_property("slot")
        if slot:
            info["slotClass"] = slot.get_class().get_name()
    except Exception:
        pass

    children = []
    try:
        if hasattr(widget, "get_child_at"):
            child_count = widget.get_child_count() if hasattr(widget, "get_child_count") else 0
            for i in range(child_count):
                child = widget.get_child_at(i)
                child_data = _serialize_widget(child, depth + 1, max_depth)
                if child_data:
                    children.append(child_data)
        elif hasattr(widget, "get_content"):
            content = widget.get_content()
            if content:
                child_data = _serialize_widget(content, depth + 1, max_depth)
                if child_data:
                    children.append(child_data)
    except Exception:
        pass

    if children:
        info["children"] = children

    return info


def _find_widget_by_name(widget, name: str):
    """Recursively find a widget by name."""
    if widget is None:
        return None
    if widget.get_name() == name:
        return widget

    try:
        if hasattr(widget, "get_child_at"):
            child_count = widget.get_child_count() if hasattr(widget, "get_child_count") else 0
            for i in range(child_count):
                found = _find_widget_by_name(widget.get_child_at(i), name)
                if found:
                    return found
        elif hasattr(widget, "get_content"):
            return _find_widget_by_name(widget.get_content(), name)
    except Exception:
        pass

    return None


def _get_widget_properties(widget) -> dict:
    """Extract all readable properties from a widget."""
    result = {
        "name": widget.get_name(),
        "class": widget.get_class().get_name(),
    }

    try:
        result["visibility"] = str(widget.get_editor_property("visibility"))
    except Exception:
        pass

    try:
        result["isEnabled"] = widget.get_editor_property("is_enabled")
    except Exception:
        pass

    try:
        result["toolTipText"] = str(widget.get_editor_property("tool_tip_text"))
    except Exception:
        pass

    try:
        result["renderOpacity"] = widget.get_editor_property("render_opacity")
    except Exception:
        pass

    try:
        result["renderTransformPivot"] = str(widget.get_editor_property("render_transform_pivot"))
    except Exception:
        pass

    class_name = widget.get_class().get_name()

    if "TextBlock" in class_name:
        try:
            result["text"] = str(widget.get_editor_property("text"))
        except Exception:
            pass
        try:
            font = widget.get_editor_property("font")
            result["fontSize"] = font.get_editor_property("size") if font else None
        except Exception:
            pass
        try:
            color = widget.get_editor_property("color_and_opacity")
            result["color"] = {"r": color.r, "g": color.g, "b": color.b, "a": color.a} if color else None
        except Exception:
            pass

    elif "Image" in class_name:
        try:
            brush = widget.get_editor_property("brush")
            if brush:
                res = brush.get_editor_property("resource_object")
                result["texture"] = res.get_path_name() if res else None
                size = brush.get_editor_property("image_size")
                result["imageSize"] = {"x": size.x, "y": size.y} if size else None
        except Exception:
            pass
        try:
            tint = widget.get_editor_property("color_and_opacity")
            result["tint"] = {"r": tint.r, "g": tint.g, "b": tint.b, "a": tint.a} if tint else None
        except Exception:
            pass

    elif "Button" in class_name:
        try:
            style = widget.get_editor_property("widget_style")
            result["hasStyle"] = style is not None
        except Exception:
            pass

    elif "ProgressBar" in class_name:
        try:
            result["percent"] = widget.get_editor_property("percent")
        except Exception:
            pass
        try:
            fill_color = widget.get_editor_property("fill_color_and_opacity")
            result["fillColor"] = {"r": fill_color.r, "g": fill_color.g, "b": fill_color.b, "a": fill_color.a} if fill_color else None
        except Exception:
            pass

    properties = {}
    for prop_name in dir(widget):
        if prop_name.startswith("_"):
            continue
        try:
            val = getattr(widget, prop_name)
            if callable(val):
                continue
            if isinstance(val, (bool, int, float, str)):
                properties[prop_name] = val
        except Exception:
            continue
    result["allProperties"] = properties

    return result


HANDLERS = {
    "read_widget_tree": read_widget_tree,
    "get_widget_details": get_widget_details,
    "set_widget_property": set_widget_property,
    "list_widget_blueprints": list_widget_blueprints,
    "read_widget_animations": read_widget_animations,
}
