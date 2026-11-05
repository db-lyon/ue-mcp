/**
 * The verb lexicon: what an action's NAME suggests it does.
 *
 * This used to live in `locking.ts` and be imported from there by everything
 * else that wanted it, which made asset locking the accidental owner of a list
 * three unrelated gates depended on. It is a leaf module now, importing
 * nothing, for two reasons. The lists are shared by modules that also need to
 * read each other, and a lexicon that sits under `locking.ts` closes an import
 * cycle the moment locking wants to read a declaration. And the lexicon's job
 * has shrunk: an action's effect is DECLARED on its ActionSpec now, so these
 * verbs answer only for names that have no declaration to read (Epic's
 * runtime-injected engine tools, a plugin action whose manifest stayed silent,
 * a raw bridge method a handler calls that no action forwards to).
 *
 * A name-based answer is a guess. Every caller here records it as one.
 */

/**
 * Verbs that mean "this call only looks".
 *
 * Kept deliberately short. A wrong entry here is the dangerous direction: it
 * would let a mutation past a gate. Anything even slightly ambiguous is left
 * out, so it falls through to the unresolved answer, which is gated as a
 * mutation.
 */
export const READ_PREFIXES = [
  "list", "search", "read", "get", "describe", "reflect", "find", "has", "status",
  "exists", "inspect", "preview", "validate", "count", "resolve", "diff",
];

/**
 * Verbs that mean "this call changes an asset".
 *
 * Narrower than the mutation lexicon in `action-class.ts`, which extends this
 * one: asset locking only ever cared about asset writes, while a routing gate
 * also has to catch process lifecycle, editor UI state and anything with an
 * effect outside the editor.
 */
export const MUTATE_PREFIXES = [
  "create", "set", "add", "remove", "delete", "rename", "move", "duplicate",
  "import", "reimport", "save", "update", "connect", "disconnect", "spawn",
  "compile", "apply", "assign", "insert", "replace", "clear", "reset", "modify",
  "write", "recenter", "bulk", "batch", "attach", "detach", "enable", "disable",
  "bake", "generate", "build",
];

/**
 * Leading verbs that mean "this call destroys or overwrites something".
 *
 * A fallback for clients that ask whether to confirm a call, read only when an
 * action does not declare `destructive` itself. Deliberately small: a verb
 * that only sometimes destroys (set, move, rename) stays out, and an action
 * the list gets wrong declares the answer on its spec instead.
 */
export const DESTRUCTIVE_VERBS: ReadonlySet<string> = new Set([
  "delete", "remove", "clear", "discard", "revert", "reset", "purge", "wipe",
  "destroy", "drop", "overwrite", "replace", "restore", "uninstall",
]);

/** Qualifiers a name can lead with before its verb: `epic_delete_asset`, `bulk_remove_x`. */
const VERB_QUALIFIERS: ReadonlySet<string> = new Set(["epic", "bulk", "batch"]);

/** The verb an action name leads with, past any qualifier. */
export function leadingVerb(action: string): string {
  const segments = action.toLowerCase().split(/[._]/).filter(Boolean);
  return segments.find((s) => !VERB_QUALIFIERS.has(s)) ?? "";
}
