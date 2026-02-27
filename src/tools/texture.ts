import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const textureTools: ToolDef[] = [
  bt("list_textures", "List texture assets in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("get_texture_info", "Get texture information: compression, LOD group, sRGB, resolution.", {
    assetPath: z.string(),
  }),
  bt("set_texture_settings", "Set texture settings (sRGB, compression, LOD group, streaming).", {
    assetPath: z.string(), settings: z.record(z.unknown()),
  }),
  bt("import_texture", "Import a texture from a file (PNG, TGA, etc.).", {
    filePath: z.string().describe("Absolute path to the image file"),
    name: z.string().optional(), packagePath: z.string().optional(),
  }),
];
