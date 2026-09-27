/**
 * The files a project's flow config is read from, each with a stamp that
 * changes whenever the file does. Shared by the live guard source and the
 * flow config cache, so both agree on when a config layer moved.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { globalConfigPath, selectedOverlay } from "../config/ue-mcp-config.js";

export interface ConfigLayer {
  file: string;
  /** Null when the file does not exist. */
  stamp: string | null;
}

export function configLayer(file: string): ConfigLayer {
  try {
    const stat = fs.statSync(file);
    return { file, stamp: `${stat.mtimeMs}:${stat.ctimeMs}:${stat.size}:${stat.ino}:${stat.mode}` };
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return { file, stamp: null };
    // The loader decides how to handle read failures (global is optional).
    // Recording them also lets a recovered stat trigger another load.
    return { file, stamp: `error:${String(error)}` };
  }
}

/** Global, project, env overlay and local layers, in the order the loader reads them. */
export function configLayers(configDir: string): ConfigLayer[] {
  const project = configLayer(path.join(configDir, "ue-mcp.yml"));
  const layers = [configLayer(globalConfigPath()), project];
  if (project.stamp !== null) {
    const overlay = selectedOverlay(configDir);
    if (overlay) layers.push(configLayer(path.join(configDir, `ue-mcp.${overlay}.yml`)));
    layers.push(configLayer(path.join(configDir, "ue-mcp.local.yml")));
  }
  return layers;
}

export function sameLayers(a: ConfigLayer[], b: ConfigLayer[]): boolean {
  return a.length === b.length && a.every((layer, i) => layer.file === b[i].file && layer.stamp === b[i].stamp);
}
