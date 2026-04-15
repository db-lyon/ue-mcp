import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3, Rotator } from "../schemas.js";

export const assetTool: ToolDef = categoryTool(
  "asset",
  "Asset management: list, search, read, CRUD, import meshes/textures, datatables.",
  {
    list: {
      description: "List assets in directory. Params: directory?, typeFilter?, recursive?",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const dir = p.directory ? ctx.project.resolveContentDir(p.directory as string) : ctx.project.contentDir!;
        const recursive = p.recursive !== false;
        const typeFilter = (p.typeFilter as string | undefined)?.toLowerCase();
        if (!fs.existsSync(dir)) throw new Error(`Directory not found: ${dir}`);
        const assets: Array<{ path: string; name: string; extension: string; sizeKB: number }> = [];
        function scan(d: string): void {
          for (const entry of fs.readdirSync(d, { withFileTypes: true })) {
            const full = path.join(d, entry.name);
            if (entry.isDirectory()) { if (recursive) scan(full); }
            else {
              const ext = path.extname(entry.name).slice(1).toLowerCase();
              if (ext !== "uasset" && ext !== "umap") continue;
              if (typeFilter && ext !== typeFilter) continue;
              assets.push({ path: ctx.project.getRelativeContentPath(full), name: path.basename(entry.name, path.extname(entry.name)), extension: ext, sizeKB: Math.round(fs.statSync(full).size / 1024) });
            }
          }
        }
        scan(dir);
        const result: Record<string, unknown> = { directory: p.directory ?? "/Game/", recursive, assetCount: assets.length, assets: assets.slice(0, 2000) };
        if (assets.length === 0) {
          const plugins = ctx.project.discoverPlugins();
          if (plugins.length > 0) {
            result.suggestion = `No assets found in ${p.directory ?? "/Game/"}. This project has plugin content — try listing one of these: ${plugins.map((pl) => pl.mountPoint).join(", ")}`;
            result.availablePlugins = plugins.map((pl) => ({ name: pl.name, mountPoint: pl.mountPoint }));
          }
        }
        return result;
      },
    },
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
    rename:         bp("Rename asset. Params: assetPath, newName", "rename_asset"),
    move:           bp("Move asset. Params: sourcePath, destinationPath", "move_asset"),
    delete:         bp("Delete asset. Params: assetPath", "delete_asset"),
    delete_batch:   bp("Batch-delete assets. Params: assetPaths[]. Returns per-path status (deleted/absent/failed).", "delete_asset_batch"),
    create_data_asset: bp("Create UDataAsset instance of custom class. Params: name, className (/Script/Module.ClassName or loaded name), packagePath?, properties? (key/value map)", "create_data_asset"),
    save:           bp("Save asset(s). Params: assetPath?", "save_asset"),
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
    reimport:             bp("Reimport asset from source file. Params: assetPath, filePath?", "reimport_asset", (p) => ({ assetPath: p.assetPath, filePath: p.filePath })),
    read_datatable:       bp("Read DataTable rows. Params: assetPath, rowFilter?", "read_datatable", (p) => ({ path: p.assetPath, rowFilter: p.rowFilter })),
    create_datatable:     bp("Create DataTable. Params: name, packagePath?, rowStruct", "create_datatable"),
    reimport_datatable:   bp("Reimport DataTable from JSON. Params: assetPath, jsonPath?, jsonString?", "reimport_datatable", (p) => ({ path: p.assetPath, jsonPath: p.jsonPath, jsonString: p.jsonString })),
    list_textures:        bp("List textures. Params: directory?, recursive?", "list_textures"),
    get_texture_info:     bp("Get texture details. Params: assetPath", "get_texture_info"),
    set_texture_settings: bp("Set texture settings. Params: assetPath, settings", "set_texture_settings"),
    add_socket:           bp("Add socket to StaticMesh or SkeletalMesh. Params: assetPath, socketName, boneName?, relativeLocation?, relativeRotation?, relativeScale?", "add_socket"),
    remove_socket:        bp("Remove socket by name. Params: assetPath, socketName", "remove_socket"),
    list_sockets:         bp("List sockets on a mesh. Params: assetPath", "list_sockets", (p) => ({ assetPath: p.assetPath })),
    reload_package:       bp("Force reload an asset package from disk. Params: assetPath", "reload_package"),
    export:               bp("Export asset to disk file (Texture2D → PNG, StaticMesh → FBX, etc.). Params: assetPath, outputPath", "export_asset"),
    search_fts:           bp("Ranked asset search (token-scored over name/class/path). Params: query, maxResults?, classFilter?", "search_assets_fts", (p) => ({ query: p.query, maxResults: p.maxResults, classFilter: p.classFilter })),
    reindex_fts:          bp("Rebuild the SQLite FTS5 asset index. Params: directory?", "reindex_assets_fts", (p) => ({ directory: p.directory })),
  },
  undefined,
  {
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
    skeletonPath: z.string().optional(),
    combineMeshes: z.boolean().optional().describe("Combine all meshes in FBX into one (default false — imports as separate assets)"),
    importMaterials: z.boolean().optional(), importTextures: z.boolean().optional(),
    generateLightmapUVs: z.boolean().optional(),
    rowFilter: z.string().optional(), rowStruct: z.string().optional(),
    jsonPath: z.string().optional(), jsonString: z.string().optional(),
    exportName: z.string().optional(), propertyName: z.string().optional(),
    includeValues: z.boolean().optional().describe("Include property values in read_properties"),
    settings: z.record(z.unknown()).optional(),
    assetPaths: z.array(z.string()).optional().describe("Array of asset paths (for recenter_pivot batch — first mesh sets reference pivot)"),
    socketName: z.string().optional().describe("Socket name"),
    boneName: z.string().optional().describe("Bone name (for skeletal mesh sockets)"),
    relativeLocation: Vec3.optional().describe("Socket relative location"),
    relativeRotation: Rotator.optional().describe("Socket relative rotation"),
    relativeScale: Vec3.optional().describe("Socket relative scale"),
    outputPath: z.string().optional().describe("Absolute file path for export (e.g. C:/output/texture.png)"),
    classFilter: z.string().optional().describe("Restrict search_fts to assets whose class name contains this substring"),
    className: z.string().optional().describe("UClass path (/Script/Module.ClassName) or loaded class name for create_data_asset"),
    properties: z.record(z.unknown()).optional().describe("Key/value property overrides for create_data_asset"),
  },
);
