/**
 * Caller-controlled field selection on any result.
 *
 * Some reads on this surface are large by nature. `level(get_component_tree)`
 * on a character dumps every component with transforms, collision, materials
 * and tags; `asset(bulk_read_properties)` answers a library-wide question
 * across hundreds of assets. An agent that wanted one number out of either has
 * to pay for all of it, and the cost lands in a context window rather than on
 * a wire, so it is not recoverable later in the conversation.
 *
 * Every category therefore accepts two routing parameters:
 *
 *   select  keep only these paths
 *   omit    drop these paths
 *
 * Both are dotted paths, and both traverse arrays transparently: on a result
 * shaped `{components: [{name, transform: {...}}]}`, `components.name` keeps
 * the name of every component. That is the spelling an agent reaches for, and
 * requiring `components[].name` would mostly produce a silent empty result.
 *
 * A path that matches nothing is reported back rather than ignored, because
 * the failure it causes otherwise is a caller concluding a field is absent
 * from the data when it only misspelled the path.
 *
 * These are routing parameters, like `timeoutMs`: dispatch reads them and
 * strips them, so they can never reach a bridge method as an argument.
 */

/** Distinguishes "this node was filtered out" from a real `undefined` value. */
const DROP = Symbol("drop");

type Spec = string[];

function splitPath(path: string): Spec {
  // `components[].name` and `components.name` mean the same thing here: array
  // traversal is implicit, so the brackets are accepted and discarded rather
  // than being a second syntax that behaves differently.
  return path
    .replace(/\[\]/g, "")
    .split(".")
    .map((s) => s.trim())
    .filter((s) => s !== "");
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

/**
 * Keep only the requested paths.
 *
 * Filtering in one pass rather than picking each path and merging afterwards,
 * because two paths into the same array (`components.name` and
 * `components.class`) have to end up as one array of two-key objects, and
 * merging two independently picked arrays cannot produce that.
 */
function keepNode(value: unknown, specs: Spec[], matched: Set<string>, origins: string[]): unknown {
  // A spec that has run out of segments means this whole subtree was asked for.
  for (let i = 0; i < specs.length; i++) {
    if (specs[i].length === 0) {
      matched.add(origins[i]);
      return value;
    }
  }

  if (Array.isArray(value)) {
    const out: unknown[] = [];
    for (const entry of value) {
      const kept = keepNode(entry, specs, matched, origins);
      if (kept !== DROP) out.push(kept);
    }
    return out.length > 0 ? out : DROP;
  }

  if (isPlainObject(value)) {
    const out: Record<string, unknown> = {};
    for (const [key, child] of Object.entries(value)) {
      const tails: Spec[] = [];
      const tailOrigins: string[] = [];
      for (let i = 0; i < specs.length; i++) {
        if (specs[i][0] === key) {
          tails.push(specs[i].slice(1));
          tailOrigins.push(origins[i]);
        }
      }
      if (tails.length === 0) continue;
      const kept = keepNode(child, tails, matched, tailOrigins);
      if (kept !== DROP) out[key] = kept;
    }
    return Object.keys(out).length > 0 ? out : DROP;
  }

  // A scalar cannot satisfy a path that still has segments left.
  return DROP;
}

/** Drop the requested paths, keeping everything else. */
function dropNode(value: unknown, specs: Spec[], matched: Set<string>, origins: string[]): unknown {
  for (let i = 0; i < specs.length; i++) {
    if (specs[i].length === 0) {
      matched.add(origins[i]);
      return DROP;
    }
  }

  if (Array.isArray(value)) {
    const out: unknown[] = [];
    for (const entry of value) {
      const kept = dropNode(entry, specs, matched, origins);
      if (kept !== DROP) out.push(kept);
    }
    return out;
  }

  if (isPlainObject(value)) {
    const out: Record<string, unknown> = {};
    for (const [key, child] of Object.entries(value)) {
      const tails: Spec[] = [];
      const tailOrigins: string[] = [];
      for (let i = 0; i < specs.length; i++) {
        if (specs[i][0] === key) {
          tails.push(specs[i].slice(1));
          tailOrigins.push(origins[i]);
        }
      }
      if (tails.length === 0) {
        out[key] = child;
        continue;
      }
      const kept = dropNode(child, tails, matched, tailOrigins);
      if (kept !== DROP) out[key] = kept;
    }
    return out;
  }

  return value;
}

export interface FieldSelection {
  select?: string[];
  omit?: string[];
}

export interface ProjectionResult<T> {
  result: T;
  /** Requested paths that matched nothing, so a typo is visible. */
  notFound: string[];
  /** Whether anything was actually filtered. */
  applied: boolean;
}

/** Read the selection out of a parameter bag, accepting a bare string for one path. */
export function takeFieldSelection(
  params: Record<string, unknown>,
): { selection: FieldSelection; rest: Record<string, unknown> } {
  const { select, omit, ...rest } = params;
  const asList = (value: unknown): string[] | undefined => {
    if (typeof value === "string" && value.trim() !== "") return [value];
    if (Array.isArray(value)) {
      const out = value.filter((v): v is string => typeof v === "string" && v.trim() !== "");
      return out.length > 0 ? out : undefined;
    }
    return undefined;
  };
  return { selection: { select: asList(select), omit: asList(omit) }, rest };
}

/**
 * Apply a selection to a result.
 *
 * `select` runs before `omit`, so asking to keep a subtree and drop one field
 * inside it does what it reads like. A selection that would empty the result
 * entirely is NOT applied: handing back `{}` with no explanation is worse than
 * handing back the data, and the unmatched paths say what went wrong.
 */
export function projectResult<T>(result: T, selection: FieldSelection): ProjectionResult<T> {
  const { select, omit } = selection;
  if ((!select || select.length === 0) && (!omit || omit.length === 0)) {
    return { result, notFound: [], applied: false };
  }
  if (result === null || typeof result !== "object") {
    return { result, notFound: [...(select ?? []), ...(omit ?? [])], applied: false };
  }

  const matched = new Set<string>();
  let value: unknown = result;

  if (select && select.length > 0) {
    const specs = select.map(splitPath);
    const kept = keepNode(value, specs, matched, select);
    value = kept === DROP ? {} : kept;
  }
  if (omit && omit.length > 0) {
    const specs = omit.map(splitPath);
    const dropped = dropNode(value, specs, matched, omit);
    value = dropped === DROP ? {} : dropped;
  }

  const requested = [...(select ?? []), ...(omit ?? [])];
  const notFound = requested.filter((path) => !matched.has(path));

  // Nothing matched at all: the caller almost certainly misspelled every path,
  // and returning an empty object would look like an empty answer from the
  // editor rather than a filter that did not fit.
  if (matched.size === 0) {
    return { result, notFound, applied: false };
  }

  return { result: value as T, notFound, applied: true };
}

/**
 * Attach the report of unmatched paths, so a misspelling is visible rather
 * than reading as absent data.
 */
export function attachFieldReport<T>(result: T, projection: ProjectionResult<T>): T {
  if (projection.notFound.length === 0) return result;
  if (result === null || typeof result !== "object" || Array.isArray(result)) return result;
  const record = result as Record<string, unknown>;
  if ("fieldsNotFound" in record) return result;
  return {
    ...record,
    fieldsNotFound: {
      paths: projection.notFound,
      note: projection.applied
        ? "These select/omit paths matched nothing in the result and were ignored."
        : "No select/omit path matched anything, so the unfiltered result is returned.",
    },
  } as T;
}
