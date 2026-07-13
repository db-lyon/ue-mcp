/**
 * Write classification for the source-control guard.
 *
 * The guard needs to know, for a given bridge method + params, which asset
 * content paths the call is about to modify, so it can check them out (or refuse
 * on a human's lock) BEFORE the write reaches disk. This module is the single
 * place that encodes that knowledge.
 *
 * Two layers:
 *   1. An explicit map for methods whose path lives under an unusual param
 *      shape (batches, rename descriptors).
 *   2. A verb+param heuristic for the common case: a method named with a write
 *      verb (`save_`, `set_`, ...) carrying a recognizable asset-path param.
 *
 * Classification returns UE content paths (e.g. "/Game/Foo"). Resolving those to
 * on-disk files, and deciding which already exist (modify -> checkout) versus do
 * not (create -> skip, it will be `p4 add`ed later), is the caller's job.
 *
 * Design note: this is intentionally conservative. An unmapped write falls
 * through as "not a write" (fail-open). The cost of a miss is that the agent
 * hits Unreal's own read-only failure and can check out explicitly - visible,
 * not silent corruption. The cost of a false positive is an unnecessary
 * checkout, which takes a lock. We bias toward not over-locking.
 */

export interface WriteClassification {
  writes: boolean;
  /** UE content paths the call modifies. Empty when nothing is guardable. */
  contentPaths: string[];
}

/** Method-name prefixes that denote a mutation. Read verbs are excluded. */
const WRITE_VERB =
  /^(save|set|create|add|delete|remove|import|rename|move|duplicate|reparent|compile|apply|assign|modify|bake|generate|build)_/;

/** Single-value params that carry an asset content path. */
const PATH_KEYS = ["assetPath", "sourcePath", "destinationPath", "packagePath", "path"];
/** Array params that carry asset content paths. */
const PATH_ARRAY_KEYS = ["assetPaths", "sourcePaths", "destinationPaths"];

type Extractor = (params: Record<string, unknown>) => string[];

/** Coerce a param to a non-empty string, or null. */
function str(v: unknown): string | null {
  return typeof v === "string" && v.length > 0 ? v : null;
}

/** Coerce a param to an array of non-empty strings. */
function strArray(v: unknown): string[] {
  return Array.isArray(v) ? v.filter((x): x is string => typeof x === "string" && x.length > 0) : [];
}

/**
 * Explicit extractors for methods whose target path is not a plain PATH_KEYS
 * lookup. Keyed by bare bridge method name.
 */
const EXPLICIT: Record<string, Extractor> = {
  // Batch rename: each entry is {sourcePath, destinationPath} or {assetPath, newName}.
  bulk_rename_assets: (p) => {
    const out: string[] = [];
    for (const r of (Array.isArray(p.renames) ? p.renames : []) as Record<string, unknown>[]) {
      const s = str(r.sourcePath) ?? str(r.assetPath);
      if (s) out.push(s);
    }
    return out;
  },
  // Batch delete.
  delete_asset_batch: (p) => strArray(p.assetPaths),
};

/**
 * Classify a bridge call. Returns the content paths a mutation will touch, or
 * `writes: false` when the call is not a guardable write.
 */
export function classifyWrite(method: string, params: Record<string, unknown>): WriteClassification {
  const explicit = EXPLICIT[method];
  if (explicit) {
    const contentPaths = dedupe(explicit(params));
    return { writes: contentPaths.length > 0, contentPaths };
  }

  if (!WRITE_VERB.test(method)) {
    return { writes: false, contentPaths: [] };
  }

  const contentPaths: string[] = [];
  for (const key of PATH_KEYS) {
    const v = str(params[key]);
    if (v) contentPaths.push(v);
  }
  for (const key of PATH_ARRAY_KEYS) {
    contentPaths.push(...strArray(params[key]));
  }

  const deduped = dedupe(contentPaths);
  return { writes: deduped.length > 0, contentPaths: deduped };
}

function dedupe(xs: string[]): string[] {
  return [...new Set(xs)];
}
