import * as fs from "node:fs";
import type { GuardDeclarations } from "../flow/guard-schema.js";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import type { TaskConstructor, TaskDefinition, FlowDefinition } from "@db-lyon/flowkit";
import type { ToolDef } from "../core/types.js";
import type { PluginEntry } from "../flow/schema.js";
import { warn, info } from "../core/log.js";
import {
  loadManifest,
  type PluginManifest,
  type ManifestInjectAction,
  type ManifestProvidedAction,
} from "./manifest.js";
import { bridgeTaskClass } from "../flow/task-factory.js";
import {
  flatContractShape,
  paramsClause,
  recordedSpecsProblems,
  type HandlerSpecs,
  type RecordedHandlerSpecs,
} from "../surface/handler-spec.js";
import type { NativeContractExtras } from "./native-contract.js";
import { resolvePackage, type ResolvedPackage } from "./resolver.js";
import { satisfiesMinimum } from "./version.js";
import {
  supersededByName,
  supersededByUePlugin,
  supersededMessage,
  type SupersededPlugin,
} from "./superseded.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "./injection.js";
import { buildProvidedTool, type ProvisionPlan } from "./provision.js";
import { readDeployedBridgeApiVersion } from "./bridge-api.js";
import { resolveServerImportsToSelf } from "./self-resolve.js";
import { requiredFieldNames, requiringParams, reservedParamName, reservedParamReason } from "./manifest-params.js";
import { ROUTING_PARAM_NAMES } from "../surface/routing-params.js";
import {
  partitionFlowsByGroup,
  runtimeConfigFor,
  type PluginConfigMap,
} from "./plugin-groups.js";

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
  /** Units the manifest declared that did not survive validation, while the
   *  rest of the plugin loaded. `"<manifest path>: <reason>"` each. Present
   *  and non-empty means the plugin is active but narrower than authored. */
  degraded: string[];
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
  /**
   * Guards each plugin declared, keyed by the plugin that declared them.
   *
   * Kept per plugin rather than merged, so a guard that cannot be built names
   * the manifest to open, and so two plugins choosing the same guard name are
   * distinguishable rather than one silently replacing the other.
   */
  guardsByPlugin: Array<{ plugin: string; guards: GuardDeclarations }>;
  /** Task names per plugin, so a name that used to mean "guard" can be refused. */
  taskNamesByPlugin: Array<{ plugin: string; taskNames: string[] }>;
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
  guardsByPlugin: [],
  taskNamesByPlugin: [],
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
  pluginConfig?: PluginConfigMap,
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
  const guardsByPlugin: Array<{ plugin: string; guards: GuardDeclarations }> = [];
  const taskNamesByPlugin: Array<{ plugin: string; taskNames: string[] }> = [];
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

    const { manifest, pkg, taskCtors, nativeSpecs } = record.payload;

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

    // Guards this plugin declares. They are not tasks and are not registered
    // as tasks: they are built into pipeline guards directly.
    if (Object.keys(manifest.guards).length > 0) {
      guardsByPlugin.push({ plugin: pkg.name, guards: manifest.guards });
    }
    taskNamesByPlugin.push({ plugin: pkg.name, taskNames: Object.keys(manifest.tasks) });

    // Merge plugin-supplied flows, filtered by the user's group toggles. A flow
    // whose group is disabled in `ue-mcp.pluginConfig.<slug>.groups` is dropped
    // before it reaches the registry, so the surface shrinks to what the user
    // opted into. Plan §"Correctness constraints" says multi-step plugin flows
    // should default to rollback_on_failure=true.
    const runtimeCfg = runtimeConfigFor(pluginConfig, pkg.name);
    const { enabled: enabledFlows, disabled: disabledFlows } = partitionFlowsByGroup(
      manifest.flows,
      runtimeCfg,
    );
    for (const flowName of enabledFlows) {
      const flowDef = manifest.flows[flowName];
      const stepCount = Object.keys(flowDef.steps).length;
      const normalised: FlowDefinition = {
        description: flowDef.description,
        steps: flowDef.steps as FlowDefinition["steps"],
        rollback_on_failure:
          flowDef.rollback_on_failure ?? (stepCount > 1 ? true : undefined),
      };
      flowDefs[flowName] = normalised;
    }
    // Reflect the filtered set on the record so introspection matches what
    // actually loaded, and note how many were held back by config.
    record.record.flows = enabledFlows;
    if (disabledFlows.length > 0) {
      info(
        "plugin",
        `${pkg.name}: ${disabledFlows.length} flow(s) disabled by pluginConfig groups (${disabledFlows.length} of ${enabledFlows.length + disabledFlows.length})`,
      );
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
        if (!ctor) continue; // skipped task - collision logged elsewhere
        const dispatchName = `${category}.${manifest.actionPrefix}_${bareName}`;
        taskRegistrations.push({ name: dispatchName, ctor: requiringParams(ctor, dispatchName, requiredFieldNames(injectSpec.schema)) });
      }
    }

    // Native handler auto-surfacing: when `nativeModule.category` is set, every
    // declared handler becomes an MCP action dispatching to the bare bridge
    // method the C++ module registers. No TS task class is needed - a generic
    // BridgeTask carries the method. A built-in category is injected into;
    // a new category is provisioned as a tool the plugin owns. Without
    // `category`, handlers stay bridge-only (back-compat).
    const nativeSurface = nativeHandlerSurface(manifest, pkg.name, builtInCategories, nativeSpecs);
    if (nativeSurface?.kind === "inject") {
      taskRegistrations.push(...nativeSurface.taskRegistrations);
      const list = plansByCategory.get(nativeSurface.plan.category) ?? [];
      list.push(nativeSurface.plan);
      plansByCategory.set(nativeSurface.plan.category, list);
    } else if (nativeSurface?.kind === "provide") {
      const cat = nativeSurface.plan.category;
      const existing = provisionByCategory.get(cat);
      if (existing) {
        warn(
          "plugin",
          `${pkg.name}: nativeModule.category '${cat}' already claimed by '${existing.pluginName}'; skipping this provider`,
        );
      } else {
        provisionByCategory.set(cat, nativeSurface.plan);
        taskRegistrations.push(...nativeSurface.taskRegistrations);
        record.record.provided[cat] = Object.keys(nativeSurface.plan.spec.actions);
      }
    }

    // Provided categories: claim each name (first writer wins). Subsequent
    // plugins that try to provide the same name are skipped with a clear
    // reason. Actions land in the task registry under `<category>.<action>`
    // with no prefix - the provider owns the namespace.
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
        taskRegistrations.push({ name: dispatchName, ctor: requiringParams(ctor, dispatchName, requiredFieldNames(actionSpec.schema)) });
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
    guardsByPlugin,
    taskNamesByPlugin,
    knowledgeByCategory,
  };
}

interface LoadOnePayload {
  manifest: PluginManifest;
  pkg: ResolvedPackage;
  taskCtors: Map<string, TaskConstructor>;
  /** The native module's recorded handler specs, when its manifest names a file (#1282). */
  nativeSpecs?: HandlerSpecs;
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
  const supersededEntry = supersededByName(entry.name);
  if (supersededEntry) return skipSuperseded(base, ...supersededEntry);
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
  // A renamed or forked package that still ships the superseded native module.
  const supersededNative = supersededByUePlugin(manifest.nativeModule?.uePluginName);
  if (supersededNative) return skipSuperseded(base, ...supersededNative);
  // Salvaged units: the plugin loads, minus whatever failed validation on its
  // own. Each one is warned individually so the log names the handler rather
  // than leaving the reader to diff the manifest against the live surface.
  for (const d of parsed.dropped) {
    base.degraded.push(`${d.path}: ${d.reason}`);
    warn("plugin", `${entry.name}: dropped ${d.path} - ${d.reason}`);
  }
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
        `${entry.name}: declares nativeModule but no UE_MCP_Bridge is deployed yet - run \`ue-mcp init\` or \`ue-mcp deploy\` to deploy the bridge before invoking its native actions`,
      );
    } else if (manifest.nativeModule.minBridgeApi > bridgeApiVersion) {
      return skip(
        base,
        `nativeModule requires bridge ABI >= ${manifest.nativeModule.minBridgeApi} (deployed bridge is ${bridgeApiVersion}). Run \`ue-mcp deploy\` to refresh the bridge.`,
      );
    }
  }

  // #1282: the native module's recorded contracts. A file that is named and
  // unusable takes the plugin down: surfacing its handlers without their
  // contracts would advertise parameters nothing checks.
  let nativeSpecs: HandlerSpecs | undefined;
  if (manifest.nativeModule?.specs) {
    const specsPath = path.join(pkg.pkgDir, manifest.nativeModule.specs);
    let raw: unknown;
    try {
      raw = JSON.parse(fs.readFileSync(specsPath, "utf-8"));
    } catch (e) {
      return skip(base, `nativeModule.specs '${manifest.nativeModule.specs}' could not be read: ${(e as Error).message}`);
    }
    const problems = recordedSpecsProblems(raw);
    if (problems.length > 0) {
      return skip(base, `nativeModule.specs '${manifest.nativeModule.specs}' is invalid: ${problems.slice(0, 5).join("; ")}`);
    }
    nativeSpecs = (raw as RecordedHandlerSpecs).handlers;
    for (const hName of Object.keys(manifest.nativeModule.handlers)) {
      if (nativeSpecs[hName]) continue;
      delete manifest.nativeModule.handlers[hName];
      base.degraded.push(`nativeModule.handlers.${hName}: no recorded spec in ${manifest.nativeModule.specs}`);
      warn("plugin", `${entry.name}: dropped nativeModule.handlers.${hName} - no recorded spec; run ue-mcp plugin record-specs`);
    }
    for (const method of Object.keys(nativeSpecs)) {
      if (manifest.nativeModule.handlers[method]) continue;
      base.degraded.push(`${manifest.nativeModule.specs}: spec for '${method}', which nativeModule.handlers does not declare`);
    }
  }

  // #1216: a parameter named after a routing key can never reach the plugin,
  // so the action declaring it is refused here rather than failing every call.
  const reserved = (names: Iterable<string>) => reservedParamName(names, ROUTING_PARAM_NAMES);
  const refuse = (unit: string, param: string, drop: () => void) => {
    drop();
    base.degraded.push(`${unit}: ${reservedParamReason(param)}`);
    warn("plugin", `${entry.name}: dropped ${unit} - ${reservedParamReason(param)}`);
  };
  for (const [category, actions] of Object.entries(manifest.inject)) {
    for (const [bare, spec] of Object.entries(actions)) {
      const bad = reserved(Object.keys(spec.schema ?? {}));
      if (bad) refuse(`inject.${category}.${bare}`, bad, () => delete actions[bare]);
    }
  }
  for (const [category, provided] of Object.entries(manifest.provides)) {
    for (const [action, spec] of Object.entries(provided.actions)) {
      const bad = reserved(Object.keys(spec.schema ?? {}));
      if (bad) refuse(`provides.${category}.actions.${action}`, bad, () => delete provided.actions[action]);
    }
  }
  if (manifest.nativeModule) {
    const handlers = manifest.nativeModule.handlers;
    for (const [hName, hSpec] of Object.entries(handlers)) {
      const recorded = nativeSpecs?.[hName]?.params ?? [];
      const bad = reserved([...Object.keys(hSpec.schema ?? {}), ...recorded.flatMap((p) => [p.name, ...(p.aliases ?? [])])]);
      if (bad) refuse(`nativeModule.handlers.${hName}`, bad, () => delete handlers[hName]);
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

  // The native-handler surfacing target is either a built-in category (handlers
  // inject into it) or a new category the plugin provisions. A new category must
  // be a legal category name; cross-plugin collisions are resolved first-wins in
  // loadPlugins.
  const nativeCategory = manifest.nativeModule?.category;
  if (
    nativeCategory &&
    !builtInCategories.has(nativeCategory) &&
    !/^[a-z][a-z0-9_]*$/.test(nativeCategory)
  ) {
    return skip(
      base,
      `nativeModule.category '${nativeCategory}' must be a built-in category or a valid new category name (lowercase identifier, /^[a-z][a-z0-9_]*$/)`,
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

  // Every inject and provides entry must point to a task we just registered.
  // One that does not is dropped rather than fatal: an action with no task
  // behind it cannot dispatch, but the plugin's other actions still can, and
  // the reason travels on the record instead of taking the category down.
  for (const [category, actions] of Object.entries(manifest.inject)) {
    for (const [bareName, spec] of Object.entries(actions)) {
      if (taskCtors.has(spec.task)) continue;
      delete actions[bareName];
      base.degraded.push(
        `inject.${category}.${bareName}: references unknown task '${spec.task}'`,
      );
      warn("plugin", `${entry.name}: dropped inject.${category}.${bareName} - unknown task '${spec.task}'`);
    }
  }

  for (const [category, providedSpec] of Object.entries(manifest.provides)) {
    for (const [actionName, actionSpec] of Object.entries(providedSpec.actions)) {
      if (taskCtors.has(actionSpec.task)) continue;
      delete providedSpec.actions[actionName];
      base.degraded.push(
        `provides.${category}.actions.${actionName}: references unknown task '${actionSpec.task}'`,
      );
      warn("plugin", `${entry.name}: dropped provides.${category}.${actionName} - unknown task '${actionSpec.task}'`);
    }
  }

  base.flows = Object.keys(manifest.flows);
  base.status = "active";
  return { record: base, payload: { manifest, pkg, taskCtors, nativeSpecs } };
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
    degraded: [],
  };
}

function skip(rec: PluginRecord, reason: string): LoadOneResult {
  warn("plugin", `${rec.name}: ${reason}`);
  rec.status = "skipped";
  rec.statusReason = reason;
  return { record: rec };
}

function skipSuperseded(rec: PluginRecord, name: string, s: SupersededPlugin): LoadOneResult {
  const reason = supersededMessage(name, s);
  warn("plugin", reason);
  rec.status = "skipped";
  rec.statusReason = reason;
  return { record: rec };
}

type TaskReg = { name: string; ctor: TaskConstructor };

/**
 * What a native module's handlers surface into. `inject` merges into an
 * existing built-in category (actions prefixed with `actionPrefix`); `provide`
 * provisions a new top-level category the plugin owns (actions unprefixed - the
 * category is the namespace).
 */
export type NativeHandlerSurface =
  | { kind: "inject"; plan: InjectionPlan; taskRegistrations: TaskReg[] }
  | { kind: "provide"; plan: ProvisionPlan; taskRegistrations: TaskReg[] };

/**
 * Surface a native module's handlers as MCP actions. Returns null when the
 * manifest declares no `nativeModule.category` (handlers stay bridge-only).
 *
 * When `category` is a built-in (per `builtInCategories`) the result is an
 * `inject` plan: each handler `h` becomes `<category>(action="<prefix>_h")`.
 * Otherwise it's a `provide` plan that owns a new `<category>` tool with
 * unprefixed actions `<category>(action="h")`. Either way `h` dispatches to the
 * bare bridge method `h` via a generic BridgeTask, carrying the handler's
 * `timeoutSeconds`. Pure and disk-free so it can be unit-tested directly.
 */
export function nativeHandlerSurface(
  manifest: PluginManifest,
  pluginName: string,
  builtInCategories: Set<string>,
  specs?: HandlerSpecs,
): NativeHandlerSurface | null {
  const native = manifest.nativeModule;
  if (!native?.category) return null;

  const cat = native.category;
  const taskRegistrations: TaskReg[] = [];

  const timeoutMs = (seconds?: number) =>
    seconds ? seconds * 1000 : undefined;

  // #1282: a handler with a recorded spec takes its parameters, Params clause
  // and per-call contract from it. The category keys are built from every
  // spec at once, so a key two handlers declare differently accepts either and
  // the contract holds each call to its own handler's declaration.
  const specced = Object.keys(native.handlers).filter((h) => specs?.[h]).map((h) => [h, specs![h]] as const);
  const zodSchema = specced.length > 0 ? flatContractShape(specced) : undefined;
  const surfaced = (hName: string, hSpec: { description?: string; schema?: ManifestInjectAction["schema"] }):
    { description?: string; schema?: ManifestInjectAction["schema"] } & NativeContractExtras => {
    const spec = specs?.[hName];
    // Injection and provisioning lift every key optional; the action keeps its required flags.
    if (!spec) return { description: hSpec.description, schema: hSpec.schema };
    return {
      description: [hSpec.description, paramsClause(spec)].filter(Boolean).join(" "),
      contract: { method: hName, spec },
      zodSchema,
    };
  };
  const prepFor = (action: string, hName: string) => {
    const spec = specs?.[hName];
    return spec
      ? { action, paramContract: { params: spec.params, choices: spec.choices, strict: true }, liveSpecMethod: hName }
      : undefined;
  };

  if (builtInCategories.has(cat)) {
    const actions: Record<string, ManifestInjectAction & NativeContractExtras> = {};
    for (const [hName, hSpec] of Object.entries(native.handlers)) {
      // `task` is synthetic - mergeInjectionsIntoTool reads only description
      // and schema; dispatch is wired through the registry registration below.
      actions[hName] = {
        task: `${manifest.actionPrefix}.${hName}`,
        ...surfaced(hName, hSpec),
      };
      const dispatchName = `${cat}.${manifest.actionPrefix}_${hName}`;
      taskRegistrations.push({
        name: dispatchName,
        ctor: bridgeTaskClass(dispatchName, hName, undefined, timeoutMs(hSpec.timeoutSeconds),
          prepFor(`${manifest.actionPrefix}_${hName}`, hName)),
      });
    }
    return {
      kind: "inject",
      plan: { category: cat, prefix: manifest.actionPrefix, pluginName, actions },
      taskRegistrations,
    };
  }

  // New category: provision a tool the plugin owns; actions are unprefixed.
  const actions: Record<string, ManifestProvidedAction & NativeContractExtras> = {};
  for (const [hName, hSpec] of Object.entries(native.handlers)) {
    actions[hName] = {
      task: `${cat}.${hName}`, // synthetic; buildProvidedTool ignores it
      ...surfaced(hName, hSpec),
    };
    const dispatchName = `${cat}.${hName}`;
    taskRegistrations.push({
      name: dispatchName,
      ctor: bridgeTaskClass(dispatchName, hName, undefined, timeoutMs(hSpec.timeoutSeconds), prepFor(hName, hName)),
    });
  }
  const spec: ProvisionPlan["spec"] = {
    description: native.categoryDescription,
    actions,
  };
  return {
    kind: "provide",
    plan: { category: cat, pluginName, description: native.categoryDescription, spec },
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
  resolveServerImportsToSelf();
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
