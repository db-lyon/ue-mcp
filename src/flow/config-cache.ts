/**
 * One session's flow config, rebuilt only when a config layer changes.
 *
 * The built-in defaults and the plugin contribution are fixed for the life of
 * a session load (a plugin change rebuilds the load), so the file stamps of the
 * global, project, env and local layers are the whole cache key.
 */
import type { ToolDef } from "../core/types.js";
import { configLayers, sameLayers, type ConfigLayer } from "./config-layers.js";
import { loadFlowConfig, type PluginContribution } from "./loader.js";
import type { FlowConfig } from "./schema.js";

export class FlowConfigCache {
  private entry: { layers: ConfigLayer[]; config: FlowConfig } | undefined;

  constructor(
    private readonly tools: ToolDef[],
    private readonly configDir: string | undefined,
    private readonly contribution?: PluginContribution,
  ) {}

  /** The merged config. A layer that fails to parse throws, and nothing is cached. */
  get(): FlowConfig {
    const dir = this.configDir ?? process.cwd();
    const layers = configLayers(dir);
    if (this.entry && sameLayers(this.entry.layers, layers)) return this.entry.config;
    const config = loadFlowConfig(this.tools, this.configDir, this.contribution).config;
    // A save that landed during the read is picked up on the next call.
    if (sameLayers(layers, configLayers(dir))) this.entry = { layers, config };
    return config;
  }
}
