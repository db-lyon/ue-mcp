/**
 * Read / mutate / unknown classification of the action surface (#817, plan 5.1).
 *
 * This exists for one decision: while this server drives more than one editor,
 * a call that does not name its target falls through to the active session. For
 * a read that is harmless. For anything that changes an editor, its project on
 * disk, or its process, a wrong guess edits somebody else's project, so those
 * calls have to name the editor they mean.
 *
 * Why this is not `flow/write-methods.ts` or `locking.ts`:
 *
 *   - `classifyWrite` (source-control guard) answers "which content paths is
 *     this call about to write", and returns `writes: false` whenever it cannot
 *     extract a path. A call with no asset path in its params is still a
 *     mutation of the editor.
 *   - `classifyAction` (asset locking) answers "should this call take a lock",
 *     and deliberately collapses everything it does not recognise into
 *     `mutates: false` so locking never blocks a call it cannot classify.
 *
 * Both are load-bearing where they are and both fail open, which is the exact
 * opposite of what a routing gate needs. So this module answers a third
 * question, three-way, and fails closed. It is not a parallel scheme: the verb
 * lexicon is `locking.ts`'s, imported and extended rather than restated, and a
 * test asserts this module never contradicts locking's verdict.
 *
 * The taxonomy is stated in terms of the ADDRESSED EDITOR:
 *
 *   read    observes. Does not change the editor, its project on disk, or its
 *           process. Landing one in the wrong editor returns the wrong answer
 *           and changes nothing.
 *   mutate  may change any of those three, or has an external side effect
 *           (writes a file, posts an issue, launches or quits a process).
 *   unknown cannot be determined from the action alone, because what it does is
 *           decided by a parameter (an arbitrary python string, a wrapped tool
 *           name). Gated exactly like `mutate`.
 *
 * Unclassifiable is a real answer here, not a shrug: an `unknown` action still
 * requires an explicit target, so the honest label costs nothing at the gate
 * and stops a guess from being recorded as fact.
 */
import { MUTATE_PREFIXES, READ_PREFIXES } from "./locking.js";

export type ActionClass = "read" | "mutate" | "unknown";

/** Where a classification came from. `unresolved` is what the drift guard fails on. */
export type ActionClassSource = "override" | "lexicon" | "epic-default" | "unresolved";

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
  "pause", "resume", "teleport", "respond", "stage", "capture", "render",
  // Authoring verbs the asset lexicon has no reason to know about.
  "author", "edit", "place", "load", "reload", "force", "regenerate", "recompile",
  "rebuild", "reparent", "auto", "populate", "bind", "rebind", "fill", "append",
  "reindex", "migrate", "override", "flush", "cleanup", "configure", "init",
  "initialize", "step", "go", "submit", "remap", "login", "logout", "sync",
  "toggle", "reactivate", "reorder", "wrap", "sculpt", "paint", "snap", "aim",
  "undo", "redo", "purge", "begin", "end", "lock", "unlock", "drop", "retarget",
  "install", "uninstall", "restore", "revert", "activate", "deactivate",
  "trigger", "emit", "send", "post", "publish", "upload", "convert", "promote",
  "mark", "dirty", "refresh", "trim", "crop", "resize", "rotate", "translate",
  "transform", "split", "merge", "patch", "seek", "mute", "unmute",
  // Verbs the wrapped engine surface uses that no native action does. Every one
  // of these names a tool that edits a graph, moves a control, drives the UI, or
  // writes a file, and without them the Epic default below would let it through
  // untargeted.
  "paste", "copy", "press", "type", "click", "drag", "hover", "scroll", "break",
  "arrange", "reposition", "layout", "commit", "zero", "tween", "key",
  "collapse", "expand", "change", "tag", "untag", "fix", "draw", "mirror",
  "blend", "empty", "hide", "show", "construct", "frame", "screenshot", "look",
]);

/**
 * Verbs that mean "this call only looks".
 *
 * Kept deliberately short. A wrong entry here is the dangerous direction: it
 * would let a mutation through untargeted. Anything even slightly ambiguous
 * gets an explicit override below instead of a verb.
 */
const READ_VERBS: ReadonlySet<string> = new Set<string>([
  ...READ_PREFIXES,
  "is", "scan", "sample", "health", "diagnose", "compare", "query", "dump",
  "fetch", "lookup", "enumerate",
]);

/**
 * Actions the verb lexicon gets wrong, or cannot see.
 *
 * Every entry is a decision about one action, so every entry carries the reason
 * it is not what the verb says. Keyed `category.action`.
 */
const OVERRIDES: Readonly<Record<string, ActionClass>> = {
  // ── Address the server's session registry, never an editor ──────────
  // Routing cannot land these in the wrong editor: they do not reach a bridge
  // and they name their subject in their own parameters. Requiring an `editor`
  // target for "register an editor" would be a riddle, not a safeguard.
  "project.list_editors": "read",
  "project.use_editor": "read",
  "project.add_editor": "read",
  "project.drop_editor": "read",

  // Renders a flow's execution plan without running a step of it. `flow.run`
  // is a mutation by the lexicon, which is correct: a flow is whatever its
  // steps are.
  "flow.plan": "read",

  // ── Arbitrary payload decides the effect ────────────────────────────
  "epic.call_tool": "unknown",
  "editor.execute_python": "mutate",
  "project.execute_python_report": "mutate",
  "editor.execute_command": "mutate",
  "editor.invoke_object_function": "unknown",
  "editor.invoke_object_functions": "unknown",
  "editor.invoke_static_function": "unknown",
  "editor.invoke_function": "unknown",
  "widget.invoke_runtime_function": "unknown",
  "niagara.batch": "mutate",

  // ── Reads whose verb reads as a write ───────────────────────────────
  // Serializers: they render a graph as text or JSON and return it.
  "blueprint.export_nodes_t3d": "read",
  "material.export_graph": "read",
  "pcg.export_graph": "read",
  // Reads the actor descriptors of an unloaded World Partition level. Nothing
  // is opened; `level.load` is the action that changes the open map.
  "level.load_actor_descs": "read",
  // Extracts a widget subtree as a description. `widget.author` writes one.
  "widget.extract_subtree": "read",
  // Cache metadata about the Fab library, not a cache write.
  "fab.cache_info": "read",
  // Reads a mirror data table. "mirror" is a mutate verb because the wrapped
  // engine surface uses it as one (mirror_selected_controls), and a mutate verb
  // anywhere in the name wins, which is wrong for exactly this action.
  "animation.read_mirror_data_table": "read",
  // Reads Control Rig controls and keys from an edit session. The `edit`
  // segment names the session type; this action does not modify it.
  "animation.read_control_rig_edit": "read",
  // Reads animation data, but optionally writes validation artifacts under
  // the addressed project's Saved directory.
  "animation.analyze_animation": "unknown",
  // Reads whose first segment is not a verb at all.
  "level.line_trace": "read",
  "level.nav_project_point": "read",
  "level.count_actors_by_class": "read",
  "gameplay.project_to_nav": "read",
  "gameplay.find_nav_path": "read",
  "editor.hit_test_viewport_pixel": "read",
  "editor.check_for_crashes": "read",
  "editor.search_log": "read",
  "project.live_coding_status": "read",
  "feedback.route": "read",
  "epic.status": "read",
  "fab.status": "read",

  // ── Writes whose verb reads as a read ───────────────────────────────
  // Drives the editor's preview: it plays an animation in a viewport.
  "animation.preview_animation": "mutate",
  // Renders to a file on disk.
  "material.render_preview": "mutate",
  "asset.export": "mutate",
  "asset.export_texture": "mutate",
  "level.export_actor_fbx": "mutate",
  "audio.extract_pcm": "mutate",
  // Rebuilds the full-text index in the editor's own database.
  "asset.reindex_fts": "mutate",
  // Posts to a public issue tracker.
  "feedback.submit": "mutate",
};

/** Split `category.action`, tolerating an action name that contains a dot. */
export function splitTaskName(taskName: string): { tool: string; action: string } {
  const i = taskName.indexOf(".");
  if (i < 0) return { tool: taskName, action: "" };
  return { tool: taskName.slice(0, i), action: taskName.slice(i + 1) };
}

/**
 * Classify one action.
 *
 * Order: explicit override, then the verb lexicon over every underscore
 * segment (a mutate verb anywhere wins, because `metasound_add_node` adds a
 * node and `cue_get_graph` does not), then the Epic default, then unresolved.
 */
export function classifyActionClass(tool: string, action: string): ActionClassification {
  const key = `${tool}.${action}`;
  const override = OVERRIDES[key];
  if (override) return { class: override, source: "override" };

  const segments = action.toLowerCase().split(/[._]/).filter(Boolean);
  if (segments.some((s) => MUTATE_VERBS.has(s))) return { class: "mutate", source: "lexicon" };
  if (segments.some((s) => READ_VERBS.has(s))) return { class: "read", source: "lexicon" };

  // Epic's wrapped engine tools are injected at runtime from a live catalog, so
  // no build-time guard can enumerate them: enrichment can invent whole
  // categories from a toolset this package has never seen. The surface is
  // overwhelmingly read-shaped and a wrapped tool that writes almost always
  // says so in its name, which the lexicon above already caught. Defaulting the
  // remainder to `unknown` would make every unbaked Epic action a hard error
  // for multi-editor users, so it defaults to `read` (plan 5.1).
  if (action.startsWith("epic_")) return { class: "read", source: "epic-default" };

  return { class: "unknown", source: "unresolved" };
}

/** Convenience over a `category.action` task name. */
export function classifyTaskClass(taskName: string): ActionClassification {
  const { tool, action } = splitTaskName(taskName);
  return classifyActionClass(tool, action);
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
