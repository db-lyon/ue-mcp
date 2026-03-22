import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const assetTool: ToolDef = categoryTool(
  "asset",
  "Asset management: list, search, read, CRUD, import meshes/textures, datatables.",
  {
    list: {
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
    read:           bp("read_asset", (p) => ({ path: p.assetPath })),
    read_properties: bp("read_asset_properties"),
    duplicate:      bp("duplicate_asset"),
    rename:         bp("rename_asset"),
    move:           bp("move_asset"),
    delete:         bp("delete_asset"),
    save:           bp("save_asset"),
    set_mesh_material:    bp("set_mesh_material"),
    recenter_pivot:       bp("recenter_pivot", (p) => {
      const paths = p.assetPaths as string[] | undefined;
      if (paths && paths.length > 0) return { assetPaths: paths };
      return { assetPath: p.assetPath };
    }),
    import_static_mesh:   bp("import_static_mesh"),
    import_skeletal_mesh: bp("import_skeletal_mesh"),
    import_animation:     bp("import_animation"),
    import_texture:       bp("import_texture"),
    read_datatable:       bp("read_datatable", (p) => ({ path: p.assetPath, rowFilter: p.rowFilter })),
    create_datatable:     bp("create_datatable"),
    reimport_datatable:   bp("reimport_datatable", (p) => ({ path: p.assetPath, jsonPath: p.jsonPath, jsonString: p.jsonString })),
    list_textures:        bp("list_textures"),
    get_texture_info:     bp("get_texture_info"),
    set_texture_settings: bp("set_texture_settings"),
    add_socket:           bp("add_socket"),
    remove_socket:        bp("remove_socket"),
    list_sockets:         bp("list_sockets", (p) => ({ assetPath: p.assetPath })),
  },
  `- list: List assets in directory. Params: directory?, typeFilter?, recursive?
- search: Search by name/class/path. Params: query, directory?, maxResults?, searchAll? (set searchAll=true to search all content roots including plugin paths like /GASP/, not just /Game/)
- read: Read asset via reflection. Params: assetPath
- read_properties: Read specific properties. Params: assetPath, exportName?, propertyName?
- duplicate: Duplicate asset. Params: sourcePath, destinationPath
- rename: Rename asset. Params: assetPath, newName
- move: Move asset. Params: sourcePath, destinationPath
- delete: Delete asset. Params: assetPath
- save: Save asset(s). Params: assetPath?
- set_mesh_material: Assign material to static mesh slot. Params: assetPath, materialPath, slotIndex?
- recenter_pivot: Move static mesh pivot to geometry center. Params: assetPath OR assetPaths (array — first mesh sets the reference pivot for all)
- import_static_mesh: Import from FBX/OBJ. Params: filePath, name?, packagePath?
- import_skeletal_mesh: Import from FBX. Params: filePath, name?, packagePath?, skeletonPath?
- import_animation: Import anim from FBX. Params: filePath, name?, packagePath?, skeletonPath?
- import_texture: Import image. Params: filePath, name?, packagePath?
- read_datatable: Read DataTable rows. Params: assetPath, rowFilter?
- create_datatable: Create DataTable. Params: name, packagePath?, rowStruct
- reimport_datatable: Reimport from JSON. Params: assetPath, jsonPath?, jsonString?
- list_textures: List textures. Params: directory?, recursive?
- get_texture_info: Get texture details. Params: assetPath
- set_texture_settings: Set texture settings. Params: assetPath, settings
- add_socket: Add socket to StaticMesh or SkeletalMesh. Params: assetPath, socketName, boneName? (skeletal only), relativeLocation?, relativeRotation?, relativeScale?
- remove_socket: Remove socket by name. Params: assetPath, socketName
- list_sockets: List sockets on a mesh. Params: assetPath`,
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
    name: z.string().optional(), packagePath: z.string().optional(),
    skeletonPath: z.string().optional(),
    rowFilter: z.string().optional(), rowStruct: z.string().optional(),
    jsonPath: z.string().optional(), jsonString: z.string().optional(),
    exportName: z.string().optional(), propertyName: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
    assetPaths: z.array(z.string()).optional().describe("Array of asset paths (for recenter_pivot batch — first mesh sets reference pivot)"),
    socketName: z.string().optional().describe("Socket name"),
    boneName: z.string().optional().describe("Bone name (for skeletal mesh sockets)"),
    relativeLocation: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Socket relative location"),
    relativeRotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("Socket relative rotation"),
    relativeScale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Socket relative scale"),
  },
);
