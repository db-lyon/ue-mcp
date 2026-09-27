/**
 * The files a project's flow config is read from, each with a stamp that
 * changes whenever the file does. The cascade and its overlay rule live in
 * config/project-config.ts; this is the stamp view the live readers poll.
 */
import { projectLayerStamps, type ConfigLayer } from "../config/project-config.js";

export { configLayer, sameLayers, type ConfigLayer } from "../config/project-config.js";

/** Global, project, env overlay and local layers, in the order they merge. */
export function configLayers(configDir: string): ConfigLayer[] {
  return projectLayerStamps(configDir);
}
