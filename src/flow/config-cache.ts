/**
 * One session's flow config, rebuilt only when a config layer changes.
 *
 * The built-in defaults and the plugin contribution are fixed for the life of
 * a session load (a plugin change rebuilds the load), so the file stamps of the
 * global, project, env and local layers are the whole cache key.
 */
import * as path from "node:path";
import type { ToolDef } from "../core/types.js";
import { readConfigDoc } from "../config/ue-mcp-config.js";
import { configLayers, sameLayers, type ConfigLayer } from "./config-layers.js";
import { buildDefaults, loadFlowConfig, type PluginContribution } from "./loader.js";
import type { ConfigLayerDoc } from "./flow-describe.js";
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

  /**
   * Each layer the config is merged from, lowest precedence first, for saying
   * where a flow or step came from. Read on demand; a file that does not parse
   * reads as empty here, since `get` is what reports it.
   */
  layerDocs(): ConfigLayerDoc[] {
    const out: ConfigLayerDoc[] = [{ source: "built-in", doc: buildDefaults(this.tools) }];
    if (this.contribution) out.push({ source: "plugin", doc: this.contribution });
    const dir = this.configDir ?? process.cwd();
    for (const layer of configLayers(dir)) {
      if (layer.stamp === null) continue;
      const inProject = path.dirname(layer.file) === path.resolve(dir);
      out.push({ source: inProject ? path.basename(layer.file) : layer.file, doc: readConfigDoc(layer.file, () => {}) });
    }
    return out;
  }
}
