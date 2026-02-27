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
        return { directory: p.directory ?? "/Game/", recursive, assetCount: assets.length, assets: assets.slice(0, 2000) };
      },
    },
    search:         bp("search_assets"),
    read:           bp("read_asset", (p) => ({ path: p.assetPath })),
    read_properties: bp("read_asset_properties"),
    duplicate:      bp("duplicate_asset"),
    rename:         bp("rename_asset"),
    move:           bp("move_asset"),
    delete:         bp("delete_asset"),
    save:           bp("save_asset"),
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
  },
  `- list: List assets in directory. Params: directory?, typeFilter?, recursive?
- search: Search by name/class/path. Params: query, directory?, maxResults?
- read: Read asset via reflection. Params: assetPath
- read_properties: Read specific properties. Params: assetPath, exportName?, propertyName?
- duplicate: Duplicate asset. Params: sourcePath, destinationPath
- rename: Rename asset. Params: assetPath, newName
- move: Move asset. Params: sourcePath, destinationPath
- delete: Delete asset. Params: assetPath
- save: Save asset(s). Params: assetPath?
- import_static_mesh: Import from FBX/OBJ. Params: filePath, name?, packagePath?
- import_skeletal_mesh: Import from FBX. Params: filePath, name?, packagePath?, skeletonPath?
- import_animation: Import anim from FBX. Params: filePath, name?, packagePath?, skeletonPath?
- import_texture: Import image. Params: filePath, name?, packagePath?
- read_datatable: Read DataTable rows. Params: assetPath, rowFilter?
- create_datatable: Create DataTable. Params: name, packagePath?, rowStruct
- reimport_datatable: Reimport from JSON. Params: assetPath, jsonPath?, jsonString?
- list_textures: List textures. Params: directory?, recursive?
- get_texture_info: Get texture details. Params: assetPath
- set_texture_settings: Set texture settings. Params: assetPath, settings`,
  {
    assetPath: z.string().optional().describe("Asset path"),
    directory: z.string().optional(), query: z.string().optional(),
    maxResults: z.number().optional(), typeFilter: z.string().optional(),
    recursive: z.boolean().optional(),
    sourcePath: z.string().optional(), destinationPath: z.string().optional(),
    newName: z.string().optional(),
    filePath: z.string().optional().describe("Absolute file path for imports"),
    name: z.string().optional(), packagePath: z.string().optional(),
    skeletonPath: z.string().optional(),
    rowFilter: z.string().optional(), rowStruct: z.string().optional(),
    jsonPath: z.string().optional(), jsonString: z.string().optional(),
    exportName: z.string().optional(), propertyName: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
  },
);
