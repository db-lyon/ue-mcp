/**
 * One session's flow config, rebuilt only when a config layer changes.
 *
 * The built-in defaults and the plugin contribution are fixed for the life of
 * a session load (a plugin change rebuilds the load), so the project's config
 * snapshot is the whole cache key: ProjectConfig.for hands back the same
 * instance until a layer file changes.
 */
import * as path from "node:path";
import type { ToolDef } from "../core/types.js";
import { ProjectConfig } from "../config/project-config.js";
import { buildDefaults, loadFlowConfig, type PluginContribution } from "./loader.js";
import type { ConfigLayerDoc } from "./flow-describe.js";
import type { FlowConfig } from "./schema.js";

export class FlowConfigCache {
  private entry: { source: ProjectConfig; config: FlowConfig } | undefined;

  constructor(
    private readonly tools: ToolDef[],
    private readonly configDir: string | undefined,
    private readonly contribution?: PluginContribution,
  ) {}

  private get dir(): string {
    return this.configDir ?? process.cwd();
  }

  /** The merged config. A layer that fails to parse throws, and nothing is cached. */
  get(): FlowConfig {
    const source = ProjectConfig.for(this.dir);
    if (this.entry?.source === source) return this.entry.config;
    const config = loadFlowConfig(this.tools, this.configDir, this.contribution).config;
    // A save that landed during the read is picked up on the next call.
    if (ProjectConfig.for(this.dir) === source) this.entry = { source, config };
    return config;
  }

  /**
   * Each layer the config is merged from, lowest precedence first, for saying
   * where a flow or step came from. A file that does not parse reads as empty
   * here, since `get` is what reports it.
   */
  layerDocs(): ConfigLayerDoc[] {
    const out: ConfigLayerDoc[] = [{ source: "built-in", doc: buildDefaults(this.tools) }];
    if (this.contribution) out.push({ source: "plugin", doc: this.contribution });
    const dir = path.resolve(this.dir);
    for (const layer of ProjectConfig.for(dir).layers) {
      if (layer.stamp === null) continue;
      const inProject = path.dirname(layer.file) === dir;
      out.push({ source: inProject ? path.basename(layer.file) : layer.file, doc: layer.error === undefined ? layer.doc : {} });
    }
    return out;
  }
}
