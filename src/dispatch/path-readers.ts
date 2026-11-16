// @session independent: Pure readers of the paths a call's parameters name; no state.
/**
 * Builders for an action's `writes` declaration: which asset paths a call
 * writes, read off its parameters (see dispatch/written-paths.ts).
 *
 * A leaf module on purpose. Tool modules declare `writes` with these, and the
 * classifier that reads the declarations indexes the tool graph, so a tool
 * importing the classifier would load the graph while it is being built.
 */

/** `writes` for an action that writes the asset named by the first of these parameters it was given. */
export function writesParam(...keys: string[]): (p: Record<string, unknown>) => string[] {
  return (p) => {
    for (const key of keys) if (typeof p[key] === "string" && p[key] !== "") return [p[key] as string];
    return [];
  };
}

/** `writes` for an action whose list parameter holds one written asset per entry, under the first of `keys` an entry has. */
export function writesEntries(list: string, ...keys: string[]): (p: Record<string, unknown>) => string[] {
  const pick = writesParam(...keys);
  return (p) => (Array.isArray(p[list]) ? (p[list] as unknown[]) : [])
    .flatMap((entry) => (entry !== null && typeof entry === "object" ? pick(entry as Record<string, unknown>) : []));
}
