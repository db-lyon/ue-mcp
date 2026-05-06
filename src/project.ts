import * as fs from "node:fs";
import * as path from "node:path";
import { McpError, ErrorCode } from "./errors.js";
import { info, warn } from "./log.js";
import { UProjectSchema, UeMcpConfigSchema } from "./schemas.js";
import { findEngineInstall } from "./deployer.js";

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
    const configPath = path.join(this.projectDir, ".ue-mcp.json");
    if (!fs.existsSync(configPath)) return;
    try {
      const raw = JSON.parse(fs.readFileSync(configPath, "utf-8"));
      const parsed = UeMcpConfigSchema.safeParse(raw);
      if (!parsed.success) {
        warn("project", `.ue-mcp.json at ${configPath} did not match expected shape - using defaults`, parsed.error);
        return;
      }
      this.config = parsed.data;
      info("project", `loaded config from ${configPath}`);
    } catch (e) {
      warn("project", `failed to parse .ue-mcp.json at ${configPath} - using defaults`, e);
    }
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
