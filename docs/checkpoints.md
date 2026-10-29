# Checkpoints

Checkpoints keep a shadow history of the files an agent changes, so a client can let it edit the project live and then keep or revert its changes file by file. Edits apply to the real project as usual: the viewport and asset editors show them as they happen.

The history is a git repository at `<project>/Saved/UE_MCP_Bridge/shadow.git`, separate from any source control the project uses. The server drives it with isomorphic-git, so no git binary is needed, and it never reads or writes the project's own `.git`, hooks or LFS config. It works on projects under Perforce, Plastic or no source control at all.

## Turning them on

Checkpoints are off unless enabled:

```yaml
ue-mcp:
  checkpoints: on
```

The object form also sets the size above which a file is kept as a plain copy beside the repository instead of a git blob (default 1 GB):

```yaml
ue-mcp:
  checkpoints:
    enabled: on
    largeFileBytes: 1073741824
```

A client can override the setting for one editor session through the daemon (`PUT /v1/checkpoints/enabled`). The override lives in the daemon until it exits and is never written to a file.

While off, no shadow repository is created and the bridge does not intercept saves. Turning them off keeps the history; `editor(clear_checkpoints)` deletes it.

## What gets recorded

Recording is lazy. Nothing is copied for paths the agent never touches, so the first checkpoint on a large project costs the same as any other.

- **Package saves.** The bridge binds `UPackage::PreSavePackageWithContextEvent`. Before the first save of a package after a checkpoint, it copies the package's current bytes into a pending folder (`Saved/UE_MCP_Bridge/shadow-pending/`), or records that it did not exist. A handler that saves with a direct `UPackage::SavePackage`, which raises no such event, records the package's file itself before writing. Deletes are recorded from `FEditorDelegates::OnPackageDeleted`, and bridge renames record their source packages first.
- **Server writes.** `project(write_cpp_file)`, `write_source_file`, `add_module_dependency` and `add_cpp_member` record the file before writing it. `project(set_config)` records the ini file in the editor before writing it.
- **A checkpoint** folds the pending copies into the history and commits the current bytes of every path recorded so far. Git stores each version of a file once.

A write outside the bridge and outside package saves (an external editor, a build) is not recorded until the next checkpoint. `list_changes` marks such a path `untracked`, and `restore` refuses it unless the client passes `force`.

Changes still in memory at a checkpoint (an unsaved level edit, a dirty Blueprint) are not on disk, so the history does not have them. The checkpoint result lists `dirtyPackages`; save them first to record them. Wrap a turn in `editor(begin_transaction)` and `end_transaction` to cover the in-memory part, and cancel the transaction before restoring files when reverting a whole turn.

## Actions

| Action | What it does |
|---|---|
| `editor(checkpoint, label?)` | Record a checkpoint and return its id, the paths it recorded as changed, and the dirty packages. |
| `editor(list_checkpoints, limit?)` | Whether checkpoints are on and why, the repository path, and the checkpoints, newest first (default 50). |
| `editor(list_changes, from, to?, assetDiffs?, blueprintDiffs?)` | Every file changed between two checkpoints, or since `from` when `to` is omitted. Config and source files carry a unified diff, and every package carries an asset diff (below). `assetDiffs` and `blueprintDiffs` default to true. |
| `editor(diff_checkpoint_asset, from, to?, changedPath)` | One package's asset diff, for a package `list_changes` deferred or to diff one again. |
| `editor(open_checkpoint_diff, from, to?, changedPath)` | Open Unreal's own diff of a package in the editor and bring it to the front. |
| `editor(restore, checkpoint, paths?, force?)` | Put every path, or only `paths`, back to the checkpoint. Restoring a subset is a cherry pick: the rest of the turn stays. |
| `editor(clear_checkpoints)` | Delete the history. |

## Asset diffs

With an editor connected, `list_changes` gives each changed `.uasset` and `.umap` an `assetDiff`. The bridge loads each version with `DiffUtils::LoadPackageForDiff`, the way source-control diffs load a revision, runs Unreal's own diff code on them, and serializes the results:

| Asset | Engine diff | Records |
|---|---|---|
| Blueprint | `SBlueprintDiff` | each graph through `FGraphDiffControl::DiffGraphs`, components through `DiffUtils::CompareUnrelatedSCS`, My Blueprint variables, Defaults on the class default object and Class Settings through `DiffUtils::CompareUnrelatedStructs`, plus the structural `blueprint(diff)` summary |
| Material, material function | `SMaterialDiff` | the material graph, rebuilt as the material editor shows it, through `FGraphDiffControl::DiffGraphs` with each node matched across versions by its expression's GUID, a `NODE_PROPERTY` record for each matched expression whose editable properties changed, and the details through `DiffUtils::CompareUnrelatedObjects` |
| Material instance | `SMaterialInstanceDiff` | the details, overridden parameters included, through `DiffUtils::CompareUnrelatedObjects` |
| DataTable | the CSV export (`UDataTable::GetTableAsCSV`) | the CSV text diff, and each row present in both versions through `DiffUtils::CompareUnrelatedStructs` |
| Level, World Partition actor package | the details diff per actor | actors matched by name, added and removed with class and transform, and each matched actor and component through `DiffUtils::CompareUnrelatedObjects` |
| Anything else (static and skeletal mesh, texture, sound, Niagara) | the details diff | `DiffUtils::CompareUnrelatedObjects` |

Each record is the engine's own: a property record has its `FPropertySoftPath`, its `EPropertyDiffType`, the engine's message (`DiffViewUtils::PropertyDiffMessage`) and both values exported with `FProperty::ExportText`; a graph record has its `EDiffType`, category, `DisplayString`, `ToolTip`, and the nodes and pins it names. Records are grouped under the engine's categories (`Graph: EventGraph`, `Components`, `My Blueprint`, `Defaults`, `Details`, `Rows`, `Actors`). Differences under properties the details panel does not show are counted in `hiddenChanges`. `engine` names the diff used and what it covers.

Each version also lists facts about it (LOD vertex and triangle counts, material slots, texture size and format, parameter values) as context beside the records, and a picture rendered with `ThumbnailTools::RenderThumbnail` on the object loaded for diff. A level, which has no scene when loaded for diff, and a class with no thumbnail renderer use the thumbnail saved in that version's package. Pictures are kept per blob id, so a version always shows the same one.

An added asset has `before: null` and a description of what it is: its properties that differ from its class defaults (for a Blueprint, its defaults against its parent class). A deleted one has `after: null`. A package that cannot be diffed carries `state: "error"` and the reason, such as `the package could not be loaded for diff: ...` (also given for a file that is not a package) or that the editor is not connected. `summary` is one line to decide on.

The first 25 packages are diffed up front. The rest are listed in `assetDiffs.deferred`, carry `state: "deferred"`, and are diffed one at a time with `diff_checkpoint_asset` or `GET /v1/checkpoints/asset-diff`. Diffs are cached per path and pair of versions.

`open_checkpoint_diff` opens Unreal's own view: `IAssetTools::DiffAssets` for a Blueprint, a material, a material instance or a data asset, `SDetailsDiff` for other assets, and, for a level or a DataTable, whose engine diff is a text diff, the external tool that Editor Preferences > Loading & Saving > Text Diff Tool names (refused with that reason when none is set). Each side's revision line in the window shows its checkpoint's label and the time the checkpoint was taken; "Current" shows the time the diff opened. On an added or deleted asset it opens the asset editor on the one version there is. `nativeDiff` on each asset diff says whether it can open, and why not.

A restore:

1. takes a checkpoint (`before`), so restoring to it undoes the restore;
2. closes asset editors on the restored packages and detaches their loaders;
3. writes the files, deleting those that did not exist at the checkpoint;
4. reloads restored packages, unloads deleted ones and drops them from the asset registry, reloads the `GConfig` branch of a restored `Config/Default*.ini`, and reopens the closed editors;
5. takes another checkpoint (`after`).

Restored source is bytes only; rebuilding is the client's call. A restore is refused during PIE. A path changed outside the agent since the checkpoint is refused as `edited-outside`, and a path nothing recorded by then as `not-recorded`.

## Daemon API

The same operations are on the daemon's HTTP API under `/v1/checkpoints`, and on `api.checkpoints` for daemon extensions (extension API 3, feature `checkpoints`). `/v1/health` lists `checkpoints` under `features`, and `checkpoints.assetDiff` for the asset diffs; extensions see the same feature, with `assetDiff`, `thumbnail` and `openNativeDiff` on `api.checkpoints`.

| Method | Path | Body or query |
|---|---|---|
| GET | `/v1/checkpoints` | `?editor&limit` |
| PUT | `/v1/checkpoints/enabled` | `{ editor?, enabled: true \| false \| null }` |
| POST | `/v1/checkpoints` | `{ editor?, label? }` |
| GET | `/v1/checkpoints/changes` | `?editor&from&to&assetDiffs&blueprintDiffs` |
| GET | `/v1/checkpoints/asset-diff` | `?editor&from&to&path` |
| GET | `/v1/checkpoints/thumbnails/<id>.png` | `?editor` |
| POST | `/v1/checkpoints/native-diff` | `{ editor?, path, from, to? }` |
| POST | `/v1/checkpoints/restore` | `{ editor?, checkpoint, paths?, force? }` |
| DELETE | `/v1/checkpoints` | `?editor` |

`editor` names a session as `/v1/health` lists it; omitted is the active session. Events on `/v1/events`: `checkpoints.toggled`, `checkpoint.created`, `checkpoint.restored` and `checkpoints.cleared`.
