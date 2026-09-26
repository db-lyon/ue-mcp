/**
 * The effect of an action nobody declared, guessed from its name.
 *
 * Every action this package ships declares its effect on its ActionSpec, and
 * the gates read that declaration (action-effects.ts). The one thing left for
 * a name to answer is a plugin action whose manifest gave no effect: plugin
 * injection and provision call inferActionEffect at load time and record the
 * answer as `inferred`.
 *
 *   read    observes. Landing it in the wrong editor changes nothing.
 *   mutate  may change the editor, its project on disk or its process, or has
 *           an external side effect.
 *   unknown the name settles nothing. Gated exactly like `mutate`.
 */
import { MUTATE_PREFIXES, READ_PREFIXES } from "./action-verbs.js";
import type { ActionEffect } from "./types.js";

/**
 * The same three values as `ActionEffect`, kept as a separate name because
 * this module answers about a NAME and that one is a declaration. An alias
 * rather than a parallel definition, so the two can never come to mean
 * different things.
 */
export type ActionClass = ActionEffect;

/** Where a classification came from: a verb the lexicon knows, or nothing. */
export type ActionClassSource = "lexicon" | "unresolved";

export interface ActionClassification {
  class: ActionClass;
  source: ActionClassSource;
}

/**
 * Verbs that mean "this call changes something".
 *
 * Seeded from the locking lexicon so the two cannot drift apart, then extended
 * with the verbs the action surface uses that locking never had to recognise
 * (locking only cares about asset writes; this also has to catch process
 * lifecycle, editor UI state, and anything with an effect outside the editor).
 *
 * Adding a verb here can only ever ask for an explicit target where one was not
 * required before. That is the safe direction, so this list is generous.
 */
const MUTATE_VERBS: ReadonlySet<string> = new Set<string>([
  ...MUTATE_PREFIXES,
  // Arbitrary code and arbitrary engine commands.
  "execute", "run", "invoke", "call", "eval",
  // Process and session lifecycle.
  "start", "stop", "restart", "quit", "kill", "launch", "request", "hot", "cook", "deploy",
  // Editor and viewport state a user would see happen in the wrong window.
  "open", "close", "focus", "select", "play", "simulate", "possess", "eject",
  "pause", "resume", "teleport", "respond", "stage", "capture", "render", "scrub",
  // Authoring verbs the asset lexicon has no reason to know about.
  "author", "edit", "place", "load", "reload", "force", "regenerate", "recompile",
  "rebuild", "reparent", "auto", "populate", "bind", "rebind", "unbind", "fill", "append",
  "reindex", "migrate", "override", "flush", "cleanup", "configure", "init",
  "initialize", "step", "go", "submit", "remap", "login", "logout", "sync",
  "toggle", "reactivate", "reorder", "wrap", "sculpt", "paint", "snap", "aim",
  "undo", "redo", "purge", "begin", "end", "lock", "unlock", "drop", "retarget",
  "install", "uninstall", "restore", "revert", "activate", "deactivate",
  // "destroy" is what the engine calls deleting an actor, and the asset
  // lexicon never needed it because assets are deleted rather than destroyed.
  "destroy",
  "trigger", "emit", "send", "post", "publish", "upload", "convert", "promote",
  "mark", "dirty", "refresh", "trim", "crop", "resize", "rotate", "translate", "nudge",
  "transform", "split", "merge", "patch", "seek", "mute", "unmute",
  // Verbs that name editing a graph, moving a control, driving the UI or
  // writing a file.
  "paste", "copy", "press", "type", "click", "drag", "hover", "scroll", "break",
  "arrange", "reposition", "layout", "commit", "zero", "tween", "key",
  "collapse", "expand", "change", "tag", "untag", "fix", "draw", "mirror",
  "blend", "empty", "hide", "show", "construct", "frame", "screenshot", "look",
]);

/**
 * Verbs that mean "this call only looks".
 *
 * Kept deliberately short. A wrong entry here is the dangerous direction: it
 * would let a mutation through untargeted. Anything ambiguous stays out and
 * falls through to `unknown`.
 */
const READ_VERBS: ReadonlySet<string> = new Set<string>([
  ...READ_PREFIXES,
  "is", "scan", "sample", "health", "diagnose", "compare", "query", "dump",
  "fetch", "lookup", "enumerate", "summarize", "measure", "audit",
]);

/** Split `category.action`, tolerating an action name that contains a dot. */
export function splitTaskName(taskName: string): { tool: string; action: string } {
  const i = taskName.indexOf(".");
  if (i < 0) return { tool: taskName, action: "" };
  return { tool: taskName.slice(0, i), action: taskName.slice(i + 1) };
}

/**
 * Classify one action name. A mutate verb in any underscore segment wins,
 * because `metasound_add_node` adds a node and `cue_get_graph` does not.
 */
export function classifyActionClass(action: string): ActionClassification {
  const segments = action.toLowerCase().split(/[._]/).filter(Boolean);
  if (segments.some((s) => MUTATE_VERBS.has(s))) return { class: "mutate", source: "lexicon" };
  // A read verb settles the question only from the FRONT of the name.
  // Elsewhere it is a noun as often as a verb (`wire_rvt_sample` wrote), and
  // reading it as a read would let a mutation through untargeted.
  if (READ_VERBS.has(segments[0] ?? "")) return { class: "read", source: "lexicon" };

  return { class: "unknown", source: "unresolved" };
}


/**
 * The lexicon's answer for a plugin action whose manifest declared no effect.
 * The category does not enter into it: only the name is read.
 */
export function inferActionEffect(_category: string, action: string): ActionEffect {
  return classifyActionClass(action).class;
}

/**
 * Does this class need the caller to name an editor?
 *
 * `unknown` is gated exactly like `mutate`. The whole point of keeping it as a
 * distinct label is that it is not a guess; the gate treats a thing that might
 * be a mutation as a mutation.
 */
export function requiresExplicitEditor(cls: ActionClass): boolean {
  return cls !== "read";
}
