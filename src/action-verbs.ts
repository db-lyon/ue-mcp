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
 * The leading verb of an action or bridge-method name.
 *
 * "bulk" and "batch" describe the SHAPE of a call, not what it does, and both
 * are in `MUTATE_PREFIXES` because every batch action written so far was a
 * write. `bulk_read_properties` is not. When the shape word is followed by a
 * read verb, that verb is the answer.
 */
export function leadingVerb(name: string): string {
  const segments = String(name).toLowerCase().split(/[._]/);
  const first = segments[0] ?? "";
  if ((first === "bulk" || first === "batch") && segments[1] && READ_PREFIXES.includes(segments[1])) {
    return segments[1];
  }
  return first;
}

/** Whether a name's leading verb is a known read. */
export function leadsWithReadVerb(name: string): boolean {
  return READ_PREFIXES.includes(leadingVerb(name));
}
