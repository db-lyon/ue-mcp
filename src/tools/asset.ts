import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3, Rotator } from "../schemas.js";

export const assetTool: ToolDef = categoryTool(
  "asset",
  "Asset management: list, search, read, CRUD, import meshes/textures, datatables.",
  {
    list: bp(
      "List assets via the AssetRegistry (sees /Game and every mounted plugin root). Params: directory? (default /Game), classFilter?, recursive? (default true), maxResults? (default 2000)",
      "list_assets",
      (p) => ({ directory: p.directory, classFilter: p.classFilter ?? p.typeFilter, recursive: p.recursive, maxResults: p.maxResults }),
    ),
    search: {
      description: "Search by name/class/path. Params: query, directory?, maxResults?, searchAll?",
      handler: async (ctx, p) => {
        const { action: _, ...rest } = p;
        const roots = ctx.project.config.contentRoots;
        // If no directory specified and contentRoots configured, search each root and merge
        if (!p.directory && roots && roots.length > 0) {
          const maxResults = (p.maxResults as number) ?? 50;
          const allResults: Array<Record<string, unknown>> = [];
          for (const root of roots) {
            const res = await ctx.bridge.call("search_assets", { ...rest, directory: root }) as Record<string, unknown>;
            if (res.results && Array.isArray(res.results)) {
              allResults.push(...(res.results as Array<Record<string, unknown>>));
            }
            if (allResults.length >= maxResults) break;
          }
          return {
            query: p.query ?? "",
            searchScope: roots,
            resultCount: Math.min(allResults.length, maxResults),
            results: allResults.slice(0, maxResults),
            success: true,
          };
        }
        return ctx.bridge.call("search_assets", rest);
      },
    },
    read:           bp("Read asset via reflection. Params: assetPath", "read_asset", (p) => ({ path: p.assetPath })),
    read_properties: bp("Read asset properties with values. Params: assetPath, propertyName?, includeValues?", "read_asset_properties"),
    duplicate:      bp("Duplicate asset. Params: sourcePath, destinationPath", "duplicate_asset"),
    rename:         bp("Rename asset. Params: assetPath, newName (or sourcePath, destinationPath), force?. World Partition levels are detected and their __ExternalActors__/__ExternalObjects__ packages migrate atomically alongside the .umap, source-side redirectors get fixed up, and the active editor world is swapped to blank if it matches the source (#409). Refuses if any package is dirty - save first. If a prior rename left externals orphaned at the old path, re-running reconciles them. Rollback descriptor is emitted even on partial failure so the inverse rename can recover. `force=true` lets the call merge into a destination with pre-existing externals (used by rollback). For batches of 3+ scene-referenced non-world assets use bulk_rename instead.", "rename_asset"),
    bulk_rename:    bp("Batched rename using IAssetTools::RenameAssets - single transaction with one redirector-fixup pass (matches Content Browser drag). Use this over looped rename for scene-referenced assets. World assets are rejected (status=rejected_world); use rename_asset which handles WP externals atomically (#409). Params: renames[] where each entry is {sourcePath, destinationPath} OR {assetPath, newName}.", "bulk_rename_assets", (p) => ({ renames: p.renames })),
    move:           bp("Move asset. Params: sourcePath, destinationPath", "move_asset"),
    delete:         bp("Delete asset. On failure returns reason (open_in_editor / has_referencers / unknown) plus referencer list. Pass force=true to auto-close any open asset editors before deleting (#278). Params: assetPath, force?", "delete_asset"),
    delete_batch:   bp("Batch-delete assets. Per-path status (deleted/absent/failed) plus reason+referencers on failed entries (#278). Params: assetPaths[], force?", "delete_asset_batch"),
    create_data_asset: bp("Create UDataAsset instance of custom class. Params: name, className (/Script/Module.ClassName or loaded name), packagePath?, properties? (key/value map)", "create_data_asset"),
    save:           bp("Save asset(s). Params: assetPath?", "save_asset"),
    save_all_dirty: bp("Flush every dirty package to disk in one call. End-of-workflow shortcut after bulk import/edit. Params: saveMapPackages? (default true), saveContentPackages? (default true). Returns savedAll boolean (#429)", "save_all_dirty", (p) => ({ saveMapPackages: p.saveMapPackages, saveContentPackages: p.saveContentPackages })),
    set_mesh_material:    bp("Assign material to static mesh slot. Params: assetPath, materialPath, slotIndex?", "set_mesh_material"),
    recenter_pivot:       { description: "Move static mesh pivot to geometry center. Params: assetPath OR assetPaths", bridge: "recenter_pivot", mapParams: (p) => {
      const paths = p.assetPaths as string[] | undefined;
      if (paths && paths.length > 0) return { assetPaths: paths };
      return { assetPath: p.assetPath };
    }},
    import_static_mesh:   bp("Import from FBX/OBJ. Params: filePath, name?, packagePath?, combineMeshes?, importMaterials?, importTextures?, generateLightmapUVs?", "import_static_mesh", (p) => ({ filename: p.filePath, destinationPath: p.packagePath, assetName: p.name, combineMeshes: p.combineMeshes, importMaterials: p.importMaterials, importTextures: p.importTextures, generateLightmapUVs: p.generateLightmapUVs })),
    import_skeletal_mesh: bp("Import skeletal mesh from FBX. Params: filePath, name?, packagePath?, skeletonPath?, importMaterials?, importTextures?", "import_skeletal_mesh", (p) => ({ filename: p.filePath, destinationPath: p.packagePath, assetName: p.name, skeletonPath: p.skeletonPath, importMaterials: p.importMaterials, importTextures: p.importTextures })),
    import_animation:     bp("Import anim from FBX. Params: filePath, name?, packagePath?, skeletonPath", "import_animation", (p) => ({ filename: p.filePath, destinationPath: p.packagePath, assetName: p.name, skeletonPath: p.skeletonPath })),
    import_texture:       bp("Import image. Params: filePath, name?, packagePath?", "import_texture", (p) => ({ filename: p.filePath, destinationPath: p.packagePath, assetName: p.name })),
    import_texture_batch: bp("Import many textures in one call - the loop stays inside the editor (no per-file bridge round-trip), so this finishes far faster than N import_texture calls. Per-item result records mirror import_texture. Params: items[]: [{filePath, packagePath?, name?, replaceExisting?}], packagePath? (default for items that don't set it), save? (default true), automated? (default true). Returns requested/imported/failed counts + items[] (#430)", "import_texture_batch", (p) => ({ items: p.items, packagePath: p.packagePath, save: p.save, automated: p.automated })),
    reimport:             bp("Reimport asset from source file. Params: assetPath, filePath?", "reimport_asset", (p) => ({ assetPath: p.assetPath, filePath: p.filePath })),
    read_datatable:       bp("Read DataTable rows. Params: assetPath, rowFilter?", "read_datatable", (p) => ({ path: p.assetPath, rowFilter: p.rowFilter })),
    create_datatable:     bp("Create DataTable. Params: name, packagePath?, rowStruct", "create_datatable"),
    reimport_datatable:   bp("Reimport DataTable from JSON. Params: assetPath, jsonPath?, jsonString?", "reimport_datatable", (p) => ({ path: p.assetPath, jsonPath: p.jsonPath, jsonString: p.jsonString })),
    list_textures:        bp("List textures. Params: directory?, recursive?", "list_textures"),
    get_texture_info:     bp("Get texture details. Params: assetPath", "get_texture_info"),
    set_texture_settings: bp("Set texture settings. Params: assetPath, settings (object with compressionSettings?, lodGroup?, sRGB?, neverStream?). Keys may also be passed at the top level.", "set_texture_settings", (p) => ({
      assetPath: p.assetPath,
      ...(typeof p.settings === "object" && p.settings !== null ? p.settings : {}),
      ...(p.compressionSettings !== undefined ? { compressionSettings: p.compressionSettings } : {}),
      ...(p.lodGroup !== undefined ? { lodGroup: p.lodGroup } : {}),
      ...(p.sRGB !== undefined ? { sRGB: p.sRGB } : {}),
      ...(p.neverStream !== undefined ? { neverStream: p.neverStream } : {}),
    })),
    add_socket:           bp("Add socket to StaticMesh or SkeletalMesh. Idempotent on socket name; pass onConflict='update' to overwrite an existing socket's transform with the supplied relativeLocation/relativeRotation/relativeScale (#412). Params: assetPath, socketName, boneName? (SkeletalMesh only, default 'root'), relativeLocation?, relativeRotation?, relativeScale?, onConflict? (skip\\|update\\|error, default skip)", "add_socket"),
    remove_socket:        bp("Remove socket by name. Params: assetPath, socketName", "remove_socket"),
    list_sockets:         bp("List sockets on a mesh (StaticMesh or SkeletalMesh). Params: assetPath", "list_sockets", (p) => ({ assetPath: p.assetPath })),
    set_socket_transform: bp("Update an existing socket's relative transform on StaticMesh or SkeletalMesh. Pass any subset of relativeLocation/relativeRotation/relativeScale; omitted fields stay at their current values. Errors if the socket does not exist (use add_socket to create). Common after FBX import when SOCKET_* empties land with scale=(100,100,100) (#412). Params: assetPath, socketName, relativeLocation?, relativeRotation?, relativeScale?", "set_socket_transform"),
    set_property:         bp("Set a UPROPERTY on any loaded asset (Material, DataAsset, DataTable, SubsurfaceProfile, etc.) using a dotted path. Walks nested structs and sub-objects internally - no more read-modify-write copies (e.g. `settings.mean_free_path_distance` on a UMaterial). Value goes through MCPJsonProperty::SetJsonOnProperty so JSON null clears object refs, structs accept {x,y,z}, arrays/maps round-trip. Params: assetPath, propertyName (dotted path), value (#420)", "set_asset_property", (p) => ({ assetPath: p.assetPath ?? p.path, propertyName: p.propertyName, value: p.value })),
    set_texture_settings_by_type: bp("Apply the canonical (compressionSettings, sRGB, LOD group) combo to every texture in each group: normal -> Normalmap, grayscale -> Grayscale, baseColor -> Default sRGB, hdr -> HDR. Params: groups (object: {normal?:[paths], grayscale?:[paths], baseColor?:[paths], hdr?:[paths]}) (#421)", "set_texture_settings_by_type", (p) => ({ groups: p.groups })),
    create_interchange_pipeline: bp("One-call factory for a UInterchangeGenericAssetsPipeline asset with the 15-property mesh-import boilerplate already applied (RecomputeNormals=false, MikkTSpace=true, HighPrecisionTangents=true, BuildNanite=false, CreatePhysicsAsset=false, etc.). Params: assetPath OR (name + packagePath?), meshType? (skeletal default | static), options? (dotted-path overrides on the resulting pipeline e.g. {'MeshPipeline.bBuildNanite': true}), onConflict? (#421)", "create_interchange_pipeline", (p) => ({ assetPath: p.assetPath, name: p.name, packagePath: p.packagePath, meshType: p.meshType, options: p.options, onConflict: p.onConflict })),
    reload_package:       bp("Force reload an asset package from disk. Params: assetPath", "reload_package"),
    health_check:         bp("Diagnose stuck-unloadable asset. Returns onDisk/inRegistry/isLoaded/canLoad/isStuck flags so an agent can detect the half-shutdown state where load returns null but the file exists (#279). Params: assetPath", "asset_health_check"),
    force_reload:         bp("Aggressive reload that resets package loaders + GCs + LoadObject. Recovers from the half-shutdown state without an editor restart (#279). Closes any open editors first. Params: assetPath", "force_reload_asset"),
    export:               bp("Export asset to disk file (Texture2D → PNG, StaticMesh → FBX, etc.). Params: assetPath, outputPath", "export_asset"),
    search_fts:           bp("Ranked asset search (token-scored over name/class/path). Params: query, maxResults?, classFilter?", "search_assets_fts", (p) => ({ query: p.query, maxResults: p.maxResults, classFilter: p.classFilter })),
    reindex_fts:          bp("Rebuild the SQLite FTS5 asset index. Params: directory?", "reindex_assets_fts", (p) => ({ directory: p.directory })),
    get_referencers:      bp("Reverse dependency lookup. Params: packages[] OR packagePath (#150). Returns {referencersByPackage, totalReferencers}.", "get_asset_referencers", (p) => ({ packages: p.packages, packagePath: p.packagePath })),
    // v1.0.0-rc.2 — #155 (asset gaps)
    set_sk_material_slots: bp("Set materials on a USkeletalMesh by slot name or slotIndex (bypasses the blueprint override-materials path that UE's ICH silently reverts). Params: assetPath, slots[{slotName?|slotIndex?, materialPath}]", "set_sk_material_slots"),
    diagnose_registry:    bp("Scan a content path and compare disk vs AssetRegistry (including in-memory pending-kill entries). Returns onDiskCount, inMemoryIncludedCount, ghostCount and paths. Params: path, recursive? (default true), reconcile? (forceRescan=true)", "diagnose_registry"),
    get_mesh_bounds:      bp("Get StaticMesh OR SkeletalMesh bounding box. Params: assetPath. Returns min, max, boxExtent, boxCenter, meshKind (#193/#351)", "get_mesh_bounds"),
    get_mesh_info:        bp("One-call mesh QA: bounds + material slots + skeleton + LOD/vertex counts. Works for both UStaticMesh and USkeletalMesh. Params: assetPath. Returns meshKind, boundsOrigin, boundsExtent, heightM, lodCount, vertexCount, skeletonPath (skeletal only), materialSlots:[{index, slotName, materialPath, isDefaultFallback}], materialCount (#431)", "get_mesh_info"),
    read_import_sources:  bp("Read AssetImportData source filenames on an imported asset (StaticMesh, SkeletalMesh, Texture, Animation, etc.). Returns sources[] of {relativeFilename, absolutePath, timestamp, fileHash, displayLabelName}. Params: assetPath (#270)", "read_import_sources", (p) => ({ assetPath: p.assetPath ?? p.path })),
    get_mesh_collision:   bp("Inspect StaticMesh collision setup. Params: assetPath. Returns collisionTraceFlag, hasSimple/ComplexCollision, element counts (#177)", "get_mesh_collision"),
    move_folder:          bp("Move/rename entire content folder with redirector fixup in one transaction. Params: sourcePath, destinationPath (#192)", "move_folder"),
    create_folder:        bp("Create empty content browser folder(s). Params: path OR paths[] (e.g. /Game/Foo, /Game/Bar/Baz). Returns per-path created/existed/failed (#212)", "create_folder", (p) => ({ path: p.path, paths: p.paths })),
    set_mesh_nav:         bp("Set StaticMesh nav contribution. Params: assetPath, bHasNavigationData?, clearNavCollision? (#167)", "set_mesh_nav"),
  },
  undefined,
  {
    saveMapPackages: z.boolean().optional().describe("save_all_dirty: include map packages (default true)"),
    saveContentPackages: z.boolean().optional().describe("save_all_dirty: include content packages (default true)"),
    items: z.array(z.object({
      filePath: z.string(),
      packagePath: z.string().optional(),
      name: z.string().optional(),
      replaceExisting: z.boolean().optional(),
    })).optional().describe("import_texture_batch entries"),
    save: z.boolean().optional().describe("import_texture_batch: save imported packages immediately (default true)"),
    automated: z.boolean().optional().describe("import_texture_batch: bypass interactive dialogs (default true)"),
    assetPath: z.string().optional().describe("Asset path"),
    directory: z.string().optional(), query: z.string().optional(),
    maxResults: z.number().optional(), typeFilter: z.string().optional(),
    searchAll: z.boolean().optional().describe("Search all content roots (plugins, engine content) not just /Game/"),
    recursive: z.boolean().optional(),
    sourcePath: z.string().optional(), destinationPath: z.string().optional(),
    newName: z.string().optional(),
    materialPath: z.string().optional().describe("Material asset path for set_mesh_material"),
    slotIndex: z.number().optional().describe("Material slot index (default 0)"),
    filePath: z.string().optional().describe("Absolute file path for imports"),
    name: z.string().optional().describe("Asset name (defaults to filename)"),
    packagePath: z.string().optional().describe("Destination package path (e.g. /Game/Meshes)"),
    groups: z.record(z.array(z.string())).optional().describe("set_texture_settings_by_type: { normal?: [...], grayscale?: [...], baseColor?: [...], hdr?: [...] }"),
    meshType: z.string().optional().describe("create_interchange_pipeline: 'skeletal' (default) or 'static'"),
    options: z.record(z.unknown()).optional().describe("create_interchange_pipeline: dotted-path overrides"),
    skeletonPath: z.string().optional(),
    combineMeshes: z.boolean().optional().describe("Combine all meshes in FBX into one (default false — imports as separate assets)"),
    importMaterials: z.boolean().optional(), importTextures: z.boolean().optional(),
    generateLightmapUVs: z.boolean().optional(),
    rowFilter: z.string().optional(), rowStruct: z.string().optional(),
    jsonPath: z.string().optional(), jsonString: z.string().optional(),
    exportName: z.string().optional(), propertyName: z.string().optional(),
    includeValues: z.boolean().optional().describe("Include property values in read_properties"),
    settings: z.record(z.unknown()).optional(),
    compressionSettings: z.string().optional().describe("Texture compression: Default, Normalmap, Grayscale, Displacementmap, VectorDisplacementmap, HDR, EditorIcon, Alpha, DistanceFieldFont, HDR_Compressed, BC7"),
    lodGroup: z.string().optional().describe("Texture LOD group: World, WorldNormalMap, Character, UI, Lightmap, Effects, etc."),
    sRGB: z.boolean().optional(),
    neverStream: z.boolean().optional(),
    assetPaths: z.array(z.string()).optional().describe("Array of asset paths (for recenter_pivot batch — first mesh sets reference pivot)"),
    renames: z.array(z.record(z.unknown())).optional().describe("Array of rename descriptors for bulk_rename — each {sourcePath, destinationPath} or {assetPath, newName}"),
    socketName: z.string().optional().describe("Socket name"),
    boneName: z.string().optional().describe("Bone name (for skeletal mesh sockets)"),
    relativeLocation: Vec3.optional().describe("Socket relative location"),
    relativeRotation: Rotator.optional().describe("Socket relative rotation"),
    relativeScale: Vec3.optional().describe("Socket relative scale"),
    outputPath: z.string().optional().describe("Absolute file path for export (e.g. C:/output/texture.png)"),
    classFilter: z.string().optional().describe("Restrict search_fts to assets whose class name contains this substring"),
    className: z.string().optional().describe("UClass path (/Script/Module.ClassName) or loaded class name for create_data_asset"),
    properties: z.record(z.unknown()).optional().describe("Key/value property overrides for create_data_asset"),
    packages: z.array(z.string()).optional().describe("Package paths for get_referencers"),
    // #155
    slots: z.array(z.object({
      slotName: z.string().optional(),
      slotIndex: z.number().optional(),
      materialPath: z.string(),
    })).optional().describe("Per-slot material assignments for set_sk_material_slots"),
    path: z.string().optional().describe("Content path (e.g. /Game/Foo) - used by diagnose_registry, create_folder"),
    paths: z.array(z.string()).optional().describe("Multiple content paths for create_folder"),
    reconcile: z.boolean().optional().describe("diagnose_registry: force synchronous rescan (evicts pending-kill ghosts)"),
    bHasNavigationData: z.boolean().optional().describe("Toggle nav data generation for set_mesh_nav"),
    clearNavCollision: z.boolean().optional().describe("Remove NavCollision from mesh for set_mesh_nav"),
    force: z.boolean().optional().describe("delete / delete_batch: auto-close any open asset editors before deleting (#278)"),
  },
);
