import * as fs from "node:fs";
import * as path from "node:path";
import { globalConfigPath } from "../global-config.js";
import type { ToolDef } from "../types.js";
import type { GuardDeclarations } from "./guard-schema.js";
import type { GuardSource } from "./guards.js";
import { loadFlowConfig, type PluginContribution } from "./loader.js";
import type { FlowConfig } from "./schema.js";

interface ConfigLayer {
  file: string;
  stamp: string | null;
}

function configLayer(file: string): ConfigLayer {
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

function configLayers(configDir: string): ConfigLayer[] {
  const project = configLayer(path.join(configDir, "ue-mcp.yml"));
  const layers = [configLayer(globalConfigPath()), project];
  if (project.stamp !== null) {
    if (process.env.UE_MCP_ENV) layers.push(configLayer(path.join(configDir, `ue-mcp.${process.env.UE_MCP_ENV}.yml`)));
    layers.push(configLayer(path.join(configDir, "ue-mcp.local.yml")));
  }
  return layers;
}

function sameLayers(a: ConfigLayer[], b: ConfigLayer[]): boolean {
  return a.length === b.length && a.every((layer, i) => layer.file === b[i].file && layer.stamp === b[i].stamp);
}

function missingLayer(previous: ConfigLayer[], next: ConfigLayer[]): ConfigLayer | undefined {
  return previous.find((layer) => layer.stamp !== null
    && !next.some((current) => current.file === layer.file && current.stamp !== null));
}

function assertRegisteredStructure(next: GuardDeclarations, registered: GuardDeclarations): void {
  for (const name of new Set([...Object.keys(next), ...Object.keys(registered)])) {
    const declaration = next[name];
    const original = registered[name];
    if (!original || !declaration || declaration.scope !== original.scope || declaration.order !== original.order
      || (["before", "after"] as const).some((phase) => declaration[phase]?.class_path !== original[phase]?.class_path)) {
      throw new Error(`Guard '${name}' changed its registration; restart ue-mcp to add or remove guards or hooks, or change class_path, scope or order`);
    }
  }
}

/** One session's YAML guard source. Registration stays fixed until restart. */
export function createLiveGuardSource(
  tools: ToolDef[],
  configDir: string = process.cwd(),
  pluginContribution?: PluginContribution,
  onError: (message: string) => void = (message) => console.error(message),
): GuardSource & { guards: GuardDeclarations; config: FlowConfig } {
  const read = (layers: ConfigLayer[], startup: boolean): FlowConfig => {
    try {
      return loadFlowConfig(tools, configDir, pluginContribution, (file, error) => {
        if (!startup) throw new Error(`${file}: ${String(error)}`, { cause: error });
        onError(`Failed to read ${file}; ignoring the user-global config layer: ${String(error)}`);
      }).config;
    } catch (error) {
      const files = layers.filter((layer) => layer.stamp !== null).map((layer) => layer.file);
      throw new Error(`${files.join(", ") || path.join(configDir, "ue-mcp.yml")}: ${String(error)}`, { cause: error });
    }
  };

  // A save during startup is transient. Retry a bounded number of times, then
  // use the last successfully parsed config instead of failing add_editor.
  let initial: { config: FlowConfig; layers: ConfigLayer[] } | undefined;
  let layers = configLayers(configDir);
  for (let attempt = 0; attempt < 3; attempt++) {
    const missing = initial && missingLayer(initial.layers, layers);
    if (missing) {
      onError(`${missing.file} disappeared during startup; using the last valid config`);
      break;
    }
    let readError: unknown;
    try {
      initial = { config: read(layers, initial === undefined), layers };
    } catch (error) {
      readError = error;
    }
    const after = configLayers(configDir);
    if (sameLayers(layers, after)) {
      if (readError) {
        if (!initial) throw readError;
        onError(`Guard config changed during startup; using the last valid config: ${String(readError)}`);
      }
      break;
    }
    layers = after;
    if (attempt === 2) {
      if (!initial) throw readError;
      onError(`Guard config changed while being read; using the last valid config from ${initial.layers.map((layer) => layer.file).join(", ")}`);
    }
  }

  const registered = initial!.config.guards;
  let cached = registered;
  let goodLayers = initial!.layers;
  let observedLayers = goodLayers;
  let nextCheck = 0;
  let retryAt = Infinity;
  let lastError: string | undefined;

  return {
    label: goodLayers.filter((layer) => layer.stamp !== null).map((layer) => layer.file).join(", ") || path.join(configDir, "ue-mcp.yml"),
    config: initial!.config,
    guards: registered,
    liveOptions(name, phase) {
      const now = Date.now();
      // All hooks in a busy pipeline share one poll, at most four times/second.
      if (now < nextCheck) return cached[name]?.[phase]?.options;
      nextCheck = now + 250;
      const currentLayers = configLayers(configDir);
      if (sameLayers(currentLayers, observedLayers) && now < retryAt) return cached[name]?.[phase]?.options;
      observedLayers = currentLayers;
      retryAt = Infinity;
      try {
        const missing = missingLayer(goodLayers, currentLayers);
        if (missing) throw new Error(`${missing.file} disappeared; restart ue-mcp to remove a config layer`);
        const next = read(currentLayers, false).guards;
        assertRegisteredStructure(next, registered);
        if (!sameLayers(currentLayers, configLayers(configDir))) {
          retryAt = now + 250;
          throw new Error("Guard config changed while being read; retrying with the last valid options in effect");
        }
        cached = next;
        goodLayers = currentLayers;
        lastError = undefined;
      } catch (error) {
        // A transient read failure can recover without new file metadata.
        // Malformed YAML/registration edits are retried only after a change.
        let cause: unknown = error;
        while (cause instanceof Error) {
          if ((cause as NodeJS.ErrnoException).code) retryAt = now + 5000;
          cause = cause.cause;
        }
        const message = `Guard config could not be reloaded; declarations are unchanged: ${error instanceof Error ? error.message : String(error)}`;
        if (message !== lastError) onError(message);
        lastError = message;
      }
      return cached[name]?.[phase]?.options;
    },
  };
}
