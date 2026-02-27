import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const assetTools: ToolDef[] = [
  {
    name: "list_assets",
    description:
      "List all assets in a directory within the project's Content folder. " +
      "Optionally filter by file extension pattern. Returns asset paths and sizes.",
    schema: {
      directory: z.string().optional().describe("Directory path relative to Content, or /Game/ path. Omit to list all."),
      typeFilter: z.string().optional().describe("Filter by extension pattern (e.g. 'uasset', 'umap')"),
      recursive: z.boolean().optional().describe("Search subdirectories. Default: true"),
    },
    handler: async (ctx, params) => {
      ctx.project.ensureLoaded();
      const dir = params.directory
        ? ctx.project.resolveContentDir(params.directory as string)
        : ctx.project.contentDir!;
      const recursive = params.recursive !== false;
      const typeFilter = (params.typeFilter as string | undefined)?.toLowerCase();

      if (!fs.existsSync(dir)) throw new Error(`Directory not found: ${dir}`);

      const assets: Array<{ path: string; name: string; extension: string; sizeKB: number }> = [];

      function scan(d: string): void {
        for (const entry of fs.readdirSync(d, { withFileTypes: true })) {
          const full = path.join(d, entry.name);
          if (entry.isDirectory()) {
            if (recursive) scan(full);
          } else {
            const ext = path.extname(entry.name).slice(1).toLowerCase();
            if (ext !== "uasset" && ext !== "umap") continue;
            if (typeFilter && ext !== typeFilter) continue;

            const relativePath = ctx.project.getRelativeContentPath(full);
            const stat = fs.statSync(full);
            assets.push({
              path: relativePath,
              name: path.basename(entry.name, path.extname(entry.name)),
              extension: ext,
              sizeKB: Math.round(stat.size / 1024),
            });
          }
        }
      }

      scan(dir);
      return {
        directory: params.directory ?? "/Game/",
        recursive,
        assetCount: assets.length,
        assets: assets.slice(0, 2000),
      };
    },
  },

  bt("read_asset", "Read an Unreal Engine asset via editor reflection. Returns class name, properties, and values. Requires live editor.", {
    assetPath: z.string().describe("Path to the asset (/Game/ path, relative, or absolute)"),
  }, "read_asset", (p) => ({ path: p.assetPath })),

  bt("read_asset_properties", "Read specific properties from an asset export. Requires live editor.", {
    assetPath: z.string().describe("Path to the asset"),
    exportName: z.string().optional().describe("Name of the export to read"),
    propertyName: z.string().optional().describe("Specific property name to read"),
  }, "read_asset_properties"),

  bt("search_assets", "Search for assets by name, class, or path using the editor's Asset Registry.", {
    query: z.string().describe("Search query — matches asset names, paths, and class types"),
    directory: z.string().optional().describe("Limit search to a specific directory"),
    maxResults: z.number().optional().describe("Maximum results. Default: 50"),
  }, "search_assets"),

  bt("duplicate_asset", "Duplicate an asset to a new path. Requires live editor.", {
    sourcePath: z.string().describe("Source asset path"),
    destinationPath: z.string().describe("Destination asset path"),
  }, "duplicate_asset"),

  bt("rename_asset", "Rename an asset (move within same directory). Requires live editor.", {
    assetPath: z.string().describe("Current asset path"),
    newName: z.string().describe("New asset name"),
  }, "rename_asset"),

  bt("move_asset", "Move an asset to a different directory. Requires live editor.", {
    sourcePath: z.string().describe("Current asset path"),
    destinationPath: z.string().describe("Destination path"),
  }, "move_asset"),

  bt("delete_asset", "Delete an asset. Requires live editor.", {
    assetPath: z.string().describe("Path to the asset to delete"),
  }, "delete_asset"),

  bt("save_asset", "Save an asset or all modified assets. Requires live editor.", {
    assetPath: z.string().optional().describe("Asset path to save. Omit to save all."),
  }, "save_asset"),
];
