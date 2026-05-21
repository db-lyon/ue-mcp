import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { McpError, ErrorCode } from "./errors.js";
import { info, warn } from "./log.js";
import { UProjectSchema, UeMcpConfigSchema } from "./schemas.js";
import { findEngineInstall } from "./deployer.js";
import { setInstalledHooks } from "./user-state.js";

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
  /** Optional HTTP surface for flow.run (#144). Disabled by default. */
  http?: {
    enabled?: boolean;
    /** Default 7723. Bound to 127.0.0.1 only. */
    port?: number;
    /** Override bind host. Defaults to 127.0.0.1 — do not expose externally. */
    host?: string;
  };
  /** Feedback flow behavior. */
  feedback?: {
    mode?: "interactive" | "auto-approve" | "defer";
  };
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
    if (inputPath.endsWith(".uproject")) {
      this.projectPath = path.resolve(inputPath);
    } else {
      const files = fs
        .readdirSync(inputPath)
        .filter((f) => f.endsWith(".uproject"));
      if (files.length === 0) {
        throw new McpError(ErrorCode.NOT_FOUND, `No .uproject file found in ${inputPath}`);
      }
      this.projectPath = path.resolve(inputPath, files[0]);
    }

    this.projectName = path.basename(this.projectPath, ".uproject");
    this.contentDir = path.join(path.dirname(this.projectPath), "Content");
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

  getRelativeContentPath(absolutePath: string): string {
    if (!this.contentDir) return absolutePath;
    const normalized = absolutePath.replace(/\\/g, "/");
    const contentNorm = this.contentDir.replace(/\\/g, "/");
    if (normalized.startsWith(contentNorm)) {
      const relative = normalized.slice(contentNorm.length + 1).replace(".uasset", "");
      return "/Game/" + relative;
    }
    const pluginPath = this.getRelativePluginPath(absolutePath);
    if (pluginPath) return pluginPath;
    return absolutePath;
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
    // Engine plugins (PCGBiomeCore, Niagara extras, etc.) — required for #253.
    const engineRoot = findEngineInstall(this.engineAssociation);
    if (engineRoot) {
      const enginePluginsRoot = path.join(engineRoot, "Engine", "Plugins");
      if (fs.existsSync(enginePluginsRoot)) scan(enginePluginsRoot);
    }
    this._pluginCache = plugins;
    return plugins;
  }

  resolvePluginPath(mountPath: string): string | null {
    const plugins = this.discoverPlugins();
    const normalized = mountPath.replace(/\\/g, "/");
    for (const plugin of plugins) {
      if (normalized.startsWith(plugin.mountPoint)) {
        const rest = normalized.slice(plugin.mountPoint.length);
        return path.join(plugin.contentDir, ...rest.split("/").filter(Boolean));
      }
      if (normalized === `/${plugin.name}` || normalized === `/${plugin.name}/`) {
        return plugin.contentDir;
      }
    }
    return null;
  }

  getRelativePluginPath(absolutePath: string): string | null {
    const normalized = absolutePath.replace(/\\/g, "/");
    for (const plugin of this.discoverPlugins()) {
      const contentNorm = plugin.contentDir.replace(/\\/g, "/");
      if (normalized.startsWith(contentNorm)) {
        const relative = normalized.slice(contentNorm.length + 1).replace(".uasset", "");
        return plugin.mountPoint + relative;
      }
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
    // Both are idempotent no-ops once migrated.
    migrateLegacyJsonConfig(this.projectDir);
    migrateLegacyLocalYaml(this.projectDir);

    const block = readUeMcpBlock(path.join(this.projectDir, "ue-mcp.yml"));
    const parsed = UeMcpConfigSchema.safeParse(block);
    if (!parsed.success) {
      warn(
        "project",
        `ue-mcp.yml ue-mcp: block did not match expected shape - using defaults`,
        parsed.error,
      );
      return;
    }
    this.config = parsed.data;
    if (Object.keys(block).length > 0) {
      info("project", `loaded config from ue-mcp.yml`);
    }
  }
}

/**
 * Extract the `ue-mcp:` block from a YAML file. Returns {} if the file
 * doesn't exist, doesn't parse, or doesn't have the block. Caller validates.
 */
function readUeMcpBlock(filePath: string): Record<string, unknown> {
  if (!fs.existsSync(filePath)) return {};
  try {
    const raw = yaml.load(fs.readFileSync(filePath, "utf-8")) as
      | { "ue-mcp"?: Record<string, unknown> }
      | null
      | undefined;
    return (raw && typeof raw === "object" && raw["ue-mcp"] && typeof raw["ue-mcp"] === "object")
      ? (raw["ue-mcp"] as Record<string, unknown>)
      : {};
  } catch (e) {
    warn("project", `failed to parse ${filePath} - skipping ue-mcp: block from this file`, e);
    return {};
  }
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

  const ymlPath = path.join(projectDir, "ue-mcp.yml");
  const { installedHooks, ...tracked } = legacy as {
    installedHooks?: string[];
  } & Record<string, unknown>;

  // Tracked fields → ue-mcp.yml's `ue-mcp:` block.
  if (Object.keys(tracked).length > 0) {
    let existing: Record<string, unknown> = {};
    if (fs.existsSync(ymlPath)) {
      try {
        existing = (yaml.load(fs.readFileSync(ymlPath, "utf-8")) as Record<string, unknown>) ?? {};
      } catch {
        existing = {};
      }
    }
    const existingBlock = (existing["ue-mcp"] as Record<string, unknown>) ?? {};
    existing["ue-mcp"] = { version: 1, ...existingBlock, ...tracked };
    fs.writeFileSync(ymlPath, yaml.dump(existing, { indent: 2 }), "utf-8");
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
 * Migrate a ue-mcp.local.yml (only created by the brief 1.0.29 release)
 * into ~/.ue-mcp/state.json. Idempotent: no-op when absent.
 */
function migrateLegacyLocalYaml(projectDir: string): void {
  const localPath = path.join(projectDir, "ue-mcp.local.yml");
  if (!fs.existsSync(localPath)) return;

  let doc: Record<string, unknown> = {};
  try {
    doc = (yaml.load(fs.readFileSync(localPath, "utf-8")) as Record<string, unknown>) ?? {};
  } catch (e) {
    warn("project", `ue-mcp.local.yml failed to parse during migration - leaving in place`, e);
    return;
  }
  const block = (doc["ue-mcp"] as Record<string, unknown> | undefined) ?? {};
  const installedHooks = block.installedHooks;

  if (Array.isArray(installedHooks) && installedHooks.length > 0) {
    setInstalledHooks(projectDir, installedHooks as string[]);
  }

  try {
    fs.unlinkSync(localPath);
    info("project", `migrated ue-mcp.local.yml → ~/.ue-mcp/state.json`);
  } catch (e) {
    warn("project", `couldn't delete ue-mcp.local.yml after migration - remove it manually`, e);
  }
}

function isGamePath(p: string): boolean {
  return (
    p.startsWith("/Game/") || p.toLowerCase() === "/game"
  );
}

function stripGamePrefix(p: string): string {
  if (p.startsWith("/Game/")) return p.slice(6);
  if (p.toLowerCase() === "/game") return "";
  return p;
}
