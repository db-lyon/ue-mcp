/**
 * One project's configuration: every layer of its cascade read once, merged
 * by one rule, and kept as an immutable snapshot.
 *
 * Precedence, low to high: ~/.ue-mcp/config.yml, <project>/ue-mcp.yml, the
 * selected `ue-mcp.<env>.yml` overlay (UE_MCP_ENV, else the `env:` a layer
 * names), <project>/ue-mcp.local.yml. The `ue-mcp:` settings block, the flow
 * config (`tasks:`, `flows:`, `guards:`) and `plugins:` all come from the same
 * merge, so no reader can see a layer another reader skips.
 *
 * `ProjectConfig.for(dir)` answers from a cache keyed by the files' stamps, so
 * a reader that asks on every call pays for a stat, not a parse. A change to
 * any layer yields a new instance; an instance never changes.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { deepMerge } from "@db-lyon/flowkit";
import { warn } from "../core/log.js";
import { readEnv } from "../core/env.js";
import { PluginEntrySchema, type PluginEntry } from "../flow/schema.js";
import {
  configLayerFiles,
  overlayConfigPath,
  projectConfigPath,
  readConfigDoc,
  selectedOverlay,
  ueMcpBlockOf,
  type ConfigDoc,
  type ConfigLayerTarget,
} from "./ue-mcp-config.js";

/** A layer file and a stamp that changes whenever the file does. */
export interface ConfigLayer {
  file: string;
  /** Null when the file does not exist. */
  stamp: string | null;
}

/** The stamp of one file. */
export function configLayer(file: string): ConfigLayer {
  try {
    const stat = fs.statSync(file);
    return { file, stamp: `${stat.mtimeMs}:${stat.ctimeMs}:${stat.size}:${stat.ino}:${stat.mode}` };
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return { file, stamp: null };
    // Recorded so a recovered stat reads as a change and triggers a reload.
    return { file, stamp: `error:${String(error)}` };
  }
}

export function sameLayers(a: readonly ConfigLayer[], b: readonly ConfigLayer[]): boolean {
  return a.length === b.length && a.every((layer, i) => layer.file === b[i].file && layer.stamp === b[i].stamp);
}

/** One parsed layer of the cascade. */
export interface ProjectConfigLayer extends ConfigLayer {
  target: ConfigLayerTarget;
  /** The parsed document; {} when the file is absent or failed to parse. */
  doc: ConfigDoc;
  /** Why the file failed to parse, when it did. */
  error?: unknown;
}

/** Validated `plugins:` entries of a document. An entry that fails the schema is skipped. */
export function pluginEntriesOf(doc: ConfigDoc): PluginEntry[] {
  if (!Array.isArray(doc.plugins)) return [];
  const out: PluginEntry[] = [];
  for (const entry of doc.plugins) {
    const parsed = PluginEntrySchema.safeParse(entry);
    if (parsed.success) out.push(parsed.data);
  }
  return out;
}

/**
 * The layer files of a project in precedence order. The overlay is a sibling
 * of ue-mcp.yml, so it is selected only when that file exists.
 */
export function projectLayerFiles(projectDir: string): Array<{ target: ConfigLayerTarget; file: string }> {
  const hasProjectFile = configLayer(projectConfigPath(projectDir)).stamp !== null;
  return configLayerFiles(projectDir, hasProjectFile ? selectedOverlay(projectDir) : undefined);
}

/**
 * Stamps of every layer file, stat only while the cached snapshot still says
 * which overlay is selected, so a poll does not parse anything.
 */
export function projectLayerStamps(projectDir: string): ConfigLayer[] {
  const dir = path.resolve(projectDir);
  const cached = cache.get(dir);
  const base = configLayerFiles(dir).map((l) => configLayer(l.file));
  const [global, project, local] = base;
  if (project.stamp === null) return base;
  const known = cached !== undefined
    && cached.envVariable === (readEnv("env") ?? "")
    && sameLayers(base, (["global", "project", "local"] as const).map((t) => cached.layer(t)).filter(isLayer));
  const overlay = known ? cached.overlay : selectedOverlay(dir);
  return overlay ? [global, project, configLayer(overlayConfigPath(dir, overlay)), local] : base;
}

function isLayer(layer: ProjectConfigLayer | undefined): layer is ProjectConfigLayer {
  return layer !== undefined;
}

/** The overlay a set of parsed layers selects: UE_MCP_ENV, else the first `env:` from local, project, global. */
function overlayOf(docs: Partial<Record<ConfigLayerTarget, ConfigDoc>>): string | undefined {
  for (const value of [readEnv("env"), ...(["local", "project", "global"] as const).map((t) => ueMcpBlockOf(docs[t] ?? {}).env)]) {
    if (typeof value === "string" && value.trim() !== "") return value.trim();
  }
  return undefined;
}

const cache = new Map<string, ProjectConfig>();

export class ProjectConfig {
  /** The merged whole documents, lowest precedence first. Parse failures are left out. */
  readonly doc: ConfigDoc;
  /** The merged `ue-mcp:` block, before validation. */
  readonly block: Record<string, unknown>;
  /** The merged `plugins:` list. */
  readonly plugins: readonly PluginEntry[];

  /** UE_MCP_ENV as it stood when this snapshot was read, since it selects the overlay. */
  readonly envVariable = readEnv("env") ?? "";

  private constructor(
    readonly projectDir: string,
    readonly overlay: string | undefined,
    readonly layers: readonly ProjectConfigLayer[],
  ) {
    const readable = layers.filter((l) => l.error === undefined);
    this.doc = readable.reduce((acc, l) => deepMerge(acc, l.doc) as ConfigDoc, {} as ConfigDoc);
    this.block = readable.reduce(
      (acc, l) => deepMerge(acc, ueMcpBlockOf(l.doc)) as Record<string, unknown>,
      {} as Record<string, unknown>,
    );
    this.plugins = Object.freeze(pluginEntriesOf(this.doc));
    Object.freeze(this.layers);
  }

  /**
   * The current snapshot for a project directory. Returns the cached instance
   * while no layer file (and no selecting variable) changed.
   */
  static for(projectDir: string): ProjectConfig {
    const dir = path.resolve(projectDir);
    const stamps = projectLayerStamps(dir);
    const cached = cache.get(dir);
    if (cached && sameLayers(cached.layers, stamps)) return cached;
    const next = ProjectConfig.read(dir);
    // A save that landed during the read is picked up by the next call, and a
    // layer that could not be read is retried rather than remembered.
    const readable = next.layers.every((l) => (l.error as NodeJS.ErrnoException | undefined)?.code === undefined);
    if (readable && sameLayers(next.layers, projectLayerStamps(dir))) cache.set(dir, next);
    return next;
  }

  /** Read every layer now, bypassing the cache. Each file is read once. */
  static read(projectDir: string): ProjectConfig {
    const dir = path.resolve(projectDir);
    const readLayer = (target: ConfigLayerTarget, file: string): ProjectConfigLayer => {
      const stamp = configLayer(file);
      let error: unknown;
      const doc = stamp.stamp === null ? {} : readConfigDoc(file, (e) => { error = e; });
      return { target, ...stamp, doc, ...(error !== undefined ? { error } : {}) };
    };
    const [global, project, local] = configLayerFiles(dir).map((l) => readLayer(l.target, l.file));
    const overlay = project.stamp !== null
      ? overlayOf({ global: global.doc, project: project.doc, local: local.doc })
      : undefined;
    const layers = overlay
      ? [global, project, readLayer("env", overlayConfigPath(dir, overlay)), local]
      : [global, project, local];
    const config = new ProjectConfig(dir, overlay, layers);
    config.warnOnce();
    return config;
  }

  /** Forget every cached snapshot. Test seam. */
  static clearCache(): void {
    cache.clear();
  }

  /** The stamps this snapshot was read at. */
  stamps(): ConfigLayer[] {
    return this.layers.map((l) => ({ file: l.file, stamp: l.stamp }));
  }

  /** True while no layer file has changed since this snapshot was read. */
  isCurrent(): boolean {
    return sameLayers(this.layers, projectLayerStamps(this.projectDir));
  }

  /** A layer by target, when the cascade has it. */
  layer(target: ConfigLayerTarget): ProjectConfigLayer | undefined {
    return this.layers.find((l) => l.target === target);
  }

  /**
   * The merged documents for the flow config. A layer that failed to parse
   * throws, except the user-global one, which is optional: `onGlobalError` is
   * told and the layer is skipped (the default warns).
   */
  flowDoc(onGlobalError?: (file: string, error: unknown) => void): ConfigDoc {
    let merged: ConfigDoc = {};
    for (const layer of this.layers) {
      if (layer.error !== undefined) {
        if (layer.target !== "global") throw layer.error;
        // Without a handler, the warning read() already gave stands.
        onGlobalError?.(layer.file, layer.error);
        continue;
      }
      merged = deepMerge(merged, layer.doc) as ConfigDoc;
    }
    return merged;
  }

  /**
   * A value from the merged `ue-mcp:` block by dotted path, for `${project.*}`
   * defaults. Undefined when any segment is missing.
   */
  lookup(dotted: string): unknown {
    let cur: unknown = this.block;
    for (const seg of dotted.split(".").filter(Boolean)) {
      if (cur === null || typeof cur !== "object") return undefined;
      cur = (cur as Record<string, unknown>)[seg];
    }
    return cur;
  }

  private warnOnce(): void {
    for (const layer of this.layers) {
      if (layer.error === undefined) continue;
      if (layer.target === "global") {
        warn("config", `failed to parse ${layer.file} - ignoring the user-global config layer`, layer.error);
      } else {
        warn("project", `failed to parse ${layer.file} - skipping ue-mcp: block from this file`, layer.error);
      }
    }
    // The editor plugin picks its overlay from UE_MCP_ENV only, so a port
    // pinned in an overlay chosen by `env:` would split client and editor.
    const env = this.layer("env");
    if (env && !readEnv("env") && (ueMcpBlockOf(env.doc).bridge as { port?: unknown } | undefined)?.port !== undefined) {
      warn(
        "project",
        `ue-mcp.${this.overlay}.yml pins bridge.port and that overlay is selected by 'env: ${this.overlay}' in this project's ` +
          `config. The editor plugin selects its overlay from UE_MCP_ENV only, so it will not read that pin and ` +
          `the two would use different ports. Move the pin into ue-mcp.yml or ue-mcp.local.yml, or select the ` +
          `overlay with UE_MCP_ENV.`,
      );
    }
  }
}
