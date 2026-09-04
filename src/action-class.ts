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
  "fetch", "lookup", "enumerate", "summarize", "measure", "audit",
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

  // ── Read the engine tree and the project's own sources ──────────────
  // These answer questions about C++ before any is written. They touch the
  // engine install and the project's Source/ read-only, and the index they
  // build is a cache under the user directory, not editor state. `verify` and
  // `lint` are not in the verb lexicon because nothing else on the surface
  // uses them.
  "project.verify_symbols": "read",
  "project.lint_cpp_header": "read",
  // The relational half of the same reading: what a class derives from, what
  // derives from it, and the lines around a declaration. `class` and `symbol`
  // are nouns rather than verbs, so the lexicon has nothing to go on. The
  // `find_*` analysis actions resolve through the lexicon already and are not
  // listed here.
  "project.class_hierarchy": "read",
  "project.symbol_context": "read",

  // ── Viewport, transactions and AI runtime ───────────────────────────
  // Repaints the viewport. Nothing about the editor, the project on disk or
  // the process changes; it is the visual equivalent of a read.
  "editor.redraw_viewport": "read",
  // Pure reads whose verb is not in the lexicon.
  "editor.get_undo_state": "read",
  "asset.read_skeletal_mesh_build_settings": "read",
  // Rewrites a mesh's UV layout in place, so it changes the asset on disk.
  "asset.unwrap_uvs": "mutate",
  // Reports UV faults and writes nothing.
  "asset.check_uvs": "read",
  // Records another skeleton as compatible on this one, so it writes the asset.
  "animation.register_compatible_skeleton": "mutate",
  // Discards an open transaction, restoring what it touched. That is a change
  // to editor state, so it is gated like one even though its purpose is undo.
  "editor.cancel_transaction": "mutate",
  // Asks whether A perceives B. Reads the perception component and nothing
  // more; the verb is simply not in the lexicon.
  "gameplay.check_perception": "read",
  // Injects a stimulus into the running world, which AI then reacts to.
  "gameplay.report_noise_event": "mutate",
  // Creates the config asset when it is absent, so it can create.
  "gameplay.ensure_mass_entity_config": "mutate",

  // ── GAS granting and diagnosis ──────────────────────────────────────
  // grant/revoke change a live actor's AbilitySystemComponent, so they are
  // mutations even though neither verb is in the lexicon.
  "gas.grant_ability": "mutate",
  "gas.revoke_ability": "mutate",
  // Reads by default. `activate: true` really does activate the ability, and
  // an action whose effect a parameter decides is exactly what `unknown` is
  // for: it is gated like a mutation, which is the safe answer, and the label
  // stays honest.
  "gas.trace_ability_activation": "unknown",

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

  // ── Terrain reads the verb lexicon has no word for ──────────────────
  // "analyze", "plan" and "project" are not in either list, and adding them as
  // verbs would classify every future action containing those segments from a
  // guess. All three of these only measure: they read the height field, or in
  // the case of plan_real_world read nothing but a file the caller named, and
  // return arithmetic. Nothing about them reaches an editor that could be the
  // wrong one.
  "landscape.analyze_terrain": "read",
  "landscape.plan_real_world": "read",
  "landscape.project_geo_coordinates": "read",

  // ── Reads whose verb reads as a write ───────────────────────────────
  // Serializers: they render a graph as text or JSON and return it.
  "blueprint.export_nodes_t3d": "read",
  // Reads call-site nodes across a directory. "search" is a read verb, but
  // "call" is a mutate verb from the wrapped engine surface, and a mutate verb
  // anywhere in the name wins - which is wrong for exactly this action. It
  // loads packages and reads graphs; it authors nothing (#945).
  "blueprint.search_call_sites": "read",
  "material.export_graph": "read",
  "pcg.export_graph": "read",
  // Reads the actor descriptors of an unloaded World Partition level. Nothing
  // is opened; `level.load` is the action that changes the open map.
  "level.load_actor_descs": "read",
  // Extracts a widget subtree as a description. `widget.author` writes one.
  "widget.extract_subtree": "read",
  // Both walk a widget tree and report on it. "focus" is a mutate verb because
  // the editor focuses a viewport on an actor, and a mutate verb anywhere in the
  // name wins, which is wrong for a report about focus and for a read of where
  // focus currently sits. `widget.set_runtime_focus` is the action that moves it.
  "widget.audit_focus_chain": "read",
  "widget.get_runtime_focus_path": "read",
  // Cache metadata about the Fab library, not a cache write.
  "fab.cache_info": "read",
  // Computes bounds, area, volume and topology over mesh data already in the
  // editor. "measure" is not in the read lexicon, and deliberately stays out of
  // it: a verb there is a licence for every future action that starts with it.
  "asset.measure_mesh_geometry": "read",
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
  // Evaluates a clip's pose to derive its planted-foot speed. Reads only.
  // "measure" is in the read lexicon, so this entry is belt and braces rather
  // than load bearing, and it documents the intent at the point of use.
  "animation.measure_natural_speed": "read",
  // ── Level refresh and inspect actions the verb lexicon cannot read ──
  // "rerun" is not "run" and "recreate" is not "create", so neither reaches
  // the lexicon. Both destroy and rebuild engine state on placed actors.
  "level.rerun_construction": "mutate",
  "level.recreate_physics_state": "mutate",
  // Compares two components' bounds and transforms. "test" is not a verb the
  // lexicon knows, and this one only measures.
  "level.test_component_overlap": "read",
  // Reads a property off many assets at once. "bulk" is a mutate verb because
  // every batch action before this one wrote, but the shape word is not the
  // verb: the read verb after it is.
  "asset.bulk_read_properties": "read",
  // Reads whose first segment is not a verb at all.
  "level.line_trace": "read",
  // Batch of the same collision queries. `bulk` is a mutate verb because most
  // bulk_* actions write; this one only observes.
  "level.bulk_line_trace": "read",
  "level.nav_project_point": "read",
  "level.count_actors_by_class": "read",
  "gameplay.project_to_nav": "read",
  "gameplay.find_nav_path": "read",
  "editor.hit_test_viewport_pixel": "read",
  "editor.check_for_crashes": "read",
  // Inspects the project descriptor, the engine tree and the deployed plugin
  // on disk. It deploys nothing, enables nothing and builds nothing; the
  // "install" in its name is the subject it reports on, not what it does.
  "project.check_install": "read",
  "editor.search_log": "read",
  "project.live_coding_status": "read",
  "feedback.route": "read",
  "epic.status": "read",
  "fab.status": "read",

  // ── Writes whose verb reads as a read ───────────────────────────────
  // Drives the editor's preview: it plays an animation in a viewport.
  "animation.preview_animation": "mutate",
  // Replaces a running component's transient post-process AnimBP override.
  "animation.set_live_post_process_anim_blueprint": "mutate",
  // Renders to a file on disk.
  "material.render_preview": "mutate",
  "asset.export": "mutate",
  "asset.export_texture": "mutate",
  "level.export_actor_fbx": "mutate",
  // Writes a 16-bit heightmap to a path the caller chose. It changes no
  // terrain, but a file appearing on disk is an external side effect and the
  // taxonomy above counts that as a mutation.
  "landscape.export_heightmap": "mutate",
  // Not one of the exports above, despite sitting next to them here since it
  // was written. It decodes a USoundWave's imported audio into memory and
  // returns the samples base64-encoded in the response: no intermediate file,
  // no path parameter to write one to, nothing dirtied and nothing saved. It
  // was classified `mutate` by association with the file-writing exports, and
  // the handler body settles it the other way.
  "audio.extract_pcm": "read",
  // Rebuilds the full-text index in the editor's own database.
  "asset.reindex_fts": "mutate",
  // Loads, rewrites and saves the packages that reference a set of
  // ObjectRedirectors, then deletes the redirectors that come out unreferenced
  // (#908). "fixup" is not a verb the lexicon knows, and this is about as far
  // from a read as an asset action gets.
  "asset.fixup_redirectors": "mutate",
  // Runs a CSG boolean over two StaticMeshes and writes the result to an
  // asset. "mesh" is not a verb at all, so the lexicon cannot see it, and this
  // is as far from a read as an asset action gets: it creates or overwrites a
  // StaticMesh package (#916).
  "asset.mesh_boolean": "mutate",
  // Posts to a public issue tracker.
  "feedback.submit": "mutate",
  // #956: reads an attribute value, but by default it first registers the
  // actor's own attribute sets on its AbilitySystemComponent when nothing is
  // registered yet, which is a change to the addressed editor's live world.
  // The verb says read; what it can do says mutate, and the gate follows what
  // it can do.
  "gas.get_live_attribute_value": "mutate",

  // Runs an EQS query through FEnvQueryManager::RunInstantQuery against an
  // already-spawned querier and serialises the scored items. `run` is a mutate
  // verb and this one does not: no spawn, no Modify, no save, nothing written
  // back to the query asset or the world. Landing it in the wrong editor
  // returns the wrong scores and changes nothing.
  "gameplay.run_eqs_query": "read",

  // ── Reads whose name leads with a subsystem, not a verb ─────────────
  // The lexicon only trusts a read verb in the leading segment, so these say
  // out loud what they read. Each one was confirmed against its handler: they
  // open an asset, walk a graph or a document, and write nothing back.
  "audio.metasound_get_graph": "read",
  "audio.metasound_read_document": "read",
  "audio.metasound_list_node_classes": "read",
  "audio.metasound_list_node_pins": "read",
  "audio.metasound_list_connections": "read",
  "audio.metasound_list_variables": "read",
  "audio.metasound_search_nodes": "read",
  "audio.metasound_inspect_node": "read",
  // Runs the MetaSound builder's own validation over a document and reports
  // the errors. It builds nothing and saves nothing.
  "audio.metasound_validate": "read",
  "audio.cue_get_graph": "read",
  // Answers whether a named weightmap layer is present on a landscape. The
  // `exists` question never creates the layer it asks about.
  "landscape.layer_exists": "read",

  // ── The workflow journal and the skill packs ────────────────────────
  // Neither reaches a bridge, so nothing here can land in the wrong EDITOR.
  // Both are still per project: the journal file is keyed by absolute project
  // root and skill packs install under a project's own `.claude/skills/`, so a
  // write that guessed the wrong session would record one project's history,
  // or install into one project's checkout, under another project's name.
  // Spelled out rather than left to the verb lexicon, which has no reason to
  // know what "note", "attach", "finish" or "cancel" do.
  "flow.journal_start": "mutate",
  "flow.journal_note": "mutate",
  "flow.journal_attach": "mutate",
  "flow.journal_finish": "mutate",
  "flow.journal_cancel": "mutate",
  "flow.journal_delete": "mutate",
  "flow.journal_list": "read",
  "flow.journal_get": "read",
  "flow.journal_status": "read",
  "flow.skill_install": "mutate",
  "flow.skill_remove": "mutate",
  "flow.skill_list": "read",
  "flow.skill_get": "read",
  "flow.skill_check": "read",
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
  // A read verb only settles the question from the FRONT of the name. Anywhere
  // else it is a noun as often as a verb, and the direction of that mistake is
  // the dangerous one: `wire_rvt_sample` was a mutation that classified as a
  // read on the strength of `sample`, and untargeted dispatch would have let it
  // edit whichever editor happened to be active. It reached a human review
  // rather than a release, and was renamed to `add_rvt_sampler`, which is a fix
  // to one name and not to the rule that produced it.
  //
  // So a trailing read verb now decides nothing and falls through to
  // `unresolved`, which is gated exactly like a mutation and which the drift
  // guard fails on. The cost is an override line for every genuine read whose
  // name leads with a subsystem instead of a verb; those are below, each saying
  // what it reads. The alternative costs a silent wrong-editor write.
  if (READ_VERBS.has(segments[0] ?? "")) return { class: "read", source: "lexicon" };

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
