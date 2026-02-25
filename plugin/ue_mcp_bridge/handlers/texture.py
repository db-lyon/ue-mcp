"""Texture handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def list_textures(params: dict) -> dict:
    directory = params.get("directory", "/Game")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(directory, recursive=True)

    textures = []
    for ad in assets:
        class_name = str(ad.asset_class_path.asset_name)
        if class_name in ("Texture2D", "TextureCube", "TextureRenderTarget2D", "MediaTexture"):
            textures.append({
                "path": str(ad.package_name),
                "name": str(ad.asset_name),
                "type": class_name,
            })

    return {"directory": directory, "count": len(textures), "textures": textures}


def get_texture_info(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if tex is None:
        raise ValueError(f"Texture not found: {asset_path}")

    info = {
        "path": asset_path,
        "name": tex.get_name(),
        "className": tex.get_class().get_name(),
    }

    for prop in ["compression_settings", "lod_group", "srgb", "never_stream"]:
        try:
            info[prop] = str(tex.get_editor_property(prop))
        except Exception:
            pass

    return info


def set_texture_settings(params: dict) -> dict:
    asset_path = params.get("path", "")
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if tex is None:
        raise ValueError(f"Texture not found: {asset_path}")

    changes = []
    if "srgb" in params:
        tex.set_editor_property("srgb", bool(params["srgb"]))
        changes.append("srgb")
    if "neverStream" in params:
        tex.set_editor_property("never_stream", bool(params["neverStream"]))
        changes.append("neverStream")
    if "compressionSettings" in params:
        comp = params["compressionSettings"]
        comp_map = {
            "Default": unreal.TextureCompressionSettings.TC_DEFAULT,
            "Normalmap": unreal.TextureCompressionSettings.TC_NORMALMAP,
            "Masks": unreal.TextureCompressionSettings.TC_MASKS,
            "HDR": unreal.TextureCompressionSettings.TC_HDR,
            "VectorDisplacementmap": unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
        }
        if comp in comp_map:
            tex.set_editor_property("compression_settings", comp_map[comp])
            changes.append("compressionSettings")
    if "lodGroup" in params:
        lod = params["lodGroup"]
        lod_map = {
            "World": unreal.TextureGroup.TEXTUREGROUP_WORLD,
            "Character": unreal.TextureGroup.TEXTUREGROUP_CHARACTER,
            "UI": unreal.TextureGroup.TEXTUREGROUP_UI,
            "Lightmap": unreal.TextureGroup.TEXTUREGROUP_LIGHTMAP,
        }
        if lod in lod_map:
            tex.set_editor_property("lod_group", lod_map[lod])
            changes.append("lodGroup")

    return {"path": asset_path, "changes": changes, "success": True}


def import_texture(params: dict) -> dict:
    file_path = params.get("filePath", "")
    destination = params.get("destination", "/Game/Textures")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")

    import os
    if not os.path.isfile(file_path):
        raise ValueError(f"File not found: {file_path}")

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", file_path)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)

    asset_tools.import_asset_tasks([task])

    imported = task.get_editor_property("imported_object_paths")
    return {
        "filePath": file_path,
        "destination": destination,
        "importedPaths": [str(p) for p in imported] if imported else [],
        "success": True,
    }


HANDLERS = {
    "list_textures": list_textures,
    "get_texture_info": get_texture_info,
    "set_texture_settings": set_texture_settings,
    "import_texture": import_texture,
}
