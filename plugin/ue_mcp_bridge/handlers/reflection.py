"""
Reflection handlers — expose UE's own type system to the MCP server.
This is the highest-leverage feature in the entire MCP: it turns every
class, struct, and enum in the running editor into on-demand context.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def reflect_class(params: dict) -> dict:
    """Reflect a UClass: parent chain, properties, functions, specifiers, interfaces."""
    class_name = params.get("className", "")
    include_inherited = params.get("includeInherited", False)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    cls = _find_class(class_name)
    if cls is None:
        raise ValueError(f"Class not found: {class_name}")

    parent = cls.get_super_class() if hasattr(cls, "get_super_class") else None
    parent_chain = []
    p = parent
    while p is not None:
        parent_chain.append(p.get_name())
        p = p.get_super_class() if hasattr(p, "get_super_class") else None

    properties = _get_class_properties(cls, include_inherited)
    functions = _get_class_functions(cls, include_inherited)

    result = {
        "className": cls.get_name(),
        "parentClass": parent.get_name() if parent else None,
        "parentChain": parent_chain,
        "isAbstract": cls.has_any_class_flags(unreal.ClassFlags.CLASS_ABSTRACT) if hasattr(unreal, "ClassFlags") else None,
        "propertyCount": len(properties),
        "functionCount": len(functions),
        "properties": properties,
        "functions": functions,
    }

    return result


def reflect_struct(params: dict) -> dict:
    """Reflect a UScriptStruct: fields and parent."""
    struct_name = params.get("structName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    struct = None
    if hasattr(unreal, "find_struct"):
        struct = unreal.find_struct(struct_name)

    if struct is None:
        short_name = struct_name.split(".")[-1] if "." in struct_name else struct_name
        for prefix in ["", "F"]:
            candidate = getattr(unreal, f"{prefix}{short_name}", None)
            if candidate is not None:
                struct = candidate
                break

    if struct is None:
        raise ValueError(f"Struct not found: {struct_name}. Try the full path e.g. '/Script/Engine.HitResult' or short name like 'Vector'")

    fields = []
    target = struct
    if hasattr(struct, "static_struct"):
        target = struct.static_struct()
    for prop_name in dir(target):
        if prop_name.startswith("_"):
            continue
        try:
            attr = getattr(target, prop_name, None)
            if callable(attr):
                continue
            fields.append({
                "name": prop_name,
                "type": type(attr).__name__ if attr is not None else "unknown",
            })
        except Exception:
            continue

    return {
        "structName": struct_name,
        "fieldCount": len(fields),
        "fields": fields,
    }


def reflect_enum(params: dict) -> dict:
    """Reflect a UEnum: all values with display names."""
    enum_name = params.get("enumName", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    try:
        enum_class = _find_enum(enum_name)
        if enum_class is None:
            raise ValueError(
                f"Enum not found: {enum_name}. "
                "Try the Python name e.g. 'CollisionChannel' or 'ECollisionChannel'"
            )

        values = []
        for attr_name in dir(enum_class):
            if attr_name.startswith("_"):
                continue
            attr = getattr(enum_class, attr_name, None)
            if isinstance(attr, enum_class):
                values.append({
                    "name": attr_name,
                    "value": int(attr) if hasattr(attr, "__int__") else str(attr),
                    "displayName": attr_name.replace("_", " ").title(),
                })

        return {
            "enumName": enum_name,
            "valueCount": len(values),
            "values": values,
        }
    except Exception as e:
        raise RuntimeError(f"Failed to reflect enum '{enum_name}': {e}")


def _find_enum(enum_name: str):
    """Find a UEnum by name, trying multiple resolution strategies."""
    direct = getattr(unreal, enum_name, None)
    if direct is not None and _looks_like_enum(direct):
        return direct

    short = enum_name.split(".")[-1] if "." in enum_name else enum_name
    for prefix in ["", "E"]:
        candidate = getattr(unreal, f"{prefix}{short}", None)
        if candidate is not None and _looks_like_enum(candidate):
            return candidate

    without_e = short[1:] if short.startswith("E") and len(short) > 1 and short[1].isupper() else short
    if without_e != short:
        candidate = getattr(unreal, without_e, None)
        if candidate is not None and _looks_like_enum(candidate):
            return candidate

    return None


def _looks_like_enum(obj):
    """Heuristic check if an object is a UE enum type."""
    if isinstance(obj, type):
        for attr_name in dir(obj):
            if not attr_name.startswith("_"):
                attr = getattr(obj, attr_name, None)
                if isinstance(attr, obj):
                    return True
    return False


def list_classes(params: dict) -> dict:
    """List classes, optionally filtered by parent class."""
    parent_filter = params.get("parentFilter", None)
    limit = params.get("limit", 100)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if parent_filter:
        parent_cls = _find_class(parent_filter)
        if parent_cls is None:
            raise ValueError(f"Parent class not found: {parent_filter}")

        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        derived = unreal.EditorAssetLibrary
        all_classes = []

        try:
            objs = unreal.EditorUtilityLibrary.get_all_blueprint_classes() if hasattr(unreal, "EditorUtilityLibrary") else []
        except Exception:
            objs = []

        result_classes = []
        for cls_obj in objs:
            try:
                if cls_obj.is_child_of(parent_cls):
                    result_classes.append(cls_obj.get_name())
            except Exception:
                continue

        return {
            "parentFilter": parent_filter,
            "count": min(len(result_classes), limit),
            "classes": result_classes[:limit],
        }
    else:
        common_classes = [
            "Actor", "Pawn", "Character", "PlayerController", "GameModeBase",
            "GameStateBase", "PlayerState", "HUD", "ActorComponent",
            "SceneComponent", "PrimitiveComponent", "StaticMeshComponent",
            "SkeletalMeshComponent", "CameraComponent", "AudioComponent",
            "LightComponent", "UserWidget", "AnimInstance",
            "GameInstance", "SaveGame", "DataAsset", "PrimaryDataAsset",
            "BlueprintFunctionLibrary", "DeveloperSettings",
            "CheatManager", "WorldSubsystem", "GameInstanceSubsystem",
            "LocalPlayerSubsystem",
        ]

        results = []
        for name in common_classes:
            cls = _find_class(name)
            if cls:
                parent = cls.get_super_class()
                results.append({
                    "name": cls.get_name(),
                    "parent": parent.get_name() if parent else None,
                })

        return {
            "note": "Showing common base classes. Use parentFilter to find derived classes.",
            "count": len(results),
            "classes": results,
        }


def list_gameplay_tags(params: dict) -> dict:
    """Get the full gameplay tag hierarchy from the running editor."""
    filter_prefix = params.get("filter", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    try:
        manager = unreal.GameplayTagsManager.get()
        if hasattr(manager, "request_all_gameplay_tags"):
            container = manager.request_all_gameplay_tags()
            tags = container.get_gameplay_tag_array() if hasattr(container, "get_gameplay_tag_array") else []

            tag_strings = []
            for tag in tags:
                tag_str = str(tag.tag_name) if hasattr(tag, "tag_name") else str(tag)
                if filter_prefix and not tag_str.startswith(filter_prefix):
                    continue
                tag_strings.append(tag_str)

            tag_strings.sort()
            return {
                "filter": filter_prefix or "(all)",
                "count": len(tag_strings),
                "tags": tag_strings,
            }
    except Exception:
        pass

    return {
        "error": "GameplayTagsManager not accessible. Tags may not be initialized.",
        "count": 0,
        "tags": [],
    }


def _find_class(class_name: str):
    """Find a UClass by name, trying multiple resolution strategies."""
    if not HAS_UNREAL:
        return None

    cls = unreal.find_class(class_name) if hasattr(unreal, "find_class") else None
    if cls:
        return cls

    cls = getattr(unreal, class_name, None)
    if cls and hasattr(cls, "static_class"):
        return cls.static_class()

    for prefix in ["", "A", "U", "F"]:
        candidate = getattr(unreal, f"{prefix}{class_name}", None)
        if candidate and hasattr(candidate, "static_class"):
            return candidate.static_class()

    return None


def _get_class_properties(cls, include_inherited: bool) -> list[dict]:
    """Extract properties from a UClass using editor reflection."""
    properties = []

    try:
        cdo = unreal.get_default_object(cls) if hasattr(unreal, "get_default_object") else None
        if cdo is None:
            return properties

        for prop_name in dir(cdo):
            if prop_name.startswith("_"):
                continue
            try:
                val = getattr(cdo, prop_name)
                if callable(val):
                    continue
                properties.append({
                    "name": prop_name,
                    "type": type(val).__name__,
                    "value": _serialize_value(val),
                })
            except Exception:
                continue
    except Exception:
        pass

    return properties


def _get_class_functions(cls, include_inherited: bool) -> list[dict]:
    """Extract function signatures from a UClass."""
    functions = []

    try:
        target_class = getattr(unreal, cls.get_name(), None)
        if target_class is None:
            return functions

        for name in dir(target_class):
            if name.startswith("_"):
                continue
            attr = getattr(target_class, name, None)
            if callable(attr) and not isinstance(attr, type):
                doc = getattr(attr, "__doc__", "") or ""
                functions.append({
                    "name": name,
                    "doc": doc[:200] if doc else None,
                })
    except Exception:
        pass

    return functions


def _serialize_value(val):
    """Convert UE values to JSON-serializable types."""
    if val is None:
        return None
    if isinstance(val, (bool, int, float, str)):
        return val
    if isinstance(val, (list, tuple)):
        return [_serialize_value(v) for v in val[:10]]

    if HAS_UNREAL:
        if isinstance(val, unreal.Vector):
            return {"x": val.x, "y": val.y, "z": val.z}
        if isinstance(val, unreal.Rotator):
            return {"pitch": val.pitch, "yaw": val.yaw, "roll": val.roll}
        if isinstance(val, unreal.LinearColor):
            return {"r": val.r, "g": val.g, "b": val.b, "a": val.a}

    return str(val)


def create_gameplay_tag(params: dict) -> dict:
    """Add a new gameplay tag to the project's tag list."""
    tag = params.get("tag", "")
    comment = params.get("comment", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not tag:
        raise ValueError("tag is required (e.g. 'Combat.Damage.Fire')")

    if hasattr(unreal, "GameplayTagsManager"):
        mgr = unreal.GameplayTagsManager.get()
        if hasattr(mgr, "add_native_gameplay_tag"):
            mgr.add_native_gameplay_tag(tag, comment)
            return {"tag": tag, "comment": comment, "success": True, "method": "add_native_gameplay_tag"}

    settings = None
    if hasattr(unreal, "GameplayTagsSettings"):
        settings = unreal.get_default_object(unreal.GameplayTagsSettings)
    if settings is None:
        try:
            settings = unreal.EditorAssetLibrary.load_asset("/Script/GameplayTags.GameplayTagsSettings")
        except Exception:
            pass

    if settings is not None and hasattr(settings, "get_editor_property"):
        try:
            tag_list = settings.get_editor_property("gameplay_tag_list")
            if hasattr(tag_list, "append"):
                tag_list.append(tag)
                settings.set_editor_property("gameplay_tag_list", tag_list)
                return {"tag": tag, "success": True, "method": "GameplayTagsSettings"}
        except Exception:
            pass

    try:
        import os
        project_dir = os.environ.get("UE_PROJECT_DIR", "")
        if not project_dir:
            project_dir = str(unreal.Paths.project_dir()) if hasattr(unreal, "Paths") else ""
        if project_dir:
            tag_file = os.path.join(project_dir, "Config", "DefaultGameplayTags.ini")
            section = "[/Script/GameplayTags.GameplayTagsSettings]"
            entry = f'+GameplayTagList=(Tag="{tag}",DevComment="{comment}")'
            if os.path.exists(tag_file):
                with open(tag_file, "r", encoding="utf-8") as f:
                    content = f.read()
                if section not in content:
                    content += f"\n\n{section}\n{entry}\n"
                elif entry not in content:
                    content = content.replace(section, f"{section}\n{entry}")
                with open(tag_file, "w", encoding="utf-8") as f:
                    f.write(content)
            else:
                with open(tag_file, "w", encoding="utf-8") as f:
                    f.write(f"{section}\n{entry}\n")
            return {"tag": tag, "success": True, "method": "ini_append", "note": "Restart editor to pick up new tag"}
    except Exception:
        pass

    raise RuntimeError("Could not add gameplay tag via available APIs")


HANDLERS = {
    "reflect_class": reflect_class,
    "reflect_struct": reflect_struct,
    "reflect_enum": reflect_enum,
    "list_classes": list_classes,
    "list_gameplay_tags": list_gameplay_tags,
    "create_gameplay_tag": create_gameplay_tag,
}
