"""
Landscape handlers — read terrain info, sample heightmaps, sculpt, paint layers.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def get_landscape_info(params: dict) -> dict:
    """Read the level's landscape setup."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    landscape = None
    for actor in all_actors:
        if "Landscape" in actor.get_class().get_name() and hasattr(actor, "get_editor_property"):
            landscape = actor
            break

    if landscape is None:
        raise ValueError("No landscape found in the current level")

    result = {
        "name": landscape.get_name(),
        "label": landscape.get_actor_label(),
        "class": landscape.get_class().get_name(),
    }

    location = landscape.get_actor_location()
    result["location"] = {"x": location.x, "y": location.y, "z": location.z}

    for prop in ["landscape_material", "component_size_quads", "subsection_size_quads",
                  "num_subsections", "static_lighting_lod", "streaming_distance_multiplier"]:
        try:
            val = landscape.get_editor_property(prop)
            if hasattr(val, "get_path_name"):
                result[prop] = val.get_path_name()
            elif isinstance(val, (bool, int, float, str)):
                result[prop] = val
            else:
                result[prop] = str(val)
        except Exception:
            pass

    try:
        comps = landscape.get_editor_property("landscape_components") if hasattr(landscape, "get_editor_property") else []
        result["componentCount"] = len(comps) if comps else 0
    except Exception:
        result["componentCount"] = None

    return result


def list_landscape_layers(params: dict) -> dict:
    """List all paint/weight layers on the landscape."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    landscape = _find_landscape()
    if landscape is None:
        raise ValueError("No landscape found in the current level")

    layers = []
    try:
        layer_infos = landscape.get_editor_property("editor_layer_settings") if hasattr(landscape, "get_editor_property") else []
        for layer_info in (layer_infos or []):
            info = {}
            try:
                li = layer_info.get_editor_property("layer_info_obj") if hasattr(layer_info, "get_editor_property") else layer_info
                if li:
                    info["name"] = li.get_name()
                    try:
                        info["layerName"] = str(li.get_editor_property("layer_name"))
                    except Exception:
                        pass
                    try:
                        pm = li.get_editor_property("phys_material")
                        info["physMaterial"] = pm.get_path_name() if pm else None
                    except Exception:
                        pass
                    try:
                        info["blendMode"] = str(li.get_editor_property("blend_mode"))
                    except Exception:
                        pass
            except Exception:
                info["raw"] = str(layer_info)
            layers.append(info)
    except Exception:
        try:
            target_layers = landscape.get_editor_property("target_layers") if hasattr(landscape, "get_editor_property") else []
            for tl in (target_layers or []):
                layers.append({"name": str(tl)})
        except Exception:
            pass

    return {
        "landscapeName": landscape.get_name(),
        "layerCount": len(layers),
        "layers": layers,
    }


def sample_landscape(params: dict) -> dict:
    """Sample landscape at world coordinates: height, normal, layer weights."""
    points = params.get("points", [])
    if not points:
        point = params.get("point", None)
        if point:
            points = [point]
        else:
            raise ValueError("Provide 'point' or 'points' parameter")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    results = []
    for pt in points:
        x = pt.get("x", 0)
        y = pt.get("y", 0)
        sample = {"x": x, "y": y}

        try:
            hit = unreal.SystemLibrary.line_trace_single(
                unreal.EditorLevelLibrary.get_editor_world(),
                unreal.Vector(x, y, 100000),
                unreal.Vector(x, y, -100000),
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                False, [], unreal.DrawDebugTrace.NONE,
                unreal.HitResult(), True
            )
            if hit:
                if hasattr(hit, "location"):
                    sample["height"] = hit.location.z if hasattr(hit.location, "z") else None
                if hasattr(hit, "normal"):
                    sample["normal"] = {"x": hit.normal.x, "y": hit.normal.y, "z": hit.normal.z}
                if hasattr(hit, "impact_point"):
                    sample["height"] = hit.impact_point.z
                if hasattr(hit, "impact_normal"):
                    sample["normal"] = {"x": hit.impact_normal.x, "y": hit.impact_normal.y, "z": hit.impact_normal.z}
        except Exception:
            try:
                loc = unreal.Vector(x, y, 0)
                if hasattr(unreal, "LandscapeEditorLibrary"):
                    height = unreal.LandscapeEditorLibrary.get_height_at_location(loc)
                    sample["height"] = height
            except Exception:
                sample["height"] = None

        results.append(sample)

    return {
        "sampleCount": len(results),
        "samples": results,
    }


def list_landscape_splines(params: dict) -> dict:
    """Read landscape spline data."""
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    landscape = _find_landscape()
    if landscape is None:
        raise ValueError("No landscape found in the current level")

    splines = []
    try:
        spline_comp = landscape.get_editor_property("spline_component") if hasattr(landscape, "get_editor_property") else None
        if spline_comp is None:
            all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
            for actor in all_actors:
                if "LandscapeSpline" in actor.get_class().get_name():
                    spline_comp = actor
                    break

        if spline_comp:
            try:
                control_points = spline_comp.get_editor_property("control_points") if hasattr(spline_comp, "get_editor_property") else []
                for cp in (control_points or []):
                    cp_info = {"name": cp.get_name()}
                    try:
                        loc = cp.get_editor_property("location")
                        cp_info["location"] = {"x": loc.x, "y": loc.y, "z": loc.z}
                    except Exception:
                        pass
                    splines.append(cp_info)
            except Exception:
                pass
    except Exception:
        pass

    return {
        "controlPointCount": len(splines),
        "controlPoints": splines,
    }


def get_landscape_component(params: dict) -> dict:
    """Inspect a specific landscape component."""
    section_x = params.get("sectionX", 0)
    section_y = params.get("sectionY", 0)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    landscape = _find_landscape()
    if landscape is None:
        raise ValueError("No landscape found in the current level")

    result = {
        "sectionX": section_x,
        "sectionY": section_y,
    }

    try:
        comps = landscape.get_editor_property("landscape_components") or []
        for comp in comps:
            try:
                cx = comp.get_editor_property("section_base_x")
                cy = comp.get_editor_property("section_base_y")
                if cx == section_x and cy == section_y:
                    result["found"] = True
                    result["componentName"] = comp.get_name()
                    for prop in ["simple_collision_mip_level", "collision_mip_level",
                                 "lod_bias", "static_lighting_resolution"]:
                        try:
                            result[prop] = comp.get_editor_property(prop)
                        except Exception:
                            pass
                    return result
            except Exception:
                continue
    except Exception:
        pass

    result["found"] = False
    return result


def sculpt_landscape(params: dict) -> dict:
    """Modify the landscape heightmap."""
    location = params.get("location", {})
    radius = params.get("radius", 500)
    strength = params.get("strength", 0.5)
    operation = params.get("operation", "raise")
    falloff = params.get("falloff", 0.5)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    x = location.get("x", 0)
    y = location.get("y", 0)

    if hasattr(unreal, "LandscapeEditorLibrary"):
        try:
            lib = unreal.LandscapeEditorLibrary
            loc = unreal.Vector(x, y, 0)
            if hasattr(lib, "sculpt"):
                lib.sculpt(loc, radius, strength, operation, falloff)
            return {"success": True, "operation": operation, "location": {"x": x, "y": y}, "radius": radius}
        except Exception as e:
            raise RuntimeError(f"Sculpt failed: {e}")

    cmd = f"Landscape.Sculpt X={x} Y={y} Radius={radius} Strength={strength} Op={operation}"
    world = unreal.EditorLevelLibrary.get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, cmd)

    return {
        "success": True,
        "note": "Executed via console command — verify visually",
        "operation": operation,
        "location": {"x": x, "y": y},
        "radius": radius,
        "strength": strength,
    }


def paint_landscape_layer(params: dict) -> dict:
    """Paint a weight layer on the landscape."""
    location = params.get("location", {})
    radius = params.get("radius", 500)
    layer_name = params.get("layerName", "")
    strength = params.get("strength", 1.0)
    falloff = params.get("falloff", 0.5)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    x = location.get("x", 0)
    y = location.get("y", 0)

    if hasattr(unreal, "LandscapeEditorLibrary"):
        try:
            lib = unreal.LandscapeEditorLibrary
            loc = unreal.Vector(x, y, 0)
            if hasattr(lib, "paint_layer"):
                lib.paint_layer(loc, radius, layer_name, strength, falloff)
            return {"success": True, "layerName": layer_name, "location": {"x": x, "y": y}, "radius": radius}
        except Exception as e:
            raise RuntimeError(f"Paint failed: {e}")

    return {
        "success": False,
        "error": "LandscapeEditorLibrary.paint_layer not available. Landscape painting may require editor mode APIs not exposed to Python.",
    }


def set_landscape_material(params: dict) -> dict:
    """Set the landscape material."""
    material_path = params.get("materialPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    landscape = _find_landscape()
    if landscape is None:
        raise ValueError("No landscape found in the current level")

    material = unreal.EditorAssetLibrary.load_asset(material_path)
    if material is None:
        raise ValueError(f"Material not found: {material_path}")

    try:
        landscape.set_editor_property("landscape_material", material)
        return {
            "success": True,
            "landscapeName": landscape.get_name(),
            "materialPath": material_path,
        }
    except Exception as e:
        raise RuntimeError(f"Failed to set landscape material: {e}")


def add_landscape_layer_info(params: dict) -> dict:
    """Register a new paint layer on the landscape."""
    layer_name = params.get("layerName", "")
    phys_material_path = params.get("physMaterialPath", None)

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    landscape = _find_landscape()
    if landscape is None:
        raise ValueError("No landscape found in the current level")

    try:
        if hasattr(unreal, "LandscapeLayerInfoObject"):
            asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
            layer_info = asset_tools.create_asset(
                f"LI_{layer_name}", "/Game/Landscape/LayerInfo",
                unreal.LandscapeLayerInfoObject, None)

            if layer_info:
                layer_info.set_editor_property("layer_name", layer_name)
                if phys_material_path:
                    pm = unreal.EditorAssetLibrary.load_asset(phys_material_path)
                    if pm:
                        layer_info.set_editor_property("phys_material", pm)

                return {
                    "success": True,
                    "layerName": layer_name,
                    "layerInfoPath": layer_info.get_path_name(),
                }
    except Exception as e:
        raise RuntimeError(f"Failed to add landscape layer: {e}")

    return {"success": False, "error": "LandscapeLayerInfoObject not available"}


def import_landscape_heightmap(params: dict) -> dict:
    """Import a heightmap from a file."""
    file_path = params.get("filePath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    landscape = _find_landscape()
    if landscape is None:
        raise ValueError("No landscape found in the current level")

    try:
        if hasattr(unreal, "LandscapeEditorLibrary") and hasattr(unreal.LandscapeEditorLibrary, "import_heightmap"):
            unreal.LandscapeEditorLibrary.import_heightmap(landscape, file_path)
            return {"success": True, "filePath": file_path}
    except Exception as e:
        raise RuntimeError(f"Heightmap import failed: {e}")

    return {
        "success": False,
        "error": "Heightmap import API not available. May require manual import through the editor.",
    }


def _find_landscape():
    """Find the landscape actor in the current level."""
    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    for actor in all_actors:
        class_name = actor.get_class().get_name()
        if class_name in ("Landscape", "LandscapeProxy", "LandscapeStreamingProxy") or "Landscape" in class_name:
            if hasattr(actor, "get_editor_property"):
                return actor
    return None


HANDLERS = {
    "get_landscape_info": get_landscape_info,
    "list_landscape_layers": list_landscape_layers,
    "sample_landscape": sample_landscape,
    "list_landscape_splines": list_landscape_splines,
    "get_landscape_component": get_landscape_component,
    "sculpt_landscape": sculpt_landscape,
    "paint_landscape_layer": paint_landscape_layer,
    "set_landscape_material": set_landscape_material,
    "add_landscape_layer_info": add_landscape_layer_info,
    "import_landscape_heightmap": import_landscape_heightmap,
}
