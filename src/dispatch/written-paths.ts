// @session independent: Pure classification of a call against the declarations in the graph it is handed.
/**
 * Which asset content paths one call writes: the one rule asset locks, asset
 * leases and the source-control guard all read.
 *
 * A call whose declared effect is `read` writes nothing, whatever it was
 * handed. For any other call the written paths are, in order:
 *
 *   1. The action's own `writes` declaration, for batches whose paths sit
 *      inside nested entries and for the few actions whose spec cannot say
 *      which path is written. It replaces the rest.
 *   2. The generic keys (PATH_KEYS, PATH_ARRAY_KEYS), read off any call,
 *      including one this server carries no declaration for.
 *   3. The action's recorded parameter spec, read under each parameter's name
 *      and every alias the registry renames to it. See `specWrittenParams`.
 *
 * An action that runs steps (`steps`, or a raw method's `steps`) writes the
 * union of what its steps write. An action that manages asset coordination
 * itself (`assetCoordination`) writes nothing for this purpose: taking a lock
 * around asset(lock) would acquire a lock in order to acquire a lock.
 *
 * Only a content path counts: a value that starts with a single slash and a
 * mount name, not a class path under /Script and not a filesystem root a
 * `*Path` parameter naming a file on disk would start with.
 *
 * A source control operation (every `source_control_*` method) is classified
 * as writing nothing, whatever paths it names: it is the provider call a guard
 * would make, so intercepting it would check a file out before checking it
 * out, or before reverting or submitting it.
 *
 * A path none of the layers names is not guarded: the write then meets
 * Unreal's own read-only failure, which is visible and can be checked out by
 * hand. Naming a read input instead takes a lock nobody needed, so a spec'd
 * parameter known only ever to be read is left out (READ_REFERENCES).
 */
import { actionSpecOf, bridgeMethodEffect, bridgeMethodSpecs, rawMethodSteps, taskEffect } from "../surface/action-effects.js";
import type { ParamSpec } from "../surface/handler-spec.js";
import type { ActionSpec, ToolDef } from "../surface/tool-types.js";

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

export interface ActionClassification {
  /** Whether the call may change anything at all (its declared effect is not `read`). */
  mutates: boolean;
  /** Distinct asset paths this call would write (may be empty even when mutating). */
  paths: string[];
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
  "interfacePath", "levelPath", "materialPath", "meshPath", "meshPaths", "mirrorDataTablePath",
  "newInputActionPath", "newParentPath", "normalizationSetPath", "parentPath",
  "rvtPath", "rvtPaths", "soundWavePath", "sourceAnimationPath", "sourceAssetPath",
  "templatePath", "texturePath", "toolPath",
]);

/**
 * Leading segments that are not an asset: a class path under /Script, and the
 * filesystem roots a POSIX path starts with.
 */
const NOT_CONTENT = /^\/(?:Script|home|Users|tmp|var|mnt|opt|usr|etc|private|Volumes|root|srv|dev|proc)\//i;

/** True when the bridge method is a source control provider operation. */
export function isProviderOperation(method: string): boolean {
  return /^source_control_/.test(method);
}

/** A value that names a content path. */
function isContentPath(v: unknown): v is string {
  return typeof v === "string" && /^\/[A-Za-z0-9_]/.test(v) && !NOT_CONTENT.test(v);
}

function strArray(v: unknown): string[] {
  return Array.isArray(v) ? v.filter((x): x is string => typeof x === "string" && x.length > 0) : [];
}

/** A parameter's name and every alias the registry renames to it. */
function namesOf(param: ParamSpec): string[] {
  return [param.name, ...(param.aliases ?? [])];
}

/**
 * The spec'd parameters a mutating action writes, as the names they may arrive
 * under. An action whose spec names a primary asset (a parameter called, or
 * aliased as, one of the generic keys) writes that asset and its outputs, and
 * reads every other reference. An action with no primary asset writes each
 * asset reference it declares, less the READ_REFERENCES.
 */
export function specWrittenParams(params: readonly ParamSpec[]): string[] {
  const refs = params.filter((p) => ASSET_REFERENCE.test(p.name) && !NOT_AN_ASSET.test(p.name));
  const primary = params.filter((p) => namesOf(p).some((n) => GENERIC_KEYS.has(n)));
  const written = primary.length > 0
    ? [...primary, ...refs.filter((p) => OUTPUT_REFERENCE.test(p.name))]
    : refs.filter((p) => !READ_REFERENCES.has(p.name));
  return [...new Set(written.flatMap(namesOf))];
}

const writtenCache = new WeakMap<readonly ParamSpec[], string[]>();

function writtenNames(spec: ActionSpec | undefined): string[] {
  const params = spec?.paramSpec;
  if (!params) return [];
  let names = writtenCache.get(params);
  if (!names) {
    names = specWrittenParams(params);
    writtenCache.set(params, names);
  }
  return names;
}

/** The paths a mutating call writes, read off its parameters by its declaration (or by the generic keys alone). */
function pathsOf(spec: ActionSpec | undefined, params: Record<string, unknown>): string[] {
  if (spec?.writes) return dedupe(spec.writes(params).filter(isContentPath));
  const out: unknown[] = [];
  for (const key of PATH_KEYS) out.push(params[key]);
  for (const key of PATH_ARRAY_KEYS) out.push(...strArray(params[key]));
  for (const key of writtenNames(spec)) {
    const value = params[key];
    out.push(...(Array.isArray(value) ? value : [value]));
  }
  return dedupe(out.filter(isContentPath));
}

/**
 * Whether a `category.action` call may change anything, and which asset paths
 * it writes. Read by asset locking and asset leases.
 *
 * The mutation half is the action's DECLARED effect, and `unknown` counts: an
 * action whose effect its parameters decide may well write the asset it names,
 * and being wrong costs one serialised call rather than two agents writing the
 * same package. A name this server does not carry gets the same `mutate`
 * default every other gate gives it.
 *
 * An unextractable path still yields an empty list, so a declared mutation that
 * names no asset runs unlocked. That is what keeps this from locking the
 * world: the path, not the verdict, is the narrow part.
 */
export function classifyAction(
  taskName: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): ActionClassification {
  const spec = actionSpecOf(taskName, graph);
  if (spec?.assetCoordination) return { mutates: false, paths: [] };
  // A batch runs its steps as one game-thread job, so it holds the paths of
  // ALL of them for the whole job. A batch of reads takes nothing. With no
  // steps to read there is nothing to be more precise than the declaration,
  // and the empty path list keeps that from locking anything.
  if (spec?.steps) {
    const steps = spec.steps(params);
    if (steps.length === 0) return { mutates: spec.effect !== "read", paths: [] };
    const union = new Set<string>();
    let mutates = false;
    for (const step of steps) {
      const one = classifyAction(step.task, step.params, graph);
      if (!one.mutates) continue;
      mutates = true;
      for (const p of one.paths) union.add(p);
    }
    return { mutates, paths: [...union] };
  }
  if (taskEffect(taskName, graph).effect === "read") return { mutates: false, paths: [] };
  return { mutates: true, paths: pathsOf(spec, params) };
}

/**
 * Classify a BRIDGE call: the content paths a mutation will touch, or
 * `writes: false` when the call is not a guardable write. Read by the
 * source-control guard, which sits on the bridge and sees method names.
 */
export function classifyWrite(
  method: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): WriteClassification {
  if (isProviderOperation(method)) return { writes: false, contentPaths: [] };

  // A batch writes what its steps write, all of it: a source-control guard
  // checking out what a call will touch has to see every path before the job
  // starts, because once it starts nothing else runs until it is done.
  const steps = rawMethodSteps(method, params, graph);
  if (steps) {
    const contentPaths = dedupe(steps.flatMap((s) => classifyWrite(s.method, s.params, graph).contentPaths));
    return { writes: contentPaths.length > 0, contentPaths };
  }

  const specs = bridgeMethodSpecs(method, graph);
  const declared = specs.find((s) => s.writes) ?? specs.find((s) => s.paramSpec) ?? specs[0];
  // A declared read writes no content, whatever path it was handed. With the
  // parameters, so a wrapped engine tool is judged by which tool it is rather
  // than by the one method all of them share.
  if (!declared?.writes && bridgeMethodEffect(method, params, graph).effect === "read") {
    return { writes: false, contentPaths: [] };
  }
  const contentPaths = pathsOf(declared, params);
  return { writes: contentPaths.length > 0, contentPaths };
}

function dedupe<T>(xs: T[]): T[] {
  return [...new Set(xs)];
}
