import * as fs from "node:fs";
import * as path from "node:path";
import { deepMerge } from "@db-lyon/flowkit";
import { McpError, ErrorCode } from "./errors.js";
import { info, warn } from "./log.js";
import { UProjectSchema, UeMcpConfigSchema } from "./schemas.js";
import { resolveEngineRoot, type EngineLookup } from "./engine-root.js";
import {
  configLayerFiles,
  localConfigPath,
  overlayConfigPath,
  projectConfigPath,
  readConfigDoc,
  readGlobalUeMcpBlock,
  readUeMcpBlock,
  writeConfigDoc,
} from "./ue-mcp-config.js";
import { setInstalledHooks, setFeedbackMode, type FeedbackMode } from "./user-state.js";
import { resolveUProjectPath } from "./uproject-path.js";
import { readEnv } from "./env.js";

export interface PluginInfo {
  name: string;
  contentDir: string;
  mountPoint: string;
}

export interface UeMcpConfig {
  /** Content roots to search by default (e.g. ["/Game/", "/GASP/", "/MyPlugin/"]) */
  contentRoots?: string[];
  /** Tool categories to disable (e.g. ["gas", "networking", "pcg"]) */
  disable?: string[];
  /** Native (Epic 5.8 ToolsetRegistry) tool surfacing. Enabled by default;
   *  `exclude` names ue-mcp categories that should not be enriched with Epic
   *  tools (they remain reachable via the `epic` gateway). */
  nativeTools?: {
    enabled?: boolean;
    exclude?: string[];
  };
  /** Editor bridge WebSocket. `port` pins the bridge port instead of deriving
   *  it from the project root path (see port.ts). Unset = derived per-worktree port. */
  bridge?: {
    port?: number;
    /** Per-project equivalent of UE_MCP_HOST (#817). Unset = 127.0.0.1. */
    host?: string;
  };
  /** Per-project equivalents of UE_EDITOR_PATH and UE_BUILD_TOOL_PATH (#817).
   *  Unset = the engine this project's EngineAssociation names. */
  editor?: {
    path?: string;
    buildToolPath?: string;
  };
  /** Per-project equivalent of UE_MCP_ENV (#817): the `ue-mcp.<env>.yml`
   *  overlay this project merges. The env var still wins. */
  env?: string;
  /** Per-asset exclusive locking for concurrent agents (see locking.ts).
   *  Opt-in; disabled by default. */
  locking?: {
    enabled?: boolean;
    ttlSeconds?: number;
  };
  /** Optional HTTP surface for flow.run (#144). Disabled by default. */
  http?: {
    enabled?: boolean;
    /** Default 7723. Bound to 127.0.0.1 only. */
    port?: number;
    /** Override bind host. Defaults to 127.0.0.1 - do not expose externally. */
    host?: string;
  };
  /** Context-seeding strategy. `micro` (default) collapses everything behind
   *  one gateway tool; `lean` keeps the category tools and action names with
   *  signatures on demand; `full` lists every action's signature inline. See
   *  lean-context.ts and micro-context.ts. */
  context?: {
    strategy?: "full" | "lean" | "micro";
  };
  /** Play In Editor. `allowIgnoreBlueprintErrors` pre-authorizes
   *  editor(play_in_editor_ignore_blueprint_errors) so it stops asking for
   *  per-launch approval. Off by default. */
  pie?: {
    allowIgnoreBlueprintErrors?: boolean;
  };
  /** Per-plugin runtime config, keyed by plugin slug (package name minus
   *  `ue-mcp-`). `groups` toggles whole flow groups (opt-out). See
   *  plugin-groups.ts. */
  pluginConfig?: Record<string, { groups?: Record<string, boolean> } & Record<string, unknown>>;
}

/**
 * Read the merged `ue-mcp:` config for a project directory without loading the
 * project. Lets a caller learn a project's settings (its pinned bridge port,
 * say) before committing to the switch.
 */
/**
 * One `ue-mcp:` key that failed validation, and what was wrong with it.
 *
 * Kept per project directory so a reader who is already looking at the project
 * (project(get_status), the startup banner) can be told, rather than the user
 * having to find a warn() line on stderr that their MCP client writes to a log
 * nobody opens.
 */
export interface UeMcpConfigRejection {
  key: string;
  message: string;
}

const rejectionsByDir = new Map<string, UeMcpConfigRejection[]>();

/** Every `ue-mcp:` key that was dropped for this project, newest read wins. */
export function ueMcpConfigRejections(projectDir?: string | null): UeMcpConfigRejection[] {
  if (!projectDir) return [];
  return rejectionsByDir.get(path.resolve(projectDir)) ?? [];
}

/** One line per rejected key, phrased for a person reading a terminal. */
export function describeConfigRejections(rejections: UeMcpConfigRejection[]): string[] {
  return rejections.map(
    (r) =>
      `ue-mcp.yml: '${r.key}' is not a valid setting and was ignored (${r.message}). ` +
      `Every other key in the block still applies.`,
  );
}

/**
 * Validate the merged block key by key, keeping the ones that parse.
 *
 * The whole-block safeParse this replaced was all-or-nothing: one malformed
 * key - `disable: gas` where a list is required - dropped the entire block,
 * so a pinned `bridge.port` went with it, the derived hash port was used
 * instead, and the client ended up on a different port from the editor, which
 * reads the same yaml and binds what `bridge.port` says. One typo, two
 * subsystems disagreeing about where the editor is.
 *
 * Each declared key is parsed against its OWN schema, so nothing reaches the
 * result unvalidated: a key that fails is dropped, never coerced and never
 * passed through. Undeclared keys pass through exactly as they did before,
 * because the schema is `.passthrough()` and always was.
 */
export function partitionUeMcpConfig(block: unknown): {
  config: UeMcpConfig;
  rejected: UeMcpConfigRejection[];
} {
  if (block === null || typeof block !== "object" || Array.isArray(block)) {
    return {
      config: {},
      rejected: [{ key: "ue-mcp", message: "the block is not a mapping of settings" }],
    };
  }

  const whole = UeMcpConfigSchema.safeParse(block);
  if (whole.success) return { config: whole.data, rejected: [] };

  const shape = UeMcpConfigSchema.shape as Record<string, { safeParse(v: unknown): { success: boolean; data?: unknown; error?: { issues: Array<{ message: string; path: Array<string | number> }> } } }>;
  const kept: Record<string, unknown> = {};
  const rejected: UeMcpConfigRejection[] = [];
  for (const [key, value] of Object.entries(block as Record<string, unknown>)) {
    const keySchema = shape[key];
    if (!keySchema) {
      kept[key] = value;
      continue;
    }
    const one = keySchema.safeParse(value);
    if (one.success) {
      if (one.data !== undefined) kept[key] = one.data;
      continue;
    }
    const issue = one.error?.issues?.[0];
    const where = issue && issue.path.length > 0 ? `${key}.${issue.path.join(".")}: ` : "";
    rejected.push({ key, message: `${where}${issue?.message ?? "invalid value"}` });
  }

  // The survivors are parsed again as a whole, so the returned object is one
  // the schema accepts rather than a hand-assembled record that only looks
  // like one. A failure here cannot come from a key that just validated, so
  // the honest answer is the empty config the old code returned.
  const again = UeMcpConfigSchema.safeParse(kept);
  if (!again.success) {
    return {
      config: {},
      rejected: [...rejected, { key: "ue-mcp", message: "the surviving keys still did not validate together" }],
    };
  }
  return { config: again.data, rejected };
}

export function readUeMcpConfig(projectDir: string): UeMcpConfig {
  const block = loadLayeredUeMcpBlock(projectDir);
  const { config, rejected } = partitionUeMcpConfig(block);
  rejectionsByDir.set(path.resolve(projectDir), rejected);
  for (const line of describeConfigRejections(rejected)) {
    warn("project", line);
  }
  return config;
}

export class ProjectContext {
  projectPath: string | null = null;
  projectName: string | null = null;
  contentDir: string | null = null;
  engineAssociation: string | null = null;
  config: UeMcpConfig = {};

  get isLoaded(): boolean {
    return this.projectPath !== null;
  }

  setProject(inputPath: string): void {
    this.projectPath = resolveUProjectPath(inputPath);
    // Strip whatever case the extension actually carries: basename() compares
    // the suffix byte for byte, so a hardcoded ".uproject" leaves the whole
    // "Game.UPROJECT" standing as the project name.
    this.projectName = path.basename(this.projectPath, path.extname(this.projectPath));
    this.contentDir = path.join(path.dirname(this.projectPath), "Content");
    // The cache is keyed to nothing but the process, so a switch would keep
    // resolving /MyPlugin/ paths against the project we just left.
    this._pluginCache = null;
    this.parseUProject();
    this.loadConfig();
  }

  ensureLoaded(): void {
    if (!this.isLoaded) {
      throw new McpError(
        ErrorCode.PROJECT_NOT_LOADED,
        'No project loaded. Pass the .uproject path as an argument in your MCP config, e.g. "args": ["C:/path/to/MyGame.uproject"]',
      );
    }
  }

  resolveContentPath(assetPath: string): string {
    this.ensureLoaded();
    // A trailing slash is an unambiguous directory hint; resolve as a dir
    // rather than a file so "/Game/MyFolder/" does not become
    // "/Game/MyFolder.uasset".
    if (assetPath.endsWith("/") || assetPath.endsWith("\\")) {
      return this.resolveContentDir(assetPath);
    }
    if (isGamePath(assetPath)) {
      let stripped = stripGamePrefix(assetPath);
      if (!stripped.endsWith(".uasset") && !stripped.endsWith(".umap")) {
        stripped += ".uasset";
      }
      return path.join(this.contentDir!, ...stripped.split("/"));
    }
    if (path.isAbsolute(assetPath)) return assetPath;
    let normalized = assetPath.replace(/\\/g, "/");
    if (!normalized.endsWith(".uasset") && !normalized.endsWith(".umap")) {
      normalized += ".uasset";
    }
    return path.join(this.contentDir!, ...normalized.split("/"));
  }

  resolveContentDir(dirPath: string): string {
    this.ensureLoaded();
    if (isGamePath(dirPath)) {
      const stripped = stripGamePrefix(dirPath).replace(/\/+$/, "");
      return path.join(this.contentDir!, ...stripped.split("/"));
    }
    const pluginResolved = this.resolvePluginPath(dirPath);
    if (pluginResolved) return pluginResolved;
    if (path.isAbsolute(dirPath)) return dirPath;
    return path.join(this.contentDir!, ...dirPath.replace(/\\/g, "/").replace(/\/+$/, "").split("/"));
  }

  /**
   * The directory a mount path (/Game, /Game/Foo, /MyPlugin/Bar) names.
   * Throws when the path is not a mount path or names no known mount.
   */
  resolveMountDir(contentPath: string): string {
    const normalized = contentPath.replace(/\\/g, "/").replace(/\/+$/, "") || "/Game";
    if (!normalized.startsWith("/")) {
      throw new Error(
        `'contentPath' must be a mount path such as /Game or /Game/Characters (got '${contentPath}').`,
      );
    }
    if (isGamePath(normalized)) {
      if (!this.contentDir) throw new Error("No project is loaded, so /Game has no directory to read.");
      return path.join(this.contentDir, ...stripGamePrefix(normalized).split("/").filter(Boolean));
    }
    const plugin = this.resolvePluginPath(normalized);
    if (plugin) return plugin;

    const mounts = ["/Game", ...this.discoverPlugins().map((p) => p.mountPoint.replace(/\/$/, ""))];
    throw new Error(`Unknown mount '${normalized}'. This project mounts: ${mounts.join(", ")}.`);
  }

  /** The mount path of a package file on disk, or null when it is under no known mount. */
  mountPathFor(file: string): string | null {
    const normalized = file.replace(/\\/g, "/");
    const withoutExt = normalized.replace(/\.(uasset|umap)$/i, "");
    const under = (dir: string): number | null => {
      const root = dir.replace(/\\/g, "/").replace(/\/+$/, "");
      return normalized.toLowerCase().startsWith(`${root.toLowerCase()}/`) ? root.length + 1 : null;
    };
    if (this.contentDir) {
      const at = under(this.contentDir);
      if (at !== null) return `/Game/${withoutExt.slice(at)}`;
    }
    for (const plugin of this.discoverPlugins()) {
      const at = under(plugin.contentDir);
      if (at !== null) return `${plugin.mountPoint}${withoutExt.slice(at)}`;
    }
    return null;
  }

  /** Whether the .uproject enables UE plugin `name`; undefined when it cannot be read. */
  isUePluginEnabled(name: string): boolean | undefined {
    if (!this.projectPath) return undefined;
    try {
      const parsed = UProjectSchema.safeParse(JSON.parse(fs.readFileSync(this.projectPath, "utf-8")));
      if (!parsed.success) return undefined;
      const entry = parsed.data.Plugins?.find((p) => p.Name === name);
      return entry ? entry.Enabled !== false : false;
    } catch {
      return undefined;
    }
  }

  get projectDir(): string | null {
    return this.projectPath ? path.dirname(this.projectPath) : null;
  }

  get configDir(): string | null {
    return this.projectDir ? path.join(this.projectDir, "Config") : null;
  }

  get pluginsDir(): string | null {
    return this.projectDir ? path.join(this.projectDir, "Plugins") : null;
  }

  /**
   * Everything the shared engine resolver needs to place this project.
   *
   * One resolver answers "which engine" for engine plugins, engine-source reads
   * and the build tool, so a source build beside the project cannot be visible
   * to one of them and invisible to the next (#959, #962).
   */
  engineLookup(): EngineLookup {
    return {
      projectPath: this.projectPath,
      engineAssociation: this.engineAssociation,
      configBuildToolPath: this.config.editor?.buildToolPath ?? null,
      configEditorPath: this.config.editor?.path ?? null,
    };
  }

  /** The engine tree this project belongs to, or null. */
  resolveEngineRoot(): string | null {
    return resolveEngineRoot(this.engineLookup());
  }

  /**
   * Cache for discoverPlugins(). Engine-tree scans walk hundreds of dirs;
   * caching for the lifetime of the server is fine because plugin layouts
   * don't change while the editor is running.
   */
  private _pluginCache: PluginInfo[] | null = null;

  discoverPlugins(): PluginInfo[] {
    if (this._pluginCache) return this._pluginCache;
    const plugins: PluginInfo[] = [];
    const seen = new Set<string>();
    function scan(dir: string): void {
      let entries: fs.Dirent[];
      try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
      const hasUplugin = entries.some((f) => f.isFile() && f.name.endsWith(".uplugin"));
      if (hasUplugin) {
        const upluginEntry = entries.find((f) => f.isFile() && f.name.endsWith(".uplugin"))!;
        const pluginName = path.basename(upluginEntry.name, ".uplugin");
        const contentDir = path.join(dir, "Content");
        if (fs.existsSync(contentDir) && !seen.has(pluginName)) {
          seen.add(pluginName);
          plugins.push({ name: pluginName, contentDir, mountPoint: `/${pluginName}/` });
        }
        return; // don't recurse into a plugin directory
      }
      for (const entry of entries) {
        if (entry.isDirectory()) scan(path.join(dir, entry.name));
      }
    }
    if (this.pluginsDir && fs.existsSync(this.pluginsDir)) scan(this.pluginsDir);
    // Engine plugins (PCGBiomeCore, Niagara extras, etc.) - required for #253.
    // Through the shared resolver, so a source build beside the project has its
    // engine plugins found the same way an installed one does (#962).
    const engineRoot = this.resolveEngineRoot();
    if (engineRoot) {
      const enginePluginsRoot = path.join(engineRoot, "Engine", "Plugins");
      if (fs.existsSync(enginePluginsRoot)) scan(enginePluginsRoot);
    }
    this._pluginCache = plugins;
    return plugins;
  }

  /** The directory a plugin mount path names, matched case-insensitively as Unreal does. */
  resolvePluginPath(mountPath: string): string | null {
    const normalized = mountPath.replace(/\\/g, "/");
    const lower = normalized.toLowerCase();
    for (const plugin of this.discoverPlugins()) {
      const mount = plugin.mountPoint.toLowerCase();
      if (lower.startsWith(mount)) {
        const rest = normalized.slice(mount.length);
        return path.join(plugin.contentDir, ...rest.split("/").filter(Boolean));
      }
      if (lower === mount.replace(/\/$/, "")) return plugin.contentDir;
    }
    return null;
  }

  private parseUProject(): void {
    if (!this.projectPath) return;
    try {
      const raw = JSON.parse(fs.readFileSync(this.projectPath, "utf-8"));
      const parsed = UProjectSchema.safeParse(raw);
      if (!parsed.success) {
        warn("project", `.uproject at ${this.projectPath} did not match expected shape - engine association unknown`, parsed.error);
        this.engineAssociation = null;
        return;
      }
      this.engineAssociation = parsed.data.EngineAssociation ?? null;
    } catch (e) {
      warn("project", `.uproject at ${this.projectPath} was not valid JSON - engine association unknown`, e);
      this.engineAssociation = null;
    }
  }

  private loadConfig(): void {
    if (!this.projectDir) return;

    // One-time migrations:
    //   - .ue-mcp.json (pre-1.0.29) → ue-mcp.yml + ~/.ue-mcp/state.json
    //   - ue-mcp.local.yml (1.0.29 only) → ~/.ue-mcp/state.json
    //   - ue-mcp.feedback.mode in ue-mcp.yml (1.0.28-1.0.31) → user state
    // All idempotent no-ops once migrated.
    migrateLegacyJsonConfig(this.projectDir);
    migrateLegacyLocalYaml(this.projectDir);
    migrateLegacyFeedbackModeInYaml(this.projectDir);

    this.config = readUeMcpConfig(this.projectDir);
    if (Object.keys(this.config).length > 0) {
      info("project", `loaded config from ue-mcp.yml (merged with any global / env / local layers)`);
    }
  }
}

/**
 * Load the `ue-mcp:` block with the full layered cascade, each
 * layer deep-merged over the one before (low -> high precedence):
 *
 *     ~/.ue-mcp/config.yml   (user-global, untracked)
 *     <project>/ue-mcp.yml   (project, tracked)
 *     ue-mcp.{env}.yml       (env overlay, when UE_MCP_ENV is set)
 *     ue-mcp.local.yml       (per-machine, untracked)
 *
 * Env vars (UE_MCP_CONTEXT_STRATEGY, ...) still win over the merged result;
 * they are applied where each setting is consumed, not here. Machine *state*
 * (installedHooks, feedback mode) is not config and never merged - it lives in
 * ~/.ue-mcp/state.json.
 */
function loadLayeredUeMcpBlock(projectDir: string): Record<string, unknown> {
  const global = readGlobalUeMcpBlock();
  const project = readUeMcpBlock(projectConfigPath(projectDir));

  const layers: Record<string, unknown>[] = [global, project];
  for (const layer of configLayerFiles(projectDir, overlayFor(projectDir, global, project))) {
    if (layer.target === "env" || layer.target === "local") layers.push(readUeMcpBlock(layer.file));
  }

  return layers.reduce(
    (acc, layer) => deepMerge(acc, layer) as Record<string, unknown>,
    {} as Record<string, unknown>,
  );
}

/**
 * Which `ue-mcp.<env>.yml` overlay this project merges (#817).
 *
 * UE_MCP_ENV is one value for the whole process, so with more than one project
 * it puts every one of them on the same overlay. `env:` lets a project name its
 * own. The variable still wins, which keeps a single-project user on exactly
 * the behaviour they have.
 *
 * One caveat is worth a warning rather than silence: the editor plugin reads
 * `bridge.port` out of these same files, and it selects its overlay from
 * UE_MCP_ENV and nothing else. An overlay chosen by `env:` that pins the port
 * would leave the client connecting to one port while the editor bound
 * another.
 */
function overlayFor(
  projectDir: string,
  global: Record<string, unknown>,
  project: Record<string, unknown>,
): string | undefined {
  const fromVariable = firstString(readEnv("env"));
  if (fromVariable) return fromVariable;

  const local = readUeMcpBlock(localConfigPath(projectDir));
  const named = firstString(local.env, project.env, global.env);
  if (!named) return undefined;

  const overlay = readUeMcpBlock(overlayConfigPath(projectDir, named));
  if ((overlay.bridge as { port?: unknown } | undefined)?.port !== undefined) {
    warn(
      "project",
      `ue-mcp.${named}.yml pins bridge.port and that overlay is selected by 'env: ${named}' in this project's ` +
        `config. The editor plugin selects its overlay from UE_MCP_ENV only, so it will not read that pin and ` +
        `the two would use different ports. Move the pin into ue-mcp.yml or ue-mcp.local.yml, or select the ` +
        `overlay with UE_MCP_ENV.`,
    );
  }
  return named;
}

function firstString(...values: unknown[]): string | undefined {
  for (const v of values) {
    if (typeof v === "string" && v.trim() !== "") return v.trim();
  }
  return undefined;
}

/**
 * Migrate a legacy .ue-mcp.json (pre-1.0.29) into ue-mcp.yml +
 * ~/.ue-mcp/state.json. Project-level fields land in ue-mcp.yml's
 * `ue-mcp:` block (merging with any existing fields); `installedHooks`
 * goes into the user-state file keyed by absolute project root. Deletes
 * the JSON after a successful migration. Idempotent: no-op when the JSON
 * file is absent.
 */
function migrateLegacyJsonConfig(projectDir: string): void {
  const jsonPath = path.join(projectDir, ".ue-mcp.json");
  if (!fs.existsSync(jsonPath)) return;

  let legacy: Record<string, unknown>;
  try {
    legacy = JSON.parse(fs.readFileSync(jsonPath, "utf-8")) as Record<string, unknown>;
  } catch (e) {
    warn("project", `legacy .ue-mcp.json failed to parse during migration - leaving in place`, e);
    return;
  }

  const ymlPath = projectConfigPath(projectDir);
  const { installedHooks, ...tracked } = legacy as {
    installedHooks?: string[];
  } & Record<string, unknown>;

  // Tracked fields → ue-mcp.yml's `ue-mcp:` block.
  if (Object.keys(tracked).length > 0) {
    const existing = readConfigDoc(ymlPath, () => undefined);
    const existingBlock = (existing["ue-mcp"] as Record<string, unknown>) ?? {};
    existing["ue-mcp"] = { version: 1, ...existingBlock, ...tracked };
    writeConfigDoc(ymlPath, existing);
  }

  // installedHooks → ~/.ue-mcp/state.json under this project's key.
  if (Array.isArray(installedHooks) && installedHooks.length > 0) {
    setInstalledHooks(projectDir, installedHooks);
  }

  try {
    fs.unlinkSync(jsonPath);
    info(
      "project",
      `migrated legacy .ue-mcp.json → ue-mcp.yml${installedHooks?.length ? " + ~/.ue-mcp/state.json" : ""}`,
    );
  } catch (e) {
    warn("project", `migration wrote new files but couldn't delete .ue-mcp.json - remove it manually`, e);
  }
}

/**
 * Migrate a `ue-mcp.feedback.mode` entry from ue-mcp.yml (used 1.0.28-1.0.31)
 * into the user-state preferences. Feedback mode is a per-user-per-device
 * preference that doesn't belong in tracked project config. Strips the key
 * from the YAML after copying it. Idempotent.
 */
function migrateLegacyFeedbackModeInYaml(projectDir: string): void {
  const ymlPath = projectConfigPath(projectDir);
  if (!fs.existsSync(ymlPath)) return;

  let doc: Record<string, unknown>;
  try {
    doc = readConfigDoc(ymlPath);
  } catch {
    return;
  }
  const block = (doc["ue-mcp"] as Record<string, unknown> | undefined) ?? {};
  const feedback = block.feedback as { mode?: unknown } | undefined;
  if (!feedback || typeof feedback.mode !== "string") return;
  const mode = feedback.mode;
  if (mode !== "interactive" && mode !== "auto-approve" && mode !== "defer") return;

  setFeedbackMode(mode as FeedbackMode);

  // Strip from yaml.
  delete (block as Record<string, unknown>).feedback;
  doc["ue-mcp"] = block;
  writeConfigDoc(ymlPath, doc);

  info(
    "project",
    `migrated ue-mcp.feedback.mode="${mode}" from ue-mcp.yml → ~/.ue-mcp/state.json (preferences). Mode is now a per-user setting; run \`npx ue-mcp feedback mode\` to change it.`,
  );
}

/**
 * Extract the one machine-state key the brief 1.0.29 release wrote into
 * ue-mcp.local.yml (`installedHooks`) and move it to ~/.ue-mcp/state.json.
 *
 * ue-mcp.local.yml is NOT a dead file: it is now a supported per-machine
 * override layer (see loadLayeredUeMcpBlock). So this only strips the legacy
 * `installedHooks` key and PRESERVES any real override content the user has
 * put there. The file is deleted only when stripping leaves it empty (a
 * genuine 1.0.29 artifact). Idempotent: no-op when there is nothing to move.
 */
function migrateLegacyLocalYaml(projectDir: string): void {
  const localPath = localConfigPath(projectDir);
  if (!fs.existsSync(localPath)) return;

  let doc: Record<string, unknown>;
  try {
    doc = readConfigDoc(localPath);
  } catch (e) {
    warn("project", `ue-mcp.local.yml failed to parse during migration - leaving in place`, e);
    return;
  }
  const block = doc["ue-mcp"] as Record<string, unknown> | undefined;
  // No legacy machine-state key present: this is a live override file, leave it.
  if (!block || !Array.isArray(block.installedHooks) || block.installedHooks.length === 0) return;

  setInstalledHooks(projectDir, block.installedHooks as string[]);
  delete block.installedHooks;
  if (Object.keys(block).length === 0) delete doc["ue-mcp"];

  // Only a genuine 1.0.29 artifact (nothing but installedHooks) is now empty;
  // delete it. Anything else is a real override layer - rewrite it in place.
  if (Object.keys(doc).length === 0) {
    try {
      fs.unlinkSync(localPath);
      info("project", `migrated ue-mcp.local.yml installedHooks → ~/.ue-mcp/state.json and removed the empty legacy file`);
    } catch (e) {
      warn("project", `migrated installedHooks but couldn't delete the now-empty ue-mcp.local.yml - remove it manually`, e);
    }
    return;
  }
  try {
    writeConfigDoc(localPath, doc);
    info("project", `moved ue-mcp.local.yml installedHooks → ~/.ue-mcp/state.json (kept your other overrides in place)`);
  } catch (e) {
    warn("project", `couldn't rewrite ue-mcp.local.yml after stripping installedHooks`, e);
  }
}

/** /Game or anything under it, in any case: Unreal's mount names are case-insensitive. */
function isGamePath(p: string): boolean {
  const lower = p.toLowerCase();
  return lower === "/game" || lower.startsWith("/game/");
}

function stripGamePrefix(p: string): string {
  return isGamePath(p) ? p.slice("/game/".length) : p;
}
