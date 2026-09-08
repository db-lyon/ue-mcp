/**
 * Backslash repair on path parameters, with the repair reported back.
 *
 * Unreal addresses content with forward slashes, and so does every bridge
 * handler. An agent running on Windows does not reliably produce them: it
 * writes `\Game\UI\WBP_Menu` or `/Game/UI\WBP_Menu` because that is what the
 * surrounding shell, the file explorer and half its own training data look
 * like. The bridge then fails to resolve an asset that is right there, and the
 * error says the asset does not exist, which sends the caller looking for the
 * wrong problem.
 *
 * Repairing it at the boundary is cheap. Doing it silently is not: a caller
 * that never learns its paths are wrong keeps emitting them, and a repair that
 * guessed wrong would be invisible. So every repair is reported alongside the
 * result under `pathsRepaired`.
 *
 * Two shapes are deliberately left alone, because rewriting them would break a
 * path that was already correct:
 *
 *   UNC paths      `\\server\share\x` - the leading pair is the syntax
 *   escape-looking `a\nb` and friends are not touched either, since only
 *                  parameters whose NAME says they hold a path are considered
 *
 * A Windows drive path (`C:\Users\...`) is repaired, because forward slashes
 * are accepted everywhere it can be used: Node's fs layer, UBT, and Unreal's
 * own file APIs all take them.
 */

/**
 * Does this parameter name hold a path?
 *
 * Matched on the trailing word rather than a substring, so `focusDirection`
 * and `pathFilter` are not mistaken for paths: the first ends in `Direction`
 * and the second is a filter over paths rather than one.
 */
export function isPathParam(name: string): boolean {
  return /(?:^|[a-z0-9])(?:[Pp]ath|[Pp]aths|[Dd]ir|[Dd]irectory|[Ff]ile|[Ff]older)$/.test(name);
}

/** Would this value be changed by the repair? */
function needsRepair(value: unknown): value is string {
  if (typeof value !== "string" || !value.includes("\\")) return false;
  // A UNC path's leading pair is load-bearing syntax, not a mistake.
  if (value.startsWith("\\\\")) return false;
  return true;
}

function repair(value: string): string {
  return value.replace(/\\/g, "/");
}

export interface PathRepair {
  param: string;
  from: string;
  to: string;
}

/**
 * Fold backslashes out of the path parameters of one call.
 *
 * Returns a new parameter bag when anything changed and the original object
 * when nothing did, so a clean call allocates nothing and is byte-identical to
 * what it was before this existed.
 */
export function normalizePathParams(
  params: Record<string, unknown>,
): { params: Record<string, unknown>; repairs: PathRepair[] } {
  const repairs: PathRepair[] = [];
  let out: Record<string, unknown> | null = null;

  const set = (key: string, value: unknown): void => {
    out ??= { ...params };
    out[key] = value;
  };

  for (const [key, value] of Object.entries(params)) {
    if (!isPathParam(key)) continue;

    if (needsRepair(value)) {
      const fixed = repair(value);
      repairs.push({ param: key, from: value, to: fixed });
      set(key, fixed);
      continue;
    }

    // `assetPaths`, `materialPaths` and friends hold a list of the same thing.
    if (Array.isArray(value) && value.some(needsRepair)) {
      const fixed = value.map((entry) => {
        if (!needsRepair(entry)) return entry;
        const to = repair(entry);
        repairs.push({ param: key, from: entry, to });
        return to;
      });
      set(key, fixed);
    }
  }

  return { params: out ?? params, repairs };
}

/**
 * Attach the repairs to a result the caller will read.
 *
 * Only a plain object gets the field: an array or a scalar result has no place
 * to put it that would not change its shape, and silently reshaping a result
 * to carry a warning is a worse trade than not warning. A result that already
 * has the field is left alone rather than overwritten, since the handler's own
 * account of what it repaired is the more specific one.
 */
export function attachPathRepairs<T>(result: T, repairs: PathRepair[]): T {
  if (repairs.length === 0) return result;
  if (result === null || typeof result !== "object" || Array.isArray(result)) return result;
  const record = result as Record<string, unknown>;
  if ("pathsRepaired" in record) return result;

  // A directive response is an envelope: the caller reads `.result`, so the
  // repair belongs on the payload rather than beside the instruction. Checked
  // structurally because types.ts imports this module, and importing the
  // guard back from there would close a cycle.
  if (record.__directive === true && "result" in record) {
    return { ...record, result: attachPathRepairs(record.result, repairs) } as T;
  }

  return {
    ...record,
    pathsRepaired: {
      note:
        "Backslashes were replaced with forward slashes in these parameters. "
        + "Unreal addresses content with forward slashes; send them that way to avoid the repair.",
      repairs,
    },
  } as T;
}
