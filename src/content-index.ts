/**
 * The project's assets, read off disk with no editor running (T16).
 *
 * T16 asks for the source index, the asset index and the analysis layers to be
 * servable offline. The source index and the analysis over it already are: they
 * read installed engine headers, which exist whether or not an editor does. The
 * asset side did not, because every asset action dispatches into the editor's
 * asset registry, and with the editor down "what is in /Game/Characters" had no
 * answer at all.
 *
 * A package file on disk answers a narrower question than the registry does,
 * and the narrower question is the one that gets asked first: does this asset
 * exist, what is under this folder, when was it last written. The class, the
 * asset tags and the dependency graph live in the registry and are not
 * recoverable from a file listing, so this reports what it can and says plainly
 * what it cannot rather than returning a thinner registry answer under the same
 * name.
 *
 * Mount points come from the same resolver the online path uses
 * (`ProjectContext`), so `/Game/` and a plugin's `/MyPlugin/` map to the same
 * directories offline as they do live.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import type { ProjectContext } from "./project.js";

/** What a package file is, from its extension. */
export type PackageKind = "asset" | "map";

export interface ContentEntry {
  /** The mount path an editor would call this package, e.g. /Game/Foo/Bar. */
  assetPath: string;
  name: string;
  kind: PackageKind;
  /** Absolute path of the package file on disk. */
  file: string;
  sizeBytes: number;
  modified: string;
}

export interface ContentListing {
  /** The mount path that was listed. */
  contentPath: string;
  /** The directory it resolved to. */
  directory: string;
  recursive: boolean;
  count: number;
  /** True when maxResults cut the walk short. */
  truncated: boolean;
  assets: ContentEntry[];
  note: string;
}

export interface ListContentOptions {
  /** A mount path: /Game, /Game/Characters, or a plugin's /MyPlugin. */
  contentPath?: string;
  recursive?: boolean;
  maxResults?: number;
  /** Case-insensitive substring the package name has to contain. */
  namePattern?: string;
}

const NOTE =
  "Read from the package files on disk, so it answers existence, layout, size and modified time. "
  + "The asset class, its registry tags and its dependencies are held by the editor's asset "
  + "registry and are not in the file, so asset(action='list') and asset(action='search') are "
  + "still the answer once an editor is running.";

/** Resolve a mount path to a directory, through the same resolver the live path uses. */
function resolveMount(project: ProjectContext, contentPath: string): string {
  const normalized = contentPath.replace(/\\/g, "/").replace(/\/+$/, "") || "/Game";
  if (!normalized.startsWith("/")) {
    throw new Error(
      `'contentPath' must be a mount path such as /Game or /Game/Characters (got '${contentPath}').`,
    );
  }
  const lower = normalized.toLowerCase();
  if (lower === "/game" || lower.startsWith("/game/")) {
    if (!project.contentDir) throw new Error("No project is loaded, so /Game has no directory to read.");
    const rest = normalized.slice("/Game".length).split("/").filter(Boolean);
    return path.join(project.contentDir, ...rest);
  }
  const plugin = project.resolvePluginPath(normalized);
  if (plugin) return plugin;

  const mounts = ["/Game", ...project.discoverPlugins().map((p) => p.mountPoint.replace(/\/$/, ""))];
  throw new Error(
    `Unknown mount '${normalized}'. This project mounts: ${mounts.join(", ")}.`,
  );
}

/** The mount path a package file has, or null when it is under no known mount. */
function mountPathFor(project: ProjectContext, file: string): string | null {
  const normalized = file.replace(/\\/g, "/");
  const withoutExt = normalized.replace(/\.(uasset|umap)$/i, "");
  if (project.contentDir) {
    const content = project.contentDir.replace(/\\/g, "/").replace(/\/+$/, "");
    if (normalized.toLowerCase().startsWith(`${content.toLowerCase()}/`)) {
      return `/Game/${withoutExt.slice(content.length + 1)}`;
    }
  }
  for (const plugin of project.discoverPlugins()) {
    const dir = plugin.contentDir.replace(/\\/g, "/").replace(/\/+$/, "");
    if (normalized.toLowerCase().startsWith(`${dir.toLowerCase()}/`)) {
      return `${plugin.mountPoint}${withoutExt.slice(dir.length + 1)}`;
    }
  }
  return null;
}

/**
 * List the packages under one mount path.
 *
 * `maxResults` stops the walk rather than trimming the result, so a huge
 * Content tree costs the caller a bounded amount of I/O and the answer says it
 * was cut short.
 */
export function listContent(project: ProjectContext, options: ListContentOptions = {}): ContentListing {
  project.ensureLoaded();
  const contentPath = (options.contentPath ?? "/Game").trim() || "/Game";
  const recursive = options.recursive !== false;
  const maxResults = Math.max(1, Math.min(options.maxResults ?? 1000, 20_000));
  const pattern = options.namePattern?.trim().toLowerCase();

  const directory = resolveMount(project, contentPath);
  if (!fs.existsSync(directory)) {
    throw new Error(
      `'${contentPath}' resolves to ${directory}, which does not exist. `
      + `Content folders that hold no packages are absent on disk even when the editor shows them.`,
    );
  }

  const assets: ContentEntry[] = [];
  let truncated = false;

  const walk = (dir: string): void => {
    if (truncated) return;
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      if (truncated) return;
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (recursive) walk(full);
        continue;
      }
      const match = /\.(uasset|umap)$/i.exec(entry.name);
      if (!match) continue;
      const name = entry.name.slice(0, entry.name.length - match[0].length);
      if (pattern && !name.toLowerCase().includes(pattern)) continue;
      const assetPath = mountPathFor(project, full);
      if (!assetPath) continue;
      let sizeBytes = 0;
      let modified = "";
      try {
        const stat = fs.statSync(full);
        sizeBytes = stat.size;
        modified = new Date(stat.mtimeMs).toISOString();
      } catch {
        // Removed between readdir and stat. Reported with what is known.
      }
      assets.push({
        assetPath,
        name,
        kind: match[1].toLowerCase() === "umap" ? "map" : "asset",
        file: full,
        sizeBytes,
        modified,
      });
      if (assets.length >= maxResults) {
        truncated = true;
        return;
      }
    }
  };
  walk(directory);

  return {
    contentPath,
    directory,
    recursive,
    count: assets.length,
    truncated,
    assets,
    note: NOTE,
  };
}
