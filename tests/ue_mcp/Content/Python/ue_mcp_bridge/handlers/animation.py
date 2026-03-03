"""
Animation handlers — read and manipulate anim montages, anim Blueprints,
state machines, blendspaces, and notifies.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def read_anim_blueprint(params: dict) -> dict:
    """Read an Animation Blueprint's structure: state machines, variables, groups, slots."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Anim Blueprint not found: {asset_path}")

    class_name = asset.get_class().get_name()
    result = {
        "assetPath": asset_path,
        "class": class_name,
        "name": asset.get_name(),
    }

    try:
        skeleton = asset.get_editor_property("target_skeleton")
        result["targetSkeleton"] = skeleton.get_path_name() if skeleton else None
    except Exception:
        result["targetSkeleton"] = None

    try:
        parent = asset.get_editor_property("parent_class")
        result["parentClass"] = parent.get_name() if parent else None
    except Exception:
        result["parentClass"] = None

    groups = []
    try:
        anim_groups = asset.get_editor_property("groups") if hasattr(asset, "get_editor_property") else []
        for group in (anim_groups or []):
            groups.append(str(group))
    except Exception:
        pass
    result["groups"] = groups

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


def read_anim_montage(params: dict) -> dict:
    """Read an Animation Montage: sections, notifies, slot, blend in/out, sequences."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Anim Montage not found: {asset_path}")

    result = {
        "assetPath": asset_path,
        "class": asset.get_class().get_name(),
        "name": asset.get_name(),
    }

    try:
        result["blendIn"] = asset.get_editor_property("blend_in").get_editor_property("blend_time")
    except Exception:
        result["blendIn"] = None

    try:
        result["blendOut"] = asset.get_editor_property("blend_out").get_editor_property("blend_time")
    except Exception:
        result["blendOut"] = None

    try:
        result["sequenceLength"] = asset.get_editor_property("sequence_length")
    except Exception:
        result["sequenceLength"] = None

    try:
        result["rateScale"] = asset.get_editor_property("rate_scale")
    except Exception:
        result["rateScale"] = None

    sections = []
    try:
        composite_sections = asset.get_editor_property("composite_sections") or []
        for section in composite_sections:
            sec_info = {
                "name": str(section.get_editor_property("section_name")),
            }
            try:
                sec_info["startTime"] = section.get_editor_property("start_time") if hasattr(section, "get_editor_property") else None
            except Exception:
                pass
            try:
                next_sec = section.get_editor_property("next_section_name")
                sec_info["nextSection"] = str(next_sec) if next_sec else None
            except Exception:
                pass
            sections.append(sec_info)
    except Exception:
        pass
    result["sections"] = sections

    notifies = []
    try:
        anim_notifies = asset.get_editor_property("notifies") or []
        for notify in anim_notifies:
            notify_info = {}
            try:
                notify_info["name"] = str(notify.get_editor_property("notify_name"))
            except Exception:
                notify_info["name"] = "Unknown"
            try:
                notify_info["triggerTime"] = notify.get_editor_property("trigger_time_offset")
            except Exception:
                pass
            try:
                notify_info["duration"] = notify.get_editor_property("duration")
            except Exception:
                pass
            try:
                n = notify.get_editor_property("notify")
                if n:
                    notify_info["class"] = n.get_class().get_name()
            except Exception:
                pass
            notifies.append(notify_info)
    except Exception:
        pass
    result["notifies"] = notifies

    slot_anims = []
    try:
        slot_anim_tracks = asset.get_editor_property("slot_anim_tracks") or []
        for track in slot_anim_tracks:
            track_info = {}
            try:
                track_info["slotName"] = str(track.get_editor_property("slot_name"))
            except Exception:
                pass
            try:
                anim_segs = track.get_editor_property("anim_track").get_editor_property("anim_segments") or []
                segments = []
                for seg in anim_segs:
                    seg_info = {}
                    try:
                        ref = seg.get_editor_property("anim_reference")
                        seg_info["animation"] = ref.get_path_name() if ref else None
                    except Exception:
                        pass
                    try:
                        seg_info["startPos"] = seg.get_editor_property("anim_start_time")
                    except Exception:
                        pass
                    try:
                        seg_info["endPos"] = seg.get_editor_property("anim_end_time")
                    except Exception:
                        pass
                    segments.append(seg_info)
                track_info["segments"] = segments
            except Exception:
                track_info["segments"] = []
            slot_anims.append(track_info)
    except Exception:
        pass
    result["slotAnimTracks"] = slot_anims

    return result


def read_anim_sequence(params: dict) -> dict:
    """Read an Animation Sequence: length, frame count, rate, notifies, curves."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Anim Sequence not found: {asset_path}")

    result = {
        "assetPath": asset_path,
        "class": asset.get_class().get_name(),
        "name": asset.get_name(),
    }

    try:
        result["sequenceLength"] = asset.get_editor_property("sequence_length")
    except Exception:
        pass

    try:
        result["rateScale"] = asset.get_editor_property("rate_scale")
    except Exception:
        pass

    try:
        result["numberOfFrames"] = asset.get_editor_property("number_of_frames")
    except Exception:
        pass

    try:
        skeleton = asset.get_editor_property("skeleton")
        result["skeleton"] = skeleton.get_path_name() if skeleton else None
    except Exception:
        pass

    try:
        result["isAdditive"] = asset.get_editor_property("additive_anim_type") != 0
    except Exception:
        pass

    notifies = []
    try:
        anim_notifies = asset.get_editor_property("notifies") or []
        for notify in anim_notifies:
            n_info = {}
            try:
                n_info["name"] = str(notify.get_editor_property("notify_name"))
            except Exception:
                pass
            try:
                n_info["triggerTime"] = notify.get_editor_property("trigger_time_offset")
            except Exception:
                pass
            try:
                n = notify.get_editor_property("notify")
                if n:
                    n_info["class"] = n.get_class().get_name()
            except Exception:
                pass
            notifies.append(n_info)
    except Exception:
        pass
    result["notifies"] = notifies

    curves = []
    try:
        if hasattr(unreal, "AnimationLibrary"):
            curve_names = unreal.AnimationLibrary.get_animation_curve_names(asset)
            for name in (curve_names or []):
                curves.append(str(name))
    except Exception:
        pass
    result["curveNames"] = curves

    return result


def read_blendspace(params: dict) -> dict:
    """Read a BlendSpace: axes, sample points, grid dimensions."""
    asset_path = params.get("assetPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"BlendSpace not found: {asset_path}")

    result = {
        "assetPath": asset_path,
        "class": asset.get_class().get_name(),
        "name": asset.get_name(),
    }

    for axis_name, prop_prefix in [("horizontal", "axis_to_scale_animation"), ("vertical", "")]:
        try:
            if axis_name == "horizontal":
                result["horizontalAxisName"] = str(asset.get_editor_property("horizontal_axis_name")) if hasattr(asset, "get_editor_property") else None
            elif axis_name == "vertical":
                result["verticalAxisName"] = str(asset.get_editor_property("vertical_axis_name")) if hasattr(asset, "get_editor_property") else None
        except Exception:
            pass

    samples = []
    try:
        blend_samples = asset.get_editor_property("sample_data") if hasattr(asset, "get_editor_property") else []
        for sample in (blend_samples or []):
            s_info = {}
            try:
                anim = sample.get_editor_property("animation")
                s_info["animation"] = anim.get_path_name() if anim else None
            except Exception:
                pass
            try:
                val = sample.get_editor_property("sample_value")
                s_info["value"] = {"x": val.x, "y": val.y} if hasattr(val, "x") else str(val)
            except Exception:
                pass
            try:
                s_info["rateScale"] = sample.get_editor_property("rate_scale")
            except Exception:
                pass
            samples.append(s_info)
    except Exception:
        pass
    result["samples"] = samples

    try:
        skeleton = asset.get_editor_property("skeleton")
        result["skeleton"] = skeleton.get_path_name() if skeleton else None
    except Exception:
        pass

    return result


def list_anim_assets(params: dict) -> dict:
    """List animation assets in a directory: montages, sequences, blendspaces, anim BPs."""
    directory = params.get("directory", "/Game/")
    recursive = params.get("recursive", True)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()

    anim_types = {
        "AnimMontage": [],
        "AnimSequence": [],
        "BlendSpace": [],
        "BlendSpace1D": [],
        "AnimBlueprint": [],
        "AimOffsetBlendSpace": [],
    }

    try:
        if recursive:
            assets = unreal.EditorAssetLibrary.list_assets(directory, recursive=True)
        else:
            assets = unreal.EditorAssetLibrary.list_assets(directory, recursive=False)

        for asset_path in assets:
            try:
                asset_data = registry.get_asset_by_object_path(asset_path)
                if asset_data:
                    class_name = str(asset_data.asset_class) if hasattr(asset_data, "asset_class") else ""
                    if not class_name:
                        class_name = str(asset_data.get_editor_property("asset_class_path").get_editor_property("asset_name")) if hasattr(asset_data, "get_editor_property") else ""

                    for anim_type in anim_types:
                        if anim_type.lower() in class_name.lower():
                            anim_types[anim_type].append({
                                "path": str(asset_path),
                                "name": str(asset_path).split("/")[-1].split(".")[0],
                            })
                            break
            except Exception:
                continue
    except Exception as e:
        return {"error": str(e), "directory": directory}

    total = sum(len(v) for v in anim_types.values())
    return {
        "directory": directory,
        "recursive": recursive,
        "totalAnimAssets": total,
        "montages": anim_types["AnimMontage"],
        "sequences": anim_types["AnimSequence"],
        "blendSpaces": anim_types["BlendSpace"] + anim_types["BlendSpace1D"] + anim_types["AimOffsetBlendSpace"],
        "animBlueprints": anim_types["AnimBlueprint"],
    }


def add_anim_notify(params: dict) -> dict:
    """Add a notify to an animation montage or sequence at a specific time."""
    asset_path = params.get("assetPath", "")
    notify_name = params.get("notifyName", "")
    trigger_time = params.get("triggerTime", 0.0)
    notify_class = params.get("notifyClass", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise ValueError(f"Animation asset not found: {asset_path}")

    try:
        if hasattr(unreal, "AnimationLibrary"):
            lib = unreal.AnimationLibrary

            if notify_class:
                cls = getattr(unreal, notify_class, None)
                if cls is None:
                    raise ValueError(f"Notify class not found: {notify_class}")
                lib.add_animation_notify_event(asset, notify_name, trigger_time, cls)
            else:
                lib.add_animation_notify_event(asset, notify_name, trigger_time)

            return {
                "success": True,
                "assetPath": asset_path,
                "notifyName": notify_name,
                "triggerTime": trigger_time,
            }
        else:
            raise RuntimeError("AnimationLibrary not available")
    except Exception as e:
        raise RuntimeError(f"Failed to add notify: {e}")


def create_anim_montage(params: dict) -> dict:
    """Create an Animation Montage from an existing AnimSequence."""
    from ._util import resolve_asset_path, ensure_asset_cleared
    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Animations")
    if not asset_name:
        asset_name = params.get("name", "AM_NewMontage")
        package_path = params.get("packagePath", "/Game/Animations")
        full_path = f"{package_path}/{asset_name}"
    anim_sequence_path = params.get("animSequencePath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    ensure_asset_cleared(full_path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    if not anim_sequence_path:
        raise ValueError("animSequencePath is required — montages are built from an AnimSequence")

    source = unreal.EditorAssetLibrary.load_asset(anim_sequence_path)
    if source is None:
        raise ValueError(f"AnimSequence not found: {anim_sequence_path}")

    factory = None
    if hasattr(unreal, "AnimMontageFactory"):
        factory = unreal.AnimMontageFactory()
        try:
            factory.set_editor_property("target_skeleton", source.get_editor_property("skeleton"))
        except Exception:
            pass
        try:
            factory.set_editor_property("source_animation", source)
        except Exception:
            pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        asset = tools.create_asset(asset_name, package_path, unreal.AnimMontage, None)

    if asset is None:
        raise RuntimeError(f"Failed to create AnimMontage at {package_path}/{asset_name}")

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


def create_anim_blueprint(params: dict) -> dict:
    """Create an Animation Blueprint for a given skeleton."""
    from ._util import resolve_asset_path, ensure_asset_cleared
    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Animations")
    if not asset_name:
        asset_name = params.get("name", "ABP_New")
        package_path = params.get("packagePath", "/Game/Animations")
        full_path = f"{package_path}/{asset_name}"
    skeleton_path = params.get("skeletonPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not skeleton_path:
        raise ValueError("skeletonPath is required")

    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    if skeleton is None:
        raise ValueError(f"Skeleton not found: {skeleton_path}")

    ensure_asset_cleared(full_path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "AnimBlueprintFactory"):
        factory = unreal.AnimBlueprintFactory()
        try:
            factory.set_editor_property("target_skeleton", skeleton)
        except Exception:
            pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        raise RuntimeError("AnimBlueprintFactory not available in this UE version")

    if asset is None:
        raise RuntimeError(f"Failed to create AnimBlueprint at {package_path}/{asset_name}")

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
    }


def create_blendspace(params: dict) -> dict:
    """Create a BlendSpace asset for a given skeleton."""
    from ._util import resolve_asset_path, ensure_asset_cleared
    asset_name, package_path, full_path = resolve_asset_path(params, "/Game/Animations")
    if not asset_name:
        asset_name = params.get("name", "BS_New")
        package_path = params.get("packagePath", "/Game/Animations")
        full_path = f"{package_path}/{asset_name}"
    skeleton_path = params.get("skeletonPath", "")
    axis_horizontal = params.get("axisHorizontal", "Speed")
    axis_vertical = params.get("axisVertical", "Direction")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    if not skeleton_path:
        raise ValueError("skeletonPath is required")

    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    if skeleton is None:
        raise ValueError(f"Skeleton not found: {skeleton_path}")

    ensure_asset_cleared(full_path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    factory = None
    if hasattr(unreal, "BlendSpaceFactoryNew"):
        factory = unreal.BlendSpaceFactoryNew()
        try:
            factory.set_editor_property("target_skeleton", skeleton)
        except Exception:
            pass
    elif hasattr(unreal, "BlendSpaceFactory"):
        factory = unreal.BlendSpaceFactory()
        try:
            factory.set_editor_property("target_skeleton", skeleton)
        except Exception:
            pass

    if factory:
        asset = tools.create_asset(asset_name, package_path, None, factory)
    else:
        raise RuntimeError("BlendSpaceFactory not available in this UE version")

    if asset is None:
        raise RuntimeError(f"Failed to create BlendSpace at {package_path}/{asset_name}")

    try:
        asset.set_editor_property("horizontal_axis_name", axis_horizontal)
        asset.set_editor_property("vertical_axis_name", axis_vertical)
    except Exception:
        pass

    unreal.EditorAssetLibrary.save_asset(f"{package_path}/{asset_name}")
    return {
        "path": f"{package_path}/{asset_name}",
        "name": asset.get_name(),
        "class": asset.get_class().get_name(),
        "axisHorizontal": axis_horizontal,
        "axisVertical": axis_vertical,
    }


HANDLERS = {
    "read_anim_blueprint": read_anim_blueprint,
    "read_anim_montage": read_anim_montage,
    "read_anim_sequence": read_anim_sequence,
    "read_blendspace": read_blendspace,
    "list_anim_assets": list_anim_assets,
    "add_anim_notify": add_anim_notify,
    "create_anim_montage": create_anim_montage,
    "create_anim_blueprint": create_anim_blueprint,
    "create_blendspace": create_blendspace,
}
