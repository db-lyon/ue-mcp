import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { BaseTask, type TaskConstructor, type TaskDefinition, type FlowDefinition } from "@db-lyon/flowkit";
import type { ToolDef } from "../types.js";
import type { FlowConfig, PluginEntry } from "../flow/schema.js";
import { warn, info } from "../log.js";
import { loadManifest, type PluginManifest } from "./manifest.js";
import { resolvePackage, type ResolvedPackage } from "./resolver.js";
import { satisfiesMinimum } from "./version.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "./injection.js";

/** Per-plugin record surfaced to the `plugins` introspection category. */
export interface PluginRecord {
  name: string;
  version: string;
  actionPrefix: string;
  pkgDir: string;
  manifestPath: string;
  minServerVersion?: string;
  uePluginDependency?: string;
  /** Final injected action names, by category. */
  injected: Record<string, string[]>;
  /** Knowledge files attached, by category. */
  knowledge: Record<string, string>;
  /** Plugin-supplied flow names. */
  flows: string[];
  /** Plugin-supplied task names registered in the flow registry. */
  tasks: string[];
  /** Validation status: "active" if loaded; anything else means skipped. */
  status: "active" | "skipped";
  statusReason?: string;
}

export interface PluginLoadResult {
  /** Tools with plugin actions merged into target categories. */
  tools: ToolDef[];
  /** Per-plugin records for introspection. */
  records: PluginRecord[];
  /** Task constructors to register with the flow registry. */
  taskRegistrations: Array<{ name: string; ctor: TaskConstructor }>;
  /** Class-path → constructor pairs for explicit registry resolution. */
  classPathRegistrations: Array<{ classPath: string; ctor: TaskConstructor }>;
  /** Plugin-contributed task definitions to merge into the FlowConfig. */
  taskDefs: Record<string, TaskDefinition>;
  /** Plugin-contributed flow definitions to merge into the FlowConfig. */
  flowDefs: Record<string, FlowDefinition>;
  /** Per-category markdown to append to AI-facing docs. */
  knowledgeByCategory: Record<string, string[]>;
}

const EMPTY_RESULT: PluginLoadResult = {
  tools: [],
  records: [],
  taskRegistrations: [],
  classPathRegistrations: [],
  taskDefs: {},
  flowDefs: {},
  knowledgeByCategory: {},
};

/**
 * Main plugin loading entry point. Walks the user's `plugins:` array, resolves
 * each package from node_modules, loads its manifest, validates, dynamically
 * imports its task classes, and computes injection plans.
 *
 * Failures are isolated per-plugin: a broken plugin is skipped with a loud
 * warning, never partially injected.
 */
export async function loadPlugins(
  tools: ToolDef[],
  entries: PluginEntry[],
  projectDir: string | undefined,
  serverVersion: string,
): Promise<PluginLoadResult> {
  if (!entries || entries.length === 0) {
    return { ...EMPTY_RESULT, tools };
  }
  if (!projectDir) {
    warn("plugin", `'plugins:' set in ue-mcp.yml but no project directory is loaded - plugins will not load. Pass the .uproject path as a server argument.`);
    return { ...EMPTY_RESULT, tools };
  }

  const builtInCategories = new Set(tools.map((t) => t.name));
  const records: PluginRecord[] = [];
  const taskRegistrations: Array<{ name: string; ctor: TaskConstructor }> = [];
  const classPathRegistrations: Array<{ classPath: string; ctor: TaskConstructor }> = [];
  const taskDefs: Record<string, TaskDefinition> = {};
  const flowDefs: Record<string, FlowDefinition> = {};
  const knowledgeByCategory: Record<string, string[]> = {};
  // For each category, accumulate injection plans across all plugins.
  const plansByCategory = new Map<string, InjectionPlan[]>();

  for (const entry of entries) {
    const record = await loadOne(
      entry,
      projectDir,
      serverVersion,
      builtInCategories,
    );
    records.push(record.record);
    if (record.record.status !== "active" || !record.payload) continue;

    const { manifest, pkg, taskCtors } = record.payload;

    // Register task constructors under (a) the plugin task name and (b) the
    // plugin class_path. Both are looked up by FlowRunner/registry consumers.
    for (const [pluginTaskName, ctor] of taskCtors) {
      const taskEntry = manifest.tasks[pluginTaskName];
      if (!taskEntry) continue;
      taskRegistrations.push({ name: pluginTaskName, ctor });
      classPathRegistrations.push({ classPath: taskEntry.class_path, ctor });
      taskDefs[pluginTaskName] = {
        class_path: taskEntry.class_path,
        description: taskEntry.description,
        group: pkg.name,
        options: {},
      };
    }

    // Merge plugin-supplied flows. Plan §"Correctness constraints" says
    // multi-step plugin flows should default to rollback_on_failure=true.
    for (const [flowName, flowDef] of Object.entries(manifest.flows)) {
      const stepCount = Object.keys(flowDef.steps).length;
      const normalised: FlowDefinition = {
        description: flowDef.description,
        steps: flowDef.steps as FlowDefinition["steps"],
        rollback_on_failure:
          flowDef.rollback_on_failure ?? (stepCount > 1 ? true : undefined),
      };
      flowDefs[flowName] = normalised;
    }

    // Per-category: build injection plans + queue knowledge attachment.
    for (const [category, actions] of Object.entries(manifest.inject)) {
      const plan: InjectionPlan = {
        category,
        prefix: manifest.actionPrefix,
        pluginName: pkg.name,
        actions,
      };
      const list = plansByCategory.get(category) ?? [];
      list.push(plan);
      plansByCategory.set(category, list);

      // Also register the injected dispatch name `<category>.<prefixed>` so
      // that index.ts's `${tool}.${action}` registry lookup finds the same
      // plugin task constructor.
      for (const [bareName, injectSpec] of Object.entries(actions)) {
        const pluginTaskName = injectSpec.task;
        const ctor = taskCtors.get(pluginTaskName);
        if (!ctor) continue; // skipped task — collision logged elsewhere
        const dispatchName = `${category}.${manifest.actionPrefix}_${bareName}`;
        taskRegistrations.push({ name: dispatchName, ctor });
      }
    }

    // Knowledge: load file contents now; the loader returns text so the
    // server can attach to instructions before the bridge connects.
    for (const [category, relPath] of Object.entries(manifest.knowledge)) {
      const abs = path.join(pkg.pkgDir, relPath);
      if (!fs.existsSync(abs)) {
        warn("plugin", `plugin ${pkg.name}: knowledge file ${relPath} not found, skipping`);
        continue;
      }
      const content = fs.readFileSync(abs, "utf-8");
      const list = knowledgeByCategory[category] ?? [];
      list.push(`<!-- ${pkg.name} -->\n${content}`);
      knowledgeByCategory[category] = list;
      record.record.knowledge[category] = relPath;
    }
  }

  // Apply merged injection plans to each target category tool. Plans are
  // applied left-to-right, so earlier-listed plugins win on action-name
  // collisions, matching the layered-config rule "later wins" for tasks but
  // intentionally INVERTED here so users get a stable surface.
  const modifiedTools = tools.map((t) => {
    const plans = plansByCategory.get(t.name);
    if (!plans || plans.length === 0) return t;
    const { tool: merged, added, skipped } = mergeInjectionsIntoTool(t, plans);
    for (const plan of plans) {
      const rec = records.find((r) => r.name === plan.pluginName);
      if (!rec) continue;
      // Filter `added` down to entries from THIS plugin only.
      const ownAdded = added.filter((a) => a.startsWith(`${plan.prefix}_`));
      if (ownAdded.length > 0) {
        rec.injected[t.name] = (rec.injected[t.name] ?? []).concat(ownAdded);
      }
    }
    for (const s of skipped) {
      warn("plugin", `injection collision skipped on ${t.name}: ${s.action} - ${s.reason}`);
    }
    return merged;
  });

  info(
    "plugin",
    `loaded ${records.filter((r) => r.status === "active").length}/${records.length} plugin(s)`,
  );

  return {
    tools: modifiedTools,
    records,
    taskRegistrations,
    classPathRegistrations,
    taskDefs,
    flowDefs,
    knowledgeByCategory,
  };
}

interface LoadOnePayload {
  manifest: PluginManifest;
  pkg: ResolvedPackage;
  taskCtors: Map<string, TaskConstructor>;
}

interface LoadOneResult {
  record: PluginRecord;
  payload?: LoadOnePayload;
}

async function loadOne(
  entry: PluginEntry,
  projectDir: string,
  serverVersion: string,
  builtInCategories: Set<string>,
): Promise<LoadOneResult> {
  const base = baseRecord(entry);
  let pkg: ResolvedPackage;
  try {
    pkg = resolvePackage(entry.name, entry.version, projectDir);
  } catch (e) {
    return skip(base, `resolve failed: ${(e as Error).message}`);
  }
  base.pkgDir = pkg.pkgDir;
  base.version = pkg.version;

  let parsed: ReturnType<typeof loadManifest>;
  try {
    parsed = loadManifest(pkg.pkgDir);
  } catch (e) {
    return skip(base, `manifest invalid: ${(e as Error).message}`);
  }
  const manifest = parsed.manifest;
  base.manifestPath = parsed.manifestPath;
  base.actionPrefix = manifest.actionPrefix;
  base.minServerVersion = manifest.minServerVersion;
  base.uePluginDependency = manifest.uePluginDependency;

  if (manifest.minServerVersion && !satisfiesMinimum(serverVersion, manifest.minServerVersion)) {
    return skip(base, `requires server >= ${manifest.minServerVersion} (have ${serverVersion})`);
  }

  // Target validation: every inject target must be a registered category.
  for (const target of Object.keys(manifest.inject)) {
    if (!builtInCategories.has(target)) {
      return skip(
        base,
        `inject target '${target}' is not a registered category. Valid: ${[...builtInCategories].sort().join(", ")}`,
      );
    }
  }

  // Dynamic task import. Class-path is resolved against multiple candidates
  // inside the plugin package; first existing file wins.
  const taskCtors = new Map<string, TaskConstructor>();
  for (const [pluginTaskName, taskEntry] of Object.entries(manifest.tasks)) {
    try {
      const ctor = await importTaskClass(pkg.pkgDir, taskEntry.class_path);
      taskCtors.set(pluginTaskName, ctor);
      base.tasks.push(pluginTaskName);
    } catch (e) {
      return skip(base, `task '${pluginTaskName}' load failed: ${(e as Error).message}`);
    }
  }

  // Every inject entry must point to a task we just registered.
  for (const [category, actions] of Object.entries(manifest.inject)) {
    for (const [bareName, spec] of Object.entries(actions)) {
      if (!taskCtors.has(spec.task)) {
        return skip(
          base,
          `inject ${category}.${bareName} references unknown task '${spec.task}'`,
        );
      }
    }
  }

  base.flows = Object.keys(manifest.flows);
  base.status = "active";
  return { record: base, payload: { manifest, pkg, taskCtors } };
}

function baseRecord(entry: PluginEntry): PluginRecord {
  return {
    name: entry.name,
    version: entry.version ?? "(unresolved)",
    actionPrefix: "(unresolved)",
    pkgDir: "(unresolved)",
    manifestPath: "(unresolved)",
    injected: {},
    knowledge: {},
    flows: [],
    tasks: [],
    status: "skipped",
  };
}

function skip(rec: PluginRecord, reason: string): LoadOneResult {
  warn("plugin", `${rec.name}: ${reason}`);
  rec.status = "skipped";
  rec.statusReason = reason;
  return { record: rec };
}

/**
 * Dynamically import a task class from a plugin package. Searches multiple
 * candidate locations under the plugin's `dist/` and root, accepting whichever
 * resolves first.
 */
async function importTaskClass(pkgDir: string, classPath: string): Promise<TaskConstructor> {
  const segments = classPath.replace(/\./g, "/");
  const candidates = [
    path.join(pkgDir, "dist", `${segments}.js`),
    path.join(pkgDir, "dist", "tasks", `${segments}.js`),
    path.join(pkgDir, `${segments}.js`),
    path.join(pkgDir, "dist", `${segments}/index.js`),
  ];
  let resolved: string | null = null;
  for (const c of candidates) {
    if (fs.existsSync(c)) {
      resolved = c;
      break;
    }
  }
  if (!resolved) {
    throw new Error(
      `class_path '${classPath}' could not be resolved. Searched:\n` +
      candidates.map((c) => `  - ${c}`).join("\n"),
    );
  }
  const mod = await import(pathToFileURL(resolved).href);
  const baseName = path.basename(segments);
  const TaskClass = mod.default ?? mod[baseName];
  if (!TaskClass) {
    throw new Error(
      `module '${resolved}' has no default export and no named export matching '${baseName}'`,
    );
  }
  if (!(TaskClass.prototype instanceof BaseTask)) {
    throw new Error(`class from '${resolved}' does not extend BaseTask`);
  }
  return TaskClass as TaskConstructor;
}
