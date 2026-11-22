---
description: "When blueprint actions save an asset to disk, and how a graph DSL rewrite replaces part of a Blueprint graph."
---

# Blueprint edits: saving and graph rewrites

## When an edit is saved

Every `blueprint` action that changes an asset (a Blueprint, an interface, a user-defined enum or a user-defined struct) saves that asset to disk when it succeeds. Pass `save: false` to keep the change in memory instead: the package is marked dirty, nothing is written, and the result says so.

```json
{ "action": "add_variable", "assetPath": "/Game/BP_Door", "name": "OpenAngle", "varType": "float", "save": false }
```

```json
{ "success": true, "saved": false, "saveDeferred": true, "dirtyPackages": ["/Game/BP_Door"], "saveNote": "..." }
```

The rule covers every package the action saves, not only the one it was pointed at. `set_class_default` saves the child Blueprints that inherit the new value, and `edit_graph_parameters` saves the Blueprints whose call sites it renamed; with `save: false` they all stay dirty in memory.

Because nothing is written, `save: false` does not need the file to be writable and never checks it out of source control, so a run of edits on a locked or unchecked-out Blueprint costs no checkouts until you save. Assets on protected mounts (engine and plugin content) are still refused.

To write the pending edits, call `editor(save_dirty)` or save in the editor. To throw them away, call `asset(force_reload)` with `discardUnsaved: true`.

The default stays `save: true`, so a call that leaves `save` out writes the asset, as it always has. The actions that take `save` are listed with it in `project(describe_action)`. `export_batch` and `run_construction_script` change no asset and do not take it.

## Graph DSL rewrites

On UE 5.9, `blueprint(epic_write)` and `blueprint(epic_edit)` call Epic's Graph DSL toolset (`BlueprintToolsets.BlueprintDslToolset.Write` and `.Edit`). A write rebuilds each graph it touches from the text, and keeps only what a statement in the text reaches. That deletes:

- loose nodes: an impure chain with no entry before it, a pure node whose output nothing reads, a stray reroute
- comment boxes the text does not carry as a `; text` line
- collapsed graphs inside a rewritten entry, which are flattened for good

`epic_read` never shows loose nodes, so the text alone does not reveal what a write will remove.

Before ue-mcp sends a Write or Edit, it works out which graphs the call touches and lists what would be lost in them. If anything would be, the call is refused, nothing is changed, and the refusal carries the list under `preflight` with `reason: "dsl_rewrite_destructive"`. To go on, do one of these:

- Pass `allowDestructive: true` to apply the rewrite and accept the losses.
- Keep what would be lost: wire each loose node into an entry's exec chain or remove it with `blueprint(delete_node)`, write each comment box as a `; text` line above the statements it covers, and leave an entry holding a collapsed graph out of the document.
- Narrow `scope` to a graph that holds nothing the rewrite would lose.

`blueprint(preflight_dsl_write)` runs the same check without writing. Pass the `dsl` a Write would send, or the `edits` an Edit would send, with the same `scope`.

A tombstone for the construction script, `(fn UserConstructionScript :!delete)`, is refused even with `allowDestructive`. No Epic tool can bring the construction script back once it is deleted, and adding a function of that name creates an ordinary function. To empty it, write ``(fn `UserConstructionScript`)`` with no statements.

The same guard applies when the toolset is called directly through `epic(call_tool)` or `epic(call_mcp_tool)`, which take `allowDestructive` too.
