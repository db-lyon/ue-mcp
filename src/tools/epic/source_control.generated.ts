// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_add = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_check_out = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_create_branch = {"properties":{"branchName":{"type":"string"},"repository":{"type":"string"}},"required":["branchName"]} as const;
const S_epic_create_changelist = {"properties":{"assetPaths":{"type":"array","items":{"type":"string"}},"description":{"type":"string"}},"required":["assetPaths","description"]} as const;
const S_epic_delete = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_delete_branch = {"properties":{"branch":{"type":"string"},"repository":{"type":"string"}},"required":["branch"]} as const;
const S_epic_download = {"properties":{"file":{"type":"string"},"revision":{"type":"string"},"destination":{"type":"string"}},"required":["file"]} as const;
const S_epic_edit_changelist_description = {"properties":{"changelistId":{"type":"string"},"description":{"type":"string"}},"required":["changelistId","description"]} as const;
const S_epic_get_asset_source_control_state = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_changelist_files = {"properties":{"changelistId":{"type":"string"}},"required":["changelistId"]} as const;
const S_epic_get_checked_out_assets = {"properties":{}} as const;
const S_epic_list_asset_revisions = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_list_branches = {"properties":{"repository":{"type":"string"}}} as const;
const S_epic_list_changelists = {"properties":{}} as const;
const S_epic_list_submitted_changelists = {"properties":{"revision":{"type":"string"},"length":{"type":"integer"},"repository":{"type":"string"}}} as const;
const S_epic_load_asset_revision = {"properties":{"assetPath":{"type":"string"},"revision":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_merge_branch = {"properties":{"branch":{"type":"string"},"repository":{"type":"string"}},"required":["branch"]} as const;
const S_epic_move_files_to_changelist = {"properties":{"changelistId":{"type":"string"},"files":{"type":"array","items":{"type":"string"}}},"required":["changelistId","files"]} as const;
const S_epic_push_branch = {"properties":{"branch":{"type":"string"},"repository":{"type":"string"}},"required":["branch"]} as const;
const S_epic_query_branch = {"properties":{"branch":{"type":"string"},"repository":{"type":"string"}}} as const;
const S_epic_query_changelist_details = {"properties":{"revision":{"type":"string"},"repository":{"type":"string"}},"required":["revision"]} as const;
const S_epic_query_history = {"properties":{"file":{"type":"string"}},"required":["file"]} as const;
const S_epic_query_provider = {"properties":{"repository":{"type":"string"}}} as const;
const S_epic_query_states = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_resolve = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_resolve_complete = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_resolve_mine = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_resolve_theirs = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_restore_as_latest = {"properties":{"description":{"type":"string"},"repository":{"type":"string"}},"required":["description"]} as const;
const S_epic_restore_branch = {"properties":{"branch":{"type":"string"},"repository":{"type":"string"}},"required":["branch"]} as const;
const S_epic_revert = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_revert_unchanged = {"properties":{"files":{"type":"array","items":{"type":"string"}}},"required":["files"]} as const;
const S_epic_submit_changelist = {"properties":{"changelistId":{"type":"string"}},"required":["changelistId"]} as const;
const S_epic_submit_changes = {"properties":{"description":{"type":"string"}},"required":["description"]} as const;
const S_epic_switch_branch = {"properties":{"branch":{"type":"string"},"repository":{"type":"string"}},"required":["branch"]} as const;
const S_epic_sync = {"properties":{"revision":{"type":"string"},"repository":{"type":"string"}}} as const;
const S_epic_validate_changelist = {"properties":{"changelistId":{"type":"string"}},"required":["changelistId"]} as const;
const S_epic_validate_changes = {"properties":{}} as const;

/** 38 wrapped engine tools routed to the `source_control` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add: {
    epicSchema: S_epic_add,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.Add", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Adds files to source control; they are added on the next submit. Async: await the result. The provider decides which files it accepts; a file it refuses, for example one that is already tracked, fails the call with its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_check_out: {
    epicSchema: S_epic_check_out,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.CheckOut", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Checks files out, so they can be edited and submitted. Async: await the result. The provider decides which files it accepts; a file it refuses, for example one that is not tracked or is checked out exclusively by another user, fails the call with its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_create_branch: {
    epicSchema: S_epic_create_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.CreateBranch", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Creates a branch off the current branch. Async: await the result. Does not change local files. Providers may refuse while there are pending changes, and may push the new branch to the server. Available on UE 5.9; absent from UE 5.8. Params: branchName, repository?", "epic_call_tool"),
  },
  epic_create_changelist: {
    epicSchema: S_epic_create_changelist,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.CreateChangelist", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Creates a new pending changelist and moves the specified files into it. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: assetPaths, description", "epic_call_tool"),
  },
  epic_delete: {
    epicSchema: S_epic_delete,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.Delete", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Marks files for delete and removes them from disk, discarding any unsaved changes in their loaded packages. Unloads the assets of the deleted files and reloads the open level when its map or external packages change, so a deleted actor of a level that stores its actors in separate files disappears from that level. Blocks until the delete and the reload complete. Game thread only. Raises when no files are given. Raises when source control is unavailable. Raises when a file cannot be resolved. Raises when the provider refuses a file, for example one that is not tracked; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_delete_branch: {
    epicSchema: S_epic_delete_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.DeleteBranch", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Deletes a branch for every user. Providers that keep history may archive it instead. Async: await the result. Does not change local files. The provider decides which branches it accepts, for example it may refuse the current branch. Available on UE 5.9; absent from UE 5.8. Params: branch, repository?", "epic_call_tool"),
  },
  epic_download: {
    epicSchema: S_epic_download,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.Download", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Downloads a revision of a file to a local path, without changing the workspace. Async: await the result. Refreshes the history of the file first. Available on UE 5.9; absent from UE 5.8. Params: file, revision?, destination?", "epic_call_tool"),
  },
  epic_edit_changelist_description: {
    epicSchema: S_epic_edit_changelist_description,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.EditChangelistDescription", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Replaces the description of a pending changelist. Async: await the result. The default changelist's description cannot be edited; to give its files a description, create a changelist with CreateChangelist and move the files into it with MoveFilesToChangelist. Available on UE 5.9; absent from UE 5.8. Params: changelistId, description", "epic_call_tool"),
  },
  epic_get_asset_source_control_state: {
    epicSchema: S_epic_get_asset_source_control_state,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.GetAssetSourceControlState", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Returns the source-control state for an asset. Blocks until the provider answers. Does not write to the message log. Raises when the asset does not exist or when the state cannot be read, for example when source control is disabled or unavailable. A file that is not tracked by source control is not an error: bIsSourceControlled is false. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_get_changelist_files: {
    epicSchema: S_epic_get_changelist_files,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.GetChangelistFiles", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Lists the files in a pending changelist, including files that are not content. Raises when source control is unavailable or the changelist does not exist. Available on UE 5.9; absent from UE 5.8. Params: changelistId", "epic_call_tool"),
  },
  epic_get_checked_out_assets: {
    epicSchema: S_epic_get_checked_out_assets,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.GetCheckedOutAssets", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Returns the content paths of all assets you currently have open in source control (checked out, marked for add, or marked for delete). Non-content files (source, config, etc.) and anything that doesn't map to a mounted content root are omitted. Files marked for delete are still listed. Raises when source control is not enabled or the status refresh fails. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_asset_revisions: {
    epicSchema: S_epic_list_asset_revisions,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ListAssetRevisions", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Returns the source-control revision history for an asset, newest first. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_list_branches: {
    epicSchema: S_epic_list_branches,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ListBranches", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Lists the branches of a repository. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: repository?", "epic_call_tool"),
  },
  epic_list_changelists: {
    epicSchema: S_epic_list_changelists,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ListChangelists", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Lists the pending changelists, including the default changelist. Raises when source control is unavailable, the provider does not use changelists or the refresh fails. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_submitted_changelists: {
    epicSchema: S_epic_list_submitted_changelists,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ListSubmittedChangelists", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Lists submitted changelists, one page at a time, in the order the provider reports them, typically newest first. Async: await the result. To get the next page, pass the last identifier of a page as Revision. Available on UE 5.9; absent from UE 5.8. Params: revision?, length?, repository?", "epic_call_tool"),
  },
  epic_load_asset_revision: {
    epicSchema: S_epic_load_asset_revision,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.LoadAssetRevision", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Loads a historical revision of an asset alongside the currently loaded one. Async: await the result. Treat the loaded package as read-only. It stays in memory until garbage collection runs. Available on UE 5.9; absent from UE 5.8. Params: assetPath, revision?", "epic_call_tool"),
  },
  epic_merge_branch: {
    epicSchema: S_epic_merge_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.MergeBranch", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Merges the head of another branch into the current branch and reloads the loaded packages the merge changes, including the open level when its map or external packages change. Blocks until the merge and the reload complete. Game thread only. Previews the merge first to find the affected files. Files changed on both branches may become conflicted: resolve them before submitting. Providers may push the merge result to the server. Raises when the branch is empty. Raises when source control is unavailable. Raises when the repository does not exist. Raises, without merging, when a loaded package the merge changes has unsaved changes: save or revert it first. Raises when the provider fails an operation; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: branch, repository?", "epic_call_tool"),
  },
  epic_move_files_to_changelist: {
    epicSchema: S_epic_move_files_to_changelist,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.MoveFilesToChangelist", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Moves files into an existing pending changelist. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: changelistId, files", "epic_call_tool"),
  },
  epic_push_branch: {
    epicSchema: S_epic_push_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.PushBranch", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Pushes a branch to the server. Async: await the result. Does not change local files. Available on UE 5.9; absent from UE 5.8. Params: branch, repository?", "epic_call_tool"),
  },
  epic_query_branch: {
    epicSchema: S_epic_query_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.QueryBranch", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Gets the details of a branch. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: branch?, repository?", "epic_call_tool"),
  },
  epic_query_changelist_details: {
    epicSchema: S_epic_query_changelist_details,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.QueryChangelistDetails", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Gets the details of a submitted changelist, such as its description, author and date. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: revision, repository?", "epic_call_tool"),
  },
  epic_query_history: {
    epicSchema: S_epic_query_history,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.QueryHistory", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Gets the revision history of any file, such as a Verse file, a project file or an asset, newest first. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: file", "epic_call_tool"),
  },
  epic_query_provider: {
    epicSchema: S_epic_query_provider,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.QueryProvider", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Gets the status the provider reports for a repository, for example the current branch, user and workspace. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: repository?", "epic_call_tool"),
  },
  epic_query_states: {
    epicSchema: S_epic_query_states,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.QueryStates", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Gets the current source control states of files, refreshing them from the provider first. Async: await the result. A file that is not tracked is not an error: its state reports bIsSourceControlled as false. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_resolve: {
    epicSchema: S_epic_resolve,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.Resolve", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Marks conflicted mergeable files, such as text or Verse files, resolved once their conflict markers have been removed from the files on disk. Edit and save the files first. Async: await the result. Does not change the files. For files that are not mergeable, use ResolveMine or ResolveTheirs instead. The provider decides which files it accepts; a file it refuses, for example one that still has conflict markers, fails the call with its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_resolve_complete: {
    epicSchema: S_epic_resolve_complete,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ResolveComplete", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Completes the resolution of conflicts, once every conflicted file has been resolved with Resolve, ResolveMine or ResolveTheirs. Async: await the result. Providers may record the resolved files locally, for example by committing them to a personal branch. The provider decides when it accepts this; for example it fails while conflicts remain or when the repository is not resolving conflicts. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_resolve_mine: {
    epicSchema: S_epic_resolve_mine,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ResolveMine", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Resolves conflicted files by keeping the local version, and reloads their loaded packages, including the open level when its map or external packages change. Blocks until the resolve and the reload complete. Game thread only. Raises when no files are given. Raises when source control is unavailable. Raises when a file cannot be resolved. Raises, without resolving, when a loaded package of the files has unsaved changes: save or revert it first. Raises when the provider refuses a file, for example one that is not conflicted; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_resolve_theirs: {
    epicSchema: S_epic_resolve_theirs,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ResolveTheirs", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Resolves conflicted files by taking the incoming version, discarding their local changes, and reloads their loaded packages, including the open level when its map or external packages change. Blocks until the resolve and the reload complete. Game thread only. Raises when no files are given. Raises when source control is unavailable. Raises when a file cannot be resolved. Raises, without resolving, when a loaded package of the files has unsaved changes: save or revert it first. Raises when the provider refuses a file, for example one that is not conflicted; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_restore_as_latest: {
    epicSchema: S_epic_restore_as_latest,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.RestoreAsLatest", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Makes the synced revision the latest revision of the current branch again, by submitting a new revision with its content. Blocks until the provider completes. Game thread only. Raises when the description is empty. Raises when source control is unavailable. Raises when the repository does not exist. Raises when the branch moved on since the last sync. Raises when files the restore changes are checked out by other users; the error lists them. Raises when the provider fails the operation; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: description, repository?", "epic_call_tool"),
  },
  epic_restore_branch: {
    epicSchema: S_epic_restore_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.RestoreBranch", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Restores a branch removed by DeleteBranch. Async: await the result. Does not change local files. The provider decides which branches it can restore. Available on UE 5.9; absent from UE 5.8. Params: branch, repository?", "epic_call_tool"),
  },
  epic_revert: {
    epicSchema: S_epic_revert,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.Revert", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Reverts files, discarding their local changes and any unsaved changes in their loaded packages, and reloads those packages, including the open level when its map or external packages change. Blocks until the revert and the reload complete. Game thread only. Raises when no files are given. Raises when source control is unavailable. Raises when a file cannot be resolved. Raises when the provider refuses a file, for example one that is not checked out; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_revert_unchanged: {
    epicSchema: S_epic_revert_unchanged,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.RevertUnchanged", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Releases the checkout of the given files that are checked out but not modified, so other users can check them out. Refreshes the modified state of the files first. Does not change the files on disk, so their loaded packages are not reloaded. Files that are not checked out or are modified are left alone. Blocks until the revert completes. Game thread only. Raises when no files are given. Raises when source control is unavailable. Raises when a file cannot be resolved. Raises when the provider fails an operation; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: files", "epic_call_tool"),
  },
  epic_submit_changelist: {
    epicSchema: S_epic_submit_changelist,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.SubmitChangelist", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Submits a pending changelist after validating it, and fails with the validation errors if it does not pass. Waits for the asset registry to finish scanning first. Invokes the Submit Tool when one is available, as the Submit action in the Changelists window does; the user completes the submit there, so the changelist is not yet submitted when the result completes. Otherwise submits it directly. Async: await the result. Use this rather than submitting by any other route. Available on UE 5.9; absent from UE 5.8. Params: changelistId", "epic_call_tool"),
  },
  epic_submit_changes: {
    epicSchema: S_epic_submit_changes,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.SubmitChanges", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Submits every pending change under the source control locations, without a changelist. Blocks until the submit completes. Game thread only. Refreshes the status of the source control locations, then submits every file there that can be checked in. Invokes the Submit Tool when one is available, as SubmitChangelist does; the user completes the submit there, so the changes are not yet submitted when this returns. Otherwise checks the files in directly, after virtualizing payloads when virtualization is enabled, which may append tags to the description. Does not save, sync or validate first, and does not add files that are not tracked. Raises when the description is empty. Raises when source control is unavailable. Raises when there is nothing to submit. Raises when the Submit Tool fails to start. Raises when the provider fails an operation; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: description", "epic_call_tool"),
  },
  epic_switch_branch: {
    epicSchema: S_epic_switch_branch,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.SwitchBranch", engines: ["5.9"] },
    ...bp("unknown", "[Epic SourceControl.SourceControlHelpers] Switches to the head of another branch and reloads the loaded packages the switch changes, including the open level when its map or external packages change. Blocks until the switch and the reload complete. Game thread only. Previews the switch first to find the affected files. Raises when the branch is empty. Raises when source control is unavailable. Raises when the repository does not exist. Raises, without switching, when a loaded package the switch changes has unsaved changes: save or revert it first. Raises when the provider fails an operation; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: branch, repository?", "epic_call_tool"),
  },
  epic_sync: {
    epicSchema: S_epic_sync,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.Sync", engines: ["5.9"] },
    ...bp("mutate", "[Epic SourceControl.SourceControlHelpers] Syncs to a revision and reloads the loaded packages the sync changes, including the open level when its map or external packages change. Blocks until the sync and the reload complete. Game thread only. Previews the sync first to find the affected files. Raises when source control is unavailable. Raises when the repository does not exist. Raises, without syncing, when a loaded package the sync changes has unsaved changes: save or revert it first. Raises when the provider fails an operation; the error has its reason. Available on UE 5.9; absent from UE 5.8. Params: revision?, repository?", "epic_call_tool"),
  },
  epic_validate_changelist: {
    epicSchema: S_epic_validate_changelist,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ValidateChangelist", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Validates a pending changelist the way the submit check does: the registered validators run on the changelist and on its assets. Waits for the asset registry to finish scanning first. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: changelistId", "epic_call_tool"),
  },
  epic_validate_changes: {
    epicSchema: S_epic_validate_changes,
    epicTool: { toolset: "SourceControl.SourceControlHelpers", name: "SourceControl.SourceControlHelpers.ValidateChanges", engines: ["5.9"] },
    ...bp("read", "[Epic SourceControl.SourceControlHelpers] Validates every pending change under the source control locations, without a changelist, the way the check-in dialog does. Collects the same files SubmitChanges submits, and runs the registered files validator on them. Waits for the asset registry to finish scanning first. Async: await the result. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  assetPath: z.string().optional().describe("Content path of the asset."),
  assetPaths: z.array(z.unknown()).optional().describe("Files to move into the new changelist, as content paths or absolute local file paths. Each must have something pending. May be empty to create an empty changel…"),
  branch: z.string().optional().describe("The name or id of the branch to delete. Must not be empty."),
  branchName: z.string().optional().describe("The name of the new branch. Must not be empty."),
  changelistId: z.string().optional().describe("The identifier of a pending changelist."),
  description: z.string().optional().describe("Description for the changelist. Must not be empty."),
  destination: z.string().optional().describe("The file path to write the revision to; it must not exist yet, and its directory is created when missing. Empty writes to a temporary file."),
  file: z.string().optional().describe("The file, as an absolute or relative file path, or a content path. Must not be empty."),
  files: z.array(z.unknown()).optional().describe("Files to add, as absolute or relative file paths, or content paths. Must not be empty."),
  length: z.number().optional().describe("The most identifiers to return. Must be positive."),
  repository: z.string().optional().describe("The root directory of the repository to create the branch in. Empty uses the repository of the project."),
  revision: z.string().optional().describe("The revision to download, for example a Revision value from the history. Empty downloads the latest revision."),
};
