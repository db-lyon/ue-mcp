import * as fs from "node:fs";
import * as path from "node:path";

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
    return absolutePath;
  }

  get projectDir(): string | null {
    return this.projectPath ? path.dirname(this.projectPath) : null;
  }

  get configDir(): string | null {
    return this.projectDir ? path.join(this.projectDir, "Config") : null;
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
