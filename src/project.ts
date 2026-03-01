import * as fs from "node:fs";
import * as path from "node:path";

export interface PluginInfo {
  name: string;
  contentDir: string;
  mountPoint: string;
}

export class ProjectContext {
  projectPath: string | null = null;
  projectName: string | null = null;
  contentDir: string | null = null;
  engineAssociation: string | null = null;

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
        throw new Error(`No .uproject file found in ${inputPath}`);
      }
      this.projectPath = path.resolve(inputPath, files[0]);
    }

    this.projectName = path.basename(this.projectPath, ".uproject");
    this.contentDir = path.join(path.dirname(this.projectPath), "Content");
    this.parseUProject();
  }

  ensureLoaded(): void {
    if (!this.isLoaded) {
      throw new Error(
        'No project loaded. Pass the .uproject path as an argument in your MCP config, e.g. "args": ["C:/path/to/MyGame.uproject"]',
      );
    }
  }

  resolveContentPath(assetPath: string): string {
    this.ensureLoaded();
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

  discoverPlugins(): PluginInfo[] {
    if (!this.pluginsDir || !fs.existsSync(this.pluginsDir)) return [];
    const plugins: PluginInfo[] = [];
    function scan(dir: string): void {
      for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue;
        const full = path.join(dir, entry.name);
        const contentDir = path.join(full, "Content");
        const hasUplugin = fs.readdirSync(full).some((f) => f.endsWith(".uplugin"));
        if (hasUplugin && fs.existsSync(contentDir)) {
          plugins.push({
            name: entry.name,
            contentDir,
            mountPoint: `/${entry.name}/`,
          });
        } else if (!hasUplugin) {
          scan(full);
        }
      }
    }
    scan(this.pluginsDir);
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
      const json = JSON.parse(fs.readFileSync(this.projectPath, "utf-8"));
      this.engineAssociation = json.EngineAssociation ?? null;
    } catch {
      this.engineAssociation = null;
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
