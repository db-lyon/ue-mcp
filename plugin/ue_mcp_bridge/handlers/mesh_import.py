"""Mesh import — import FBX/OBJ as StaticMesh or SkeletalMesh."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


def _run_import(file_path, dest_path, dest_name, as_skeletal=False, skeleton_path=""):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", file_path)
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)

    if file_path.lower().endswith(".fbx"):
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_animations", False)
        options.set_editor_property("import_materials", True)
        options.set_editor_property("import_textures", True)

        if as_skeletal:
            options.set_editor_property("import_as_skeletal", True)
            options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
            if skeleton_path:
                skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
                if skeleton:
                    options.set_editor_property("skeleton", skeleton)
        else:
            options.set_editor_property("import_as_skeletal", False)
            options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)

        task.set_editor_property("options", options)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = task.get_editor_property("imported_object_paths")
    if not imported or len(imported) == 0:
        raise RuntimeError(f"Import returned no assets for {file_path}")

    return {
        "filePath": file_path,
        "importedAssets": [str(p) for p in imported],
        "destinationPath": dest_path,
        "success": True,
    }


def import_static_mesh(params: dict) -> dict:
    file_path = params.get("filePath", "")
    dest_path = params.get("destinationPath", "/Game/Meshes")
    dest_name = params.get("name", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not file_path:
        raise ValueError("filePath is required (absolute path to FBX/OBJ file)")

    if not dest_name:
        import os
        dest_name = os.path.splitext(os.path.basename(file_path))[0]

    return _run_import(file_path, dest_path, dest_name, as_skeletal=False)


def import_skeletal_mesh(params: dict) -> dict:
    file_path = params.get("filePath", "")
    dest_path = params.get("destinationPath", "/Game/Meshes")
    dest_name = params.get("name", "")
    skeleton_path = params.get("skeletonPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not file_path:
        raise ValueError("filePath is required (absolute path to FBX file)")

    if not dest_name:
        import os
        dest_name = os.path.splitext(os.path.basename(file_path))[0]

    return _run_import(file_path, dest_path, dest_name, as_skeletal=True, skeleton_path=skeleton_path)


def import_animation(params: dict) -> dict:
    """Import an animation from FBX, optionally targeting an existing skeleton."""
    file_path = params.get("filePath", "")
    dest_path = params.get("destinationPath", "/Game/Animations")
    dest_name = params.get("name", "")
    skeleton_path = params.get("skeletonPath", "")

    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")
    if not file_path:
        raise ValueError("filePath is required")

    if not dest_name:
        import os
        dest_name = os.path.splitext(os.path.basename(file_path))[0]

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", file_path)
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)

    if file_path.lower().endswith(".fbx"):
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", False)
        options.set_editor_property("import_animations", True)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)

        if skeleton_path:
            skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
            if skeleton:
                options.set_editor_property("skeleton", skeleton)

        task.set_editor_property("options", options)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = task.get_editor_property("imported_object_paths")
    if not imported or len(imported) == 0:
        raise RuntimeError(f"Import returned no assets for {file_path}")

    return {
        "filePath": file_path,
        "importedAssets": [str(p) for p in imported],
        "destinationPath": dest_path,
        "success": True,
    }


HANDLERS = {
    "import_static_mesh": import_static_mesh,
    "import_skeletal_mesh": import_skeletal_mesh,
    "import_animation": import_animation,
}
