/**
 * `ue-mcp init` as two pure steps: plan() reads the machine and the project
 * and says what would change, apply() makes those changes. The interactive
 * CLI, `init --yes --json` and the installer all go through these, so every
 * install order lands on the same end state.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { EPIC_CATEGORIES } from "../tools/epic/index.js";
import { projectConfigPath, readConfigDoc, writeConfigDoc } from "../config/ue-mcp-config.js";
import { ProjectContext } from "../config/project.js";
import { deploy, projectEngineVersion } from "../editor/deployer.js";
import { installBinaryPlugin, resolveBinarySource, describeBinarySource } from "../editor/bridge-binaries.js";
import {
  defaultPluginDir,
  installKindOf,
  readInstallMarker,
  resolvePluginDir,
  unrealPlatform,
  type InstallKind,
} from "../editor/install-marker.js";
import { detectToolchain, inspectInstall, installWarning, DEFAULT_PROBE_HOOKS, type ProbeHooks } from "../editor/install-check.js";
import {
  coreSkillsInstalled,
  installCoreSkills,
  removeCoreSkills,
  conflictMessages,
  syncPluginSkills,
} from "../extensions/skills.js";
import { installClaudeHooks, uninstallClaudeHooks } from "../integrations/claude-code/hook-installer.js";
import { getInstalledHooks } from "../config/user-state.js";
import {
  detectMcpClients,
  isProjectScopedClient,
  readUeMcpEntry,
  writeMcpConfig,
  type McpClient,
  type ServerLaunch,
} from "../integrations/claude-code/mcp-client-config.js";
import { packageVersion } from "../core/package-root.js";
import { warn as logWarn } from "../core/log.js";

/* ------------------------------------------------------------------ */
/*  Tool categories                                                    */
/* ------------------------------------------------------------------ */

export interface ToolCategory {
  name: string;
  label: string;
  description?: string;
  requiredPlugins?: string[];
  alwaysOn?: boolean;
}

export const CATEGORIES: ToolCategory[] = [
  { name: "project",    label: "project",       alwaysOn: true },
  { name: "editor",     label: "editor",        alwaysOn: true },
  { name: "reflection", label: "reflection",    alwaysOn: true },
  { name: "level",      label: "levels",        description: "spawn/move/select actors, sublevels, world settings" },
  { name: "blueprint",  label: "blueprints",    description: "Blueprint classes: variables, functions, components, nodes" },
  { name: "material",   label: "materials",     description: "Materials, Material Functions, parameters" },
  { name: "asset",      label: "assets",        description: "import/move/rename/delete content browser assets" },
  { name: "animation",  label: "animation",     description: "anim sequences, montages, AnimBPs, skeletons" },
  { name: "niagara",    label: "vfx (niagara)", description: "VFX systems, emitters, modules", requiredPlugins: ["Niagara"] },
  { name: "landscape",  label: "landscape",     description: "terrain edit, layers, paint, sculpt" },
  { name: "pcg",        label: "pcg",           description: "procedural content graphs", requiredPlugins: ["PCG"] },
  { name: "foliage",    label: "foliage",       description: "instanced foliage placement and types" },
  { name: "audio",      label: "audio",         description: "MetaSounds, sound cues, sound classes" },
  { name: "widget",     label: "ui (widgets)",  description: "UMG widgets, editor utility widgets" },
  { name: "gameplay",   label: "gameplay / ai", description: "input mappings, collision, navmesh, PIE", requiredPlugins: ["EnhancedInput"] },
  { name: "gas",        label: "gas",           description: "Gameplay Ability System abilities, effects, attributes", requiredPlugins: ["GameplayAbilities"] },
  { name: "networking", label: "networking",    description: "replication, dormancy, RPCs" },
  { name: "demo",       label: "demo",          description: "tutorial / demo project scaffolding" },
  // feedback has its own toggle: enabling it gives the agent a tool that can
  // post to a public issue tracker, behind user approval.
  { name: "feedback",   label: "feedback",      alwaysOn: true },
];

/** Categories that carry wrapped engine tools, read from the generated modules. */
export const ENRICHABLE_CATEGORIES = Object.keys(EPIC_CATEGORIES).sort();

export type ContextStrategy = "micro" | "lean" | "full";

/** Stable ids for `--clients`, mapped to the names detectMcpClients uses. */
export const CLIENT_IDS: Record<string, string> = {
  "claude-code": "Claude Code (project)",
  "claude-code-global": "Claude Code (global)",
  "claude-desktop": "Claude Desktop",
  cursor: "Cursor",
  codex: "Codex",
};

export function clientId(name: string): string {
  return Object.entries(CLIENT_IDS).find(([, n]) => n === name)?.[0] ?? name;
}

/* ------------------------------------------------------------------ */
/*  Choices and plan                                                   */
/* ------------------------------------------------------------------ */

/** Everything a caller may decide. Anything unset keeps the project's current state, or the fresh-install default. */
export interface InitChoices {
  /** .uproject path. */
  project: string;
  /** Optional categories to disable. */
  disable?: string[];
  nativeTools?: { enabled: boolean; exclude: string[] };
  contextStrategy?: ContextStrategy;
  /**
   * Client ids (see CLIENT_IDS) or names. Unset keeps the clients that already
   * have a ue-mcp entry, or on a fresh project every detected project-scoped one.
   */
  clients?: string[];
  feedback?: boolean;
  promptHook?: boolean;
  skills?: boolean;
  /** auto picks binary when there is no toolchain and keeps whatever is installed. */
  install?: "auto" | InstallKind;
  /** Where prebuilt binaries come from; see resolveBinarySource. */
  binaries?: string | null;
  /** Pin client configs to this package version instead of @latest. */
  pin?: boolean;
  /** Replaces npx in written client configs. */
  command?: string | null;
}

export interface PlannedClient {
  id: string;
  name: string;
  configPath: string;
  detected: boolean;
  projectScoped: boolean;
  selected: boolean;
}

export interface InitPlan {
  version: string;
  project: { path: string; dir: string; name: string; engine: string | null; engineAssociation: string | null };
  isReInit: boolean;
  install: {
    kind: InstallKind;
    /** What is installed now, or null. */
    existing: InstallKind | null;
    pluginDir: string;
    /** Installed plugin version, when one is present. */
    installedVersion: string | null;
    /** Why this kind was chosen. */
    reason: string;
    /** True when the plugin tree will be written. */
    writes: boolean;
    toolchain: { present: boolean; name: string | null; fix?: string };
    /** Binary source, when kind is binary and the binaries will be fetched. */
    binarySource?: string;
  };
  requiredPlugins: string[];
  disable: string[];
  nativeTools: { enabled: boolean; exclude: string[] };
  contextStrategy: ContextStrategy;
  clients: PlannedClient[];
  launch: ServerLaunch;
  feedback: boolean;
  promptHook: boolean;
  skills: boolean;
  /** Reasons apply() would refuse to run. */
  blockers: string[];
}

function readUpluginVersion(pluginDir: string): string | null {
  try {
    const v = JSON.parse(fs.readFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "utf-8")).VersionName;
    return typeof v === "string" ? v : null;
  } catch {
    return null;
  }
}

/** Read the machine and the project and decide what init would do. Writes nothing. */
export function plan(choices: InitChoices, hooks: ProbeHooks = DEFAULT_PROBE_HOOKS): InitPlan {
  const project = new ProjectContext();
  project.setProject(choices.project);
  const projectDir = project.projectDir!;
  const blockers: string[] = [];

  const isReInit = fs.existsSync(path.join(projectDir, "ue-mcp.yml"));
  const existingDisabled = new Set(project.config.disable ?? []);
  const optional = new Set(CATEGORIES.filter((c) => !c.alwaysOn).map((c) => c.name));

  const disable = (choices.disable ?? [...existingDisabled].filter((c) => optional.has(c))).filter((c) => c !== "feedback");
  for (const c of disable) if (!optional.has(c)) blockers.push(`unknown category '${c}'`);

  const feedback = choices.feedback ?? (isReInit && !existingDisabled.has("feedback"));

  const existingNative = (project.config.nativeTools ?? {}) as { enabled?: boolean; exclude?: string[] };
  const nativeTools = choices.nativeTools ?? { enabled: existingNative.enabled !== false, exclude: existingNative.exclude ?? [] };
  const contextStrategy = choices.contextStrategy ?? project.config.context?.strategy ?? "micro";

  const requiredPlugins = new Set(["PythonScriptPlugin"]);
  for (const cat of CATEGORIES) {
    if (disable.includes(cat.name)) continue;
    for (const p of cat.requiredPlugins ?? []) requiredPlugins.add(p);
  }

  // Install kind. What is already there is adopted; source is never written over binaries.
  const located = resolvePluginDir(projectDir);
  const pluginDir = located?.dir ?? defaultPluginDir(projectDir);
  const existing = located ? installKindOf(located.dir) : null;
  const installedVersion = located ? readUpluginVersion(located.dir) : null;
  const toolchain = detectToolchain(hooks);
  const wanted = choices.install ?? "auto";
  let kind: InstallKind;
  let reason: string;
  if (wanted === "source" && existing === "binary") {
    kind = "binary";
    reason = "a binary install is present and source is never written over it";
    blockers.push("--install=source over an existing binary install; remove the plugin first to switch");
  } else if (wanted !== "auto") {
    kind = wanted;
    reason = `requested with --install=${wanted}`;
  } else if (existing) {
    kind = existing;
    reason = `adopting the existing ${existing} install`;
  } else if (toolchain.present) {
    kind = "source";
    reason = `C++ toolchain found (${toolchain.name})`;
  } else {
    kind = "binary";
    reason = "no C++ toolchain on this machine, so prebuilt binaries";
  }

  const version = packageVersion();
  const marker = located ? readInstallMarker(located.dir) : null;
  const engine = projectEngineVersion(project);
  const platform = unrealPlatform();
  let writes: boolean;
  let binarySource: string | undefined;
  if (kind === "source") {
    writes = existing !== "binary";
  } else {
    // A binary install is current when it matches this version, engine and platform.
    writes = !(existing === "binary" && installedVersion === version
      && (!marker || ((marker.engine ?? engine) === engine && (marker.platform ?? platform) === platform)));
    if (writes) {
      if (!engine || !/^\d+\.\d+$/.test(engine)) {
        blockers.push(`cannot pick prebuilt binaries: engine version of ${project.projectName} is unknown (${engine ?? "unset"})`);
      } else {
        binarySource = describeBinarySource(resolveBinarySource({ version, engine, platform }, choices.binaries));
      }
    }
  }

  const detected = detectMcpClients(projectDir);
  const wantedClients = choices.clients?.map((c) => CLIENT_IDS[c] ?? c);
  for (const name of wantedClients ?? []) {
    if (!detected.some((c) => c.name === name)) blockers.push(`unknown client '${name}' (known: ${Object.keys(CLIENT_IDS).join(", ")})`);
  }
  const configured = detected.filter((c) => readUeMcpEntry(c).configured).map((c) => c.name);
  const clients: PlannedClient[] = detected.map((c) => ({
    id: clientId(c.name),
    name: c.name,
    configPath: c.configPath,
    detected: c.detected,
    projectScoped: isProjectScopedClient(c.name),
    selected: wantedClients
      ? wantedClients.includes(c.name)
      : configured.length > 0
        ? configured.includes(c.name)
        : c.detected && isProjectScopedClient(c.name),
  }));

  const claudeCode = clients.some((c) => c.selected && c.name.startsWith("Claude Code"));
  const settingsPath = path.join(projectDir, ".claude", "settings.json");
  const hookInstalled = new Set(getInstalledHooks(projectDir).map((p) => path.resolve(p))).has(path.resolve(settingsPath));
  const promptHook = claudeCode && feedback && (choices.promptHook ?? hookInstalled);
  const skills = claudeCode && (choices.skills ?? coreSkillsInstalled(projectDir));

  return {
    version,
    project: {
      path: project.projectPath!,
      dir: projectDir,
      name: project.projectName!,
      engine,
      engineAssociation: project.engineAssociation,
    },
    isReInit,
    install: {
      kind,
      existing,
      pluginDir,
      installedVersion,
      reason,
      writes,
      toolchain: { present: toolchain.present, name: toolchain.name, fix: toolchain.fix },
      binarySource,
    },
    requiredPlugins: [...requiredPlugins],
    disable,
    nativeTools,
    contextStrategy,
    clients,
    launch: { pin: choices.pin ? version : null, command: choices.command ?? null },
    feedback,
    promptHook,
    skills,
    blockers,
  };
}

/* ------------------------------------------------------------------ */
/*  Apply                                                              */
/* ------------------------------------------------------------------ */

export interface AppliedStep {
  id: string;
  status: "changed" | "unchanged" | "removed" | "failed";
  target: string;
  detail: string;
}

export interface InitResult {
  ok: boolean;
  plan: InitPlan;
  steps: AppliedStep[];
  warnings: string[];
  error?: string;
}

/**
 * Write a .uproject change as one read-modify-write against the file as it is
 * now, through a rename, so an edit the editor made since plan() survives and
 * a reader never sees half a file.
 */
export function updateUproject(uprojectPath: string, mutate: (root: Record<string, unknown>) => boolean): boolean {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw) as Record<string, unknown>;
  if (!mutate(root)) return false;
  const tmp = `${uprojectPath}.ue-mcp-${process.pid}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(root, null, "\t"));
  fs.renameSync(tmp, uprojectPath);
  return true;
}

export function ensurePluginsEnabled(uprojectPath: string, pluginNames: string[]): string[] {
  const enabled: string[] = [];
  updateUproject(uprojectPath, (root) => {
    const plugins = (Array.isArray(root.Plugins) ? root.Plugins : (root.Plugins = [])) as Array<{ Name?: string; Enabled?: boolean }>;
    for (const name of pluginNames) {
      const existing = plugins.find((p) => p.Name?.toLowerCase() === name.toLowerCase());
      if (!existing) {
        plugins.push({ Name: name, Enabled: true });
        enabled.push(name);
      } else if (!existing.Enabled) {
        existing.Enabled = true;
        enabled.push(name);
      }
    }
    return enabled.length > 0;
  });
  return enabled;
}

/**
 * Ensure `entry` is a line in the project's .gitignore. True when the file was
 * created or the entry appended.
 */
export function ensureGitignoreEntry(projectDir: string, entry: string): boolean {
  const gitignorePath = path.join(projectDir, ".gitignore");
  if (!fs.existsSync(gitignorePath)) {
    fs.writeFileSync(gitignorePath, `${entry}\n`);
    return true;
  }
  const content = fs.readFileSync(gitignorePath, "utf-8");
  if (content.split(/\r?\n/).some((line) => line.trim() === entry)) return false;
  const sep = content.endsWith("\n") || content.length === 0 ? "" : "\n";
  fs.appendFileSync(gitignorePath, `${sep}${entry}\n`);
  return true;
}

const CONFIG_SCAFFOLD = [
  "ue-mcp:",
  "  version: 1",
  "",
  "# Custom tasks - pre-fill options for built-in actions",
  "# All built-in actions are available without listing them here.",
  "#",
  "# tasks:",
  "#   import_hero:",
  "#     class_path: ue-mcp.bridge",
  "#     options:",
  "#       method: import_skeletal_mesh",
  "#       filePath: Meshes/hero_sk.fbx",
  "#       packagePath: /Game/Characters/Hero",
  "",
  "tasks: {}",
  "",
  "# Flows compose tasks into repeatable multi-step sequences.",
  "#",
  "# flows:",
  "#   fresh_level:",
  "#     description: Blank level with basic lighting",
  "#     steps:",
  "#       1:",
  "#         task: level.create",
  "#         options:",
  "#           name: Sandbox",
  "#       2:",
  "#         task: level.spawn_light",
  "#         options:",
  "#           type: DirectionalLight",
  "",
  "flows: {}",
  "",
].join("\n");

/**
 * Merge the category, native-tool and context choices into ue-mcp.yml's
 * `ue-mcp:` block. Everything else in the file is kept. True when it changed.
 */
export function writeProjectConfig(
  projectDir: string,
  disabled: string[],
  nativeTools?: { enabled: boolean; exclude: string[] },
  contextStrategy?: ContextStrategy,
): boolean {
  const configPath = projectConfigPath(projectDir);
  const before = fs.existsSync(configPath) ? fs.readFileSync(configPath, "utf-8") : null;
  const existing = readConfigDoc(configPath, (e) => logWarn("init", "ue-mcp.yml was not valid YAML - overwriting", e));

  const block = ((existing["ue-mcp"] as Record<string, unknown>) ?? {});
  if (typeof block.version !== "number") block.version = 1;

  if (disabled.length > 0) block.disable = disabled;
  else delete block.disable;

  // Only persisted when it differs from the default (on, nothing excluded).
  if (nativeTools) {
    const nt: Record<string, unknown> = {};
    if (!nativeTools.enabled) nt.enabled = false;
    else if (nativeTools.exclude.length > 0) nt.exclude = nativeTools.exclude;
    if (Object.keys(nt).length > 0) block.nativeTools = nt;
    else delete block.nativeTools;
  }

  // micro is the default (#1172), so only lean and full are written.
  if (contextStrategy === "lean" || contextStrategy === "full") block.context = { strategy: contextStrategy };
  else if (contextStrategy === "micro") delete block.context;

  if (!Array.isArray(block.contentRoots) || (block.contentRoots as unknown[]).length === 0) {
    block.contentRoots = ["/Game/"];
  }

  existing["ue-mcp"] = block;
  if (!("tasks" in existing)) existing.tasks = {};
  if (!("flows" in existing)) existing.flows = {};

  writeConfigDoc(configPath, existing);
  return fs.readFileSync(configPath, "utf-8") !== before;
}

/** Make the changes a plan describes. Never throws; a failed step ends the run with ok=false. */
export async function apply(p: InitPlan): Promise<InitResult> {
  const steps: AppliedStep[] = [];
  const warnings: string[] = [];
  const done = (ok: boolean, error?: string): InitResult => ({ ok, plan: p, steps, warnings, error });

  if (p.blockers.length > 0) return done(false, p.blockers.join("; "));

  const project = new ProjectContext();
  project.setProject(p.project.path);
  const projectDir = p.project.dir;
  const uproject = p.project.path;

  try {
    // 1. The plugin tree.
    if (p.install.kind === "binary" && p.install.writes) {
      const engine = p.project.engine!;
      const target = { version: p.version, engine, platform: unrealPlatform() };
      const installed = await installBinaryPlugin(p.install.pluginDir, target, resolveBinarySource(target, p.install.binarySource));
      steps.push({ id: "plugin", status: "changed", target: installed.pluginDir, detail: `installed prebuilt binaries from ${installed.from}` });
    }
    const deployed = deploy(project);
    if (deployed.error) throw new Error(`plugin deployment failed: ${deployed.error}`);
    if (p.install.kind === "source") {
      steps.push({
        id: "plugin",
        status: deployed.cppPluginDeployed ? "changed" : "unchanged",
        target: deployed.pluginDir ?? p.install.pluginDir,
        detail: deployed.cppPluginDeployed ? "deployed bridge plugin source" : "bridge plugin source already current",
      });
    } else if (!p.install.writes) {
      steps.push({ id: "plugin", status: "unchanged", target: p.install.pluginDir, detail: "prebuilt binaries already current" });
    }

    // 2. .uproject plugin entries. deploy() added the bridge and Python.
    const enabled = ensurePluginsEnabled(uproject, ["UE_MCP_Bridge", ...p.requiredPlugins]);
    const enabledByDeploy = [deployed.pythonPluginEnabled && "PythonScriptPlugin", deployed.cppPluginEnabled && "UE_MCP_Bridge"]
      .filter((x): x is string => !!x);
    const allEnabled = [...enabledByDeploy, ...enabled];
    steps.push({
      id: "uproject",
      status: allEnabled.length > 0 ? "changed" : "unchanged",
      target: uproject,
      detail: allEnabled.length > 0 ? `enabled ${allEnabled.join(", ")}` : "required plugins already enabled",
    });

    try {
      const blocker = installWarning(inspectInstall(uproject, { skipToolchain: p.install.kind === "binary" }));
      if (blocker) warnings.push(blocker);
    } catch (e) {
      logWarn("init", "install check skipped", e);
    }

    // 3. ue-mcp.yml scaffold and .gitignore.
    const configPath = path.join(projectDir, "ue-mcp.yml");
    const scaffolded = !fs.existsSync(configPath);
    if (scaffolded) fs.writeFileSync(configPath, CONFIG_SCAFFOLD);
    if (ensureGitignoreEntry(projectDir, "ue-mcp.local.yml")) {
      steps.push({ id: "gitignore", status: "changed", target: path.join(projectDir, ".gitignore"), detail: "ignored ue-mcp.local.yml" });
    }

    // 4. MCP client configs.
    const detected = detectMcpClients(projectDir);
    for (const c of p.clients.filter((x) => x.selected)) {
      const client = detected.find((d) => d.name === c.name) as McpClient;
      const before = fs.existsSync(client.configPath) ? fs.readFileSync(client.configPath, "utf-8") : null;
      writeMcpConfig(client, uproject, p.launch);
      const after = fs.readFileSync(client.configPath, "utf-8");
      steps.push({ id: `client:${c.id}`, status: after === before ? "unchanged" : "changed", target: client.configPath, detail: `${c.name} MCP server entry` });
    }

    // 5. Claude Code hook and skills, which only mean something with Claude Code configured.
    if (p.clients.some((c) => c.selected && c.name.startsWith("Claude Code"))) {
      const settingsPath = path.join(projectDir, ".claude", "settings.json");
      if (p.promptHook) {
        installClaudeHooks(settingsPath, projectDir);
        steps.push({ id: "hook", status: "changed", target: settingsPath, detail: "feedback prompt hook installed" });
      } else if (uninstallClaudeHooks(settingsPath, projectDir)) {
        steps.push({ id: "hook", status: "removed", target: settingsPath, detail: "feedback prompt hook removed" });
      }
      if (p.skills) {
        const r = installCoreSkills(projectDir);
        warnings.push(...conflictMessages(r));
        steps.push({ id: "skills", status: r.installed.length > 0 ? "changed" : "unchanged", target: r.skillsDir, detail: `skills: ${r.installed.join(", ") || "none"}` });
      } else {
        const removed = removeCoreSkills(projectDir);
        if (removed.length > 0) steps.push({ id: "skills", status: "removed", target: projectDir, detail: `skills removed: ${removed.join(", ")}` });
      }
      const pluginSkills = syncPluginSkills(projectDir, configPath);
      for (const [owner, r] of Object.entries(pluginSkills.plugins)) {
        warnings.push(...conflictMessages(r));
        if (r.installed.length > 0) steps.push({ id: `skills:${owner}`, status: "changed", target: r.skillsDir, detail: r.installed.join(", ") });
      }
    }

    // 6. ue-mcp.yml choices, last, so the feedback toggle is captured.
    const disabled = p.feedback ? p.disable : [...p.disable, "feedback"];
    const configChanged = writeProjectConfig(projectDir, disabled, p.nativeTools, p.contextStrategy);
    steps.push({
      id: "config",
      status: scaffolded || configChanged ? "changed" : "unchanged",
      target: configPath,
      detail: scaffolded ? "ue-mcp.yml created" : "tool surface and content roots",
    });

    return done(true);
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    steps.push({ id: "error", status: "failed", target: projectDir, detail: message });
    return done(false, message);
  }
}
