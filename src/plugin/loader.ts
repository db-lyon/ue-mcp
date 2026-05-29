import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import type { TaskConstructor, TaskDefinition, FlowDefinition } from "@db-lyon/flowkit";
import type { ToolDef } from "../types.js";
import type { FlowConfig, PluginEntry } from "../flow/schema.js";
import { warn, info } from "../log.js";
import { loadManifest, type PluginManifest, type ManifestInjectAction } from "./manifest.js";
import { bridgeTaskClass } from "../flow/task-factory.js";
import { resolvePackage, type ResolvedPackage } from "./resolver.js";
import { satisfiesMinimum } from "./version.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "./injection.js";
import { buildProvidedTool, type ProvisionPlan } from "./provision.js";
import { readDeployedBridgeApiVersion } from "./bridge-api.js";

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
  /** Categories this plugin contributes as new top-level MCP tools. Map
   *  category name -> action names provided. */
  provided: Record<string, string[]>;
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
  const bridgeApiVersion = readDeployedBridgeApiVersion(projectDir);
  const records: PluginRecord[] = [];
  const taskRegistrations: Array<{ name: string; ctor: TaskConstructor }> = [];
  const classPathRegistrations: Array<{ classPath: string; ctor: TaskConstructor }> = [];
  const taskDefs: Record<string, TaskDefinition> = {};
  const flowDefs: Record<string, FlowDefinition> = {};
  const knowledgeByCategory: Record<string, string[]> = {};
  // For each category, accumulate injection plans across all plugins.
  const plansByCategory = new Map<string, InjectionPlan[]>();
  // Provided categories: first writer wins. Tracks the owning plugin name
  // so cross-plugin collisions surface as skip reasons on the loser.
  const provisionByCategory = new Map<string, ProvisionPlan>();

  for (const entry of entries) {
    const record = await loadOne(
      entry,
      projectDir,
      serverVersion,
      builtInCategories,
      bridgeApiVersion,
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

    // Native handler auto-surfacing: when `nativeModule.category` is set, every
    // declared handler becomes `<category>(action="<prefix>_<handler>")`
    // dispatching to the bare bridge method the C++ module registers. No TS
    // task class is needed — a generic BridgeTask carries the method. This is
    // the path that makes a native-only plugin usable without per-handler
    // wrappers; without `category`, handlers stay bridge-only (back-compat).
    const nativeSurface = nativeHandlerInjection(manifest, pkg.name);
    if (nativeSurface) {
      const { plan, taskRegistrations: nativeRegs } = nativeSurface;
      taskRegistrations.push(...nativeRegs);
      const list = plansByCategory.get(plan.category) ?? [];
      list.push(plan);
      plansByCategory.set(plan.category, list);
    }

    // Provided categories: claim each name (first writer wins). Subsequent
    // plugins that try to provide the same name are skipped with a clear
    // reason. Actions land in the task registry under `<category>.<action>`
    // with no prefix — the provider owns the namespace.
    for (const [category, providedSpec] of Object.entries(manifest.provides)) {
      const existing = provisionByCategory.get(category);
      if (existing) {
        warn(
          "plugin",
          `${pkg.name}: provides target '${category}' already claimed by '${existing.pluginName}'; skipping this provider`,
        );
        continue;
      }
      const plan: ProvisionPlan = {
        category,
        pluginName: pkg.name,
        description: providedSpec.description,
        spec: providedSpec,
      };
      provisionByCategory.set(category, plan);

      for (const [actionName, actionSpec] of Object.entries(providedSpec.actions)) {
        const ctor = taskCtors.get(actionSpec.task);
        if (!ctor) continue;
        const dispatchName = `${category}.${actionName}`;
        taskRegistrations.push({ name: dispatchName, ctor });
      }

      record.record.provided[category] = Object.keys(providedSpec.actions);
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

  // Append provided categories as new top-level tools. By this point we
  // have already rejected names that collide with built-ins (in loadOne)
  // and with other plugins (first-wins above), so we can safely build and
  // push each plan's ToolDef.
  const providedTools: ToolDef[] = [];
  for (const plan of provisionByCategory.values()) {
    providedTools.push(buildProvidedTool(plan));
  }

  info(
    "plugin",
    `loaded ${records.filter((r) => r.status === "active").length}/${records.length} plugin(s)`,
  );

  return {
    tools: [...modifiedTools, ...providedTools],
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
  bridgeApiVersion: number | null,
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

  // Native module gate: the plugin ships C++ that registers handlers via
  // UEMCP::RegisterExternalHandler. Refuse to load if its minBridgeApi
  // exceeds the bridge ABI currently deployed in this project. We do NOT
  // hard-fail when bridgeApiVersion is unknown (no bridge deployed yet);
  // a warning lets users see the manifest but the C++ side won't run
  // until they `ue-mcp init`/`update` to deploy a bridge.
  if (manifest.nativeModule) {
    if (bridgeApiVersion === null) {
      warn(
        "plugin",
        `${entry.name}: declares nativeModule but no UE_MCP_Bridge is deployed yet — run \`ue-mcp init\` or \`ue-mcp deploy\` to deploy the bridge before invoking its native actions`,
      );
    } else if (manifest.nativeModule.minBridgeApi > bridgeApiVersion) {
      return skip(
        base,
        `nativeModule requires bridge ABI >= ${manifest.nativeModule.minBridgeApi} (deployed bridge is ${bridgeApiVersion}). Run \`ue-mcp deploy\` to refresh the bridge.`,
      );
    }
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

  // The native-handler surfacing target must likewise be a real category.
  const nativeCategory = manifest.nativeModule?.category;
  if (nativeCategory && !builtInCategories.has(nativeCategory)) {
    return skip(
      base,
      `nativeModule.category '${nativeCategory}' is not a registered category. Valid: ${[...builtInCategories].sort().join(", ")}`,
    );
  }

  // Provided categories must not collide with built-in category names.
  // Cross-plugin collisions are detected in the second pass in loadPlugins,
  // where all active plugins' provides: blocks are visible.
  for (const provided of Object.keys(manifest.provides)) {
    if (builtInCategories.has(provided)) {
      return skip(
        base,
        `provides target '${provided}' collides with a built-in category; plugins may not override built-ins`,
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

  // Every provides entry must also point to a task we just registered.
  for (const [category, providedSpec] of Object.entries(manifest.provides)) {
    for (const [actionName, actionSpec] of Object.entries(providedSpec.actions)) {
      if (!taskCtors.has(actionSpec.task)) {
        return skip(
          base,
          `provides ${category}.${actionName} references unknown task '${actionSpec.task}'`,
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
    provided: {},
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
 * Build the injection plan and dispatch task registrations that surface a
 * native module's handlers as MCP actions. Returns null when the manifest
 * declares no `nativeModule.category` (handlers stay bridge-only).
 *
 * Each handler `h` maps to action `<actionPrefix>_h` in the target category,
 * dispatching to the bare bridge method `h` via a generic BridgeTask. The
 * handler's `schema` becomes the action's params and `timeoutSeconds` its
 * bridge-call timeout. Pure and disk-free so it can be unit-tested directly.
 */
export function nativeHandlerInjection(
  manifest: PluginManifest,
  pluginName: string,
): {
  plan: InjectionPlan;
  taskRegistrations: Array<{ name: string; ctor: TaskConstructor }>;
} | null {
  const native = manifest.nativeModule;
  if (!native?.category) return null;

  const cat = native.category;
  const actions: Record<string, ManifestInjectAction> = {};
  const taskRegistrations: Array<{ name: string; ctor: TaskConstructor }> = [];

  for (const [hName, hSpec] of Object.entries(native.handlers)) {
    // One flat schema backs every action in the host category, so a
    // schema-level required param would be forced onto unrelated actions.
    // Native handlers validate their own params (RequireString), so force
    // every surfaced param optional regardless of what the manifest declares.
    const optionalSchema = hSpec.schema
      ? Object.fromEntries(
          Object.entries(hSpec.schema).map(([k, f]) => [k, { ...f, required: false }]),
        )
      : undefined;
    // `task` is synthetic — mergeInjectionsIntoTool reads only description and
    // schema; dispatch is wired through the registry registration below.
    actions[hName] = {
      task: `${manifest.actionPrefix}.${hName}`,
      description: hSpec.description,
      schema: optionalSchema,
    };
    const dispatchName = `${cat}.${manifest.actionPrefix}_${hName}`;
    const timeoutMs = hSpec.timeoutSeconds
      ? hSpec.timeoutSeconds * 1000
      : undefined;
    taskRegistrations.push({
      name: dispatchName,
      ctor: bridgeTaskClass(dispatchName, hName, undefined, timeoutMs),
    });
  }

  return {
    plan: { category: cat, prefix: manifest.actionPrefix, pluginName, actions },
    taskRegistrations,
  };
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
  if (!looksLikeBaseTask(TaskClass)) {
    throw new Error(
      `class from '${resolved}' does not look like a BaseTask subclass ` +
      `(missing run() / execute() on the prototype chain)`,
    );
  }
  return TaskClass as TaskConstructor;
}

/**
 * Duck-type check that a class behaves like a BaseTask subclass. We can't use
 * `instanceof BaseTask` here because npm 7+ installs peerDependencies into
 * the consumer's node_modules, giving the plugin its own copy of
 * `@db-lyon/flowkit` with a distinct BaseTask identity. `instanceof` then
 * fails across module instances even though the classes are behaviourally
 * identical. The prototype-chain check is sufficient because the registry
 * only invokes `.run()` on instances.
 */
export function looksLikeBaseTask(TaskClass: unknown): boolean {
  if (typeof TaskClass !== "function") return false;
  const proto = (TaskClass as { prototype?: unknown }).prototype;
  if (!proto || typeof proto !== "object") return false;
  // `run` is implemented on BaseTask itself; `execute` is abstract on
  // BaseTask and overridden by every subclass. Both must be reachable on
  // the prototype chain.
  return hasFn(proto, "run") && hasFn(proto, "execute");
}

function hasFn(obj: object, name: string): boolean {
  let cur: object | null = obj;
  while (cur && cur !== Object.prototype) {
    const desc = Object.getOwnPropertyDescriptor(cur, name);
    if (desc && typeof desc.value === "function") return true;
    cur = Object.getPrototypeOf(cur) as object | null;
  }
  return false;
}
