/**
 * Write classification for the source-control guard.
 *
 * The guard needs to know, for a given bridge method + params, which asset
 * content paths the call is about to modify, so it can check them out (or refuse
 * on a human's lock) BEFORE the write reaches disk. This module is the single
 * place that encodes that knowledge.
 *
 * A method whose declared effect is `read` writes nothing, whatever it was
 * handed. For any other method the written paths are the union of three layers:
 *
 *   1. EXPLICIT extractors, for batches whose paths sit inside nested entries
 *      and for the few methods whose spec cannot say which path is written.
 *   2. The generic keys (PATH_KEYS, PATH_ARRAY_KEYS), read off any method,
 *      including one this server carries no spec for.
 *   3. The method's recorded handler spec (RECORDED_HANDLER_SPECS), read under
 *      each parameter's name and every alias the registry renames to it. See
 *      `specWrittenParams` for which spec'd parameters count as written.
 *
 * Classification returns UE content paths (e.g. "/Game/Foo"). Resolving those to
 * on-disk files, and deciding which already exist (modify -> checkout) versus do
 * not (create -> skip, it will be `p4 add`ed later), is the caller's job, so a
 * value that names no existing content file costs nothing.
 *
 * A path none of the layers names is not checked out: the write then meets
 * Unreal's own read-only failure, which is visible and can be checked out by
 * hand. Naming a read input instead takes a lock nobody needed, so a spec'd
 * parameter known only ever to be read is left out (READ_REFERENCES).
 */
import { bridgeMethodEffect } from "../surface/action-effects.js";
import type { HandlerSpecs, ParamSpec } from "../surface/handler-spec.js";
import { RECORDED_HANDLER_SPECS } from "../tools/specs/index.js";
import type { ToolDef } from "../core/types.js";

export interface WriteClassification {
  /**
   * Whether this call modifies named content, which is what a source-control
   * or path-policy guard cares about. False for a mutation that names no
   * content path, so it is NOT the same question as "does this change
   * anything".
   */
  writes: boolean;
  /** UE content paths the call modifies. Empty when nothing is guardable. */
  contentPaths: string[];
}

/** Single-value params that carry an asset content path. */
const PATH_KEYS = ["assetPath", "sourcePath", "destinationPath", "packagePath", "path"];
/** Array params that carry asset content paths. */
const PATH_ARRAY_KEYS = ["assetPaths", "sourcePaths", "destinationPaths"];
const GENERIC_KEYS = new Set([...PATH_KEYS, ...PATH_ARRAY_KEYS]);

/** A spec'd parameter named like an asset reference. */
const ASSET_REFERENCE = /Paths?$/;
/**
 * `*Path` parameters that address something other than a content asset: an
 * actor or component in a world, an outliner folder, a graph node or state,
 * a file on disk, a source subdirectory, or a filter list.
 */
const NOT_AN_ASSET = /^(actor|cameraActor|child|component|focusActor|folder|grid|instigator|node|object|state|targetState|newParentState|targetActor|world|file|json|csv|sub|exclude|keep)Paths?$/;
/** Written even beside a primary asset: the call creates or overwrites it. */
const OUTPUT_REFERENCE = /^(output|destination|backup)\w*Paths?$/;
/**
 * Asset references that every spec'd method declaring them only reads: the
 * operand of a boolean, the interface a Blueprint implements, the mesh or
 * material assigned, the parent a child is re-pointed at, and so on.
 */
const READ_REFERENCES = new Set([
  "animAssetPath", "animBlueprintClassPath", "animSequencePath", "attenuationPath",
  "chooserPath", "compatibleSkeletonPath", "compatibleSkeletonPaths", "concurrencyPath",
  "controlRigPath", "databasePath", "emitterPath", "enumPath", "graphPath",
  "interfacePath", "levelPath", "materialPath", "meshPath", "mirrorDataTablePath",
  "newInputActionPath", "newParentPath", "normalizationSetPath", "parentPath",
  "rvtPath", "rvtPaths", "soundWavePath", "sourceAnimationPath", "sourceAssetPath",
  "templatePath", "texturePath", "toolPath",
]);

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
 * Explicit extractors, keyed by bare bridge method name. They replace the other
 * two layers for their method.
 */
const EXPLICIT: Record<string, Extractor> = {
  // The edit session writes the sequence; the mesh, source animation and rig
  // it is built from are only read.
  begin_control_rig_edit: (p) => strArray([p.sequencePath]),
  // The bake writes its output asset and leaves the edit sequence alone.
  bake_control_rig_edit: (p) => strArray([p.outputAssetPath]),
  // Batch rename: each entry is {sourcePath, destinationPath} or {assetPath, newName}.
  bulk_rename_assets: (p) => {
    const out: string[] = [];
    for (const r of (Array.isArray(p.renames) ? p.renames : []) as Record<string, unknown>[]) {
      const s = str(r.sourcePath) ?? str(r.assetPath);
      if (s) out.push(s);
    }
    return out;
  },
  // Bulk property write: each descriptor owns one asset path.
  bulk_set_asset_properties: (p) => {
    const out: string[] = [];
    for (const item of (Array.isArray(p.items) ? p.items : []) as Record<string, unknown>[]) {
      const assetPath = str(item.assetPath);
      if (assetPath) out.push(assetPath);
    }
    return out;
  },
  // Batch mesh material assignment: the mesh being written is each entry's
  // assetPath. materialPath is only read, so it is not checked out.
  set_mesh_materials_batch: (p) => {
    const out: string[] = [];
    for (const a of (Array.isArray(p.assignments) ? p.assignments : []) as Record<string, unknown>[]) {
      const assetPath = str(a.assetPath);
      if (assetPath) out.push(assetPath);
    }
    return out;
  },
  // Batch DataAsset upsert: the target package is assembled from each item's
  // packagePath + name, and never touched at all under dryRun.
  bulk_upsert_data_assets: (p) => {
    if (p.dryRun === true) return [];
    const out: string[] = [];
    for (const item of (Array.isArray(p.items) ? p.items : []) as Record<string, unknown>[]) {
      const dir = str(item.packagePath);
      const name = str(item.name);
      if (dir && name) out.push(`${dir.replace(/\/+$/, "")}/${name}`);
    }
    return out;
  },
};

/** A parameter's name and every alias the registry renames to it. */
function namesOf(param: ParamSpec): string[] {
  return [param.name, ...(param.aliases ?? [])];
}

/**
 * The spec'd parameters a mutating method writes, as the names they may arrive
 * under. A method whose spec names a primary asset (a parameter called, or
 * aliased as, one of the generic keys) writes that asset and its outputs, and
 * reads every other reference. A method with no primary asset writes each
 * asset reference it declares, less the READ_REFERENCES.
 */
export function specWrittenParams(specs: HandlerSpecs, method: string): string[] {
  const spec = specs[method];
  if (!spec) return [];
  const refs = spec.params.filter((p) => ASSET_REFERENCE.test(p.name) && !NOT_AN_ASSET.test(p.name));
  const primary = spec.params.filter((p) => namesOf(p).some((n) => GENERIC_KEYS.has(n)));
  const written = primary.length > 0
    ? [...primary, ...refs.filter((p) => OUTPUT_REFERENCE.test(p.name))]
    : refs.filter((p) => !READ_REFERENCES.has(p.name));
  return [...new Set(written.flatMap(namesOf))];
}

const writtenCache = new Map<string, string[]>();

function recordedWrittenParams(method: string): string[] {
  let names = writtenCache.get(method);
  if (!names) {
    names = specWrittenParams(RECORDED_HANDLER_SPECS, method);
    writtenCache.set(method, names);
  }
  return names;
}

/**
 * Classify a bridge call. Returns the content paths a mutation will touch, or
 * `writes: false` when the call is not a guardable write.
 */
export function classifyWrite(
  method: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): WriteClassification {
  const explicit = EXPLICIT[method];
  if (explicit) {
    const contentPaths = dedupe(explicit(params));
    return { writes: contentPaths.length > 0, contentPaths };
  }

  // A declared read writes no content, whatever path it was handed. Anything
  // else is a candidate, whatever its name looks like. `unknown` is a
  // candidate for the same reason it gates like a mutation everywhere else.
  // With the parameters, so a wrapped engine tool is judged by which tool it
  // is rather than by the one method all of them share.
  if (bridgeMethodEffect(method, params, graph).effect === "read") {
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
  // Spec'd names carry actor labels and file paths too, so only a rooted
  // content path counts.
  for (const key of recordedWrittenParams(method)) {
    const value = params[key];
    for (const v of Array.isArray(value) ? strArray(value) : strArray([value])) {
      if (v.startsWith("/")) contentPaths.push(v);
    }
  }

  const deduped = dedupe(contentPaths);
  return { writes: deduped.length > 0, contentPaths: deduped };
}

function dedupe(xs: string[]): string[] {
  return [...new Set(xs)];
}
