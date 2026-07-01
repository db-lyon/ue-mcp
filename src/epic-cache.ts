import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import type { EpicCatalog } from "./epic-enrich.js";

/**
 * Disk cache for Epic's toolset catalog.
 *
 * The catalog only exists live when the editor is running, but the ue-mcp tool
 * surface should be stable and inspectable across sessions - including when the
 * editor is offline and for deterministic unit tests. So whenever we fetch the
 * live catalog we persist it here, and when the editor is not reachable at
 * startup we fall back to the cached copy to surface the same first-class Epic
 * actions. The cache lives under the project's Saved/ (gitignored in UE
 * projects), keyed implicitly by project since the enabled toolsets vary.
 */
export interface CatalogCacheEnvelope {
  savedAt: string;
  engineAssociation?: string | null;
  toolsetCount: number;
  catalog: EpicCatalog;
}

function cacheFile(projectDir?: string): string | null {
  if (!projectDir) return null;
  return path.join(projectDir, "Saved", "UE_MCP_Bridge", "epic-catalog.json");
}

/** Persist the live catalog. Best-effort: never throws. */
export function saveCatalogCache(
  projectDir: string | undefined,
  catalog: EpicCatalog,
  engineAssociation?: string | null,
): void {
  const file = cacheFile(projectDir);
  if (!file) return;
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const envelope: CatalogCacheEnvelope = {
      savedAt: new Date().toISOString(),
      engineAssociation: engineAssociation ?? null,
      toolsetCount: catalog?.toolsets?.length ?? 0,
      catalog,
    };
    fs.writeFileSync(file, JSON.stringify(envelope, null, 2));
  } catch {
    // Cache is an optimization; a write failure must never break startup.
  }
}

/** Load the cached catalog, or null if absent/unreadable. */
export function loadCatalogCache(projectDir?: string): EpicCatalog | null {
  const file = cacheFile(projectDir);
  if (!file || !fs.existsSync(file)) return null;
  try {
    const envelope = JSON.parse(fs.readFileSync(file, "utf-8")) as CatalogCacheEnvelope;
    return envelope?.catalog ?? null;
  } catch {
    return null;
  }
}

/**
 * Load the catalog snapshot baked into the shipped package
 * (assets/epic-catalog.snapshot.json). This is the deterministic default source
 * so the Epic tool surface appears on the very first startup, with no editor and
 * no prior cache, and matches the generated docs (same snapshot feeds both).
 * Returns null if the asset is missing (e.g. a dev build before the snapshot is
 * generated).
 */
export function loadBakedCatalog(): EpicCatalog | null {
  // dist/epic-cache.js -> package root is one level up; assets/ ships there.
  const here = path.dirname(fileURLToPath(import.meta.url));
  const candidates = [
    path.join(here, "..", "assets", "epic-catalog.snapshot.json"),
    path.join(here, "..", "..", "assets", "epic-catalog.snapshot.json"),
  ];
  for (const file of candidates) {
    if (!fs.existsSync(file)) continue;
    try {
      return JSON.parse(fs.readFileSync(file, "utf-8")) as EpicCatalog;
    } catch {
      return null;
    }
  }
  return null;
}
