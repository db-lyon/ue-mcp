# Saving Blueprint edits

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
