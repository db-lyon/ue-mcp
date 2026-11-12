# Owned Fab content

The `fab` category takes content you own on Fab from your library into the project without leaving the conversation: sign in, sync your library, find the item, download it, install it. Searching and buying stay on fab.com; `fab(open_search)` opens fab.com's search page at a query for that.

## Before you start

Fab's editor plugin is not part of the engine install. The Epic Games Launcher adds it as its own Library item, **Fab UE Plugin**, which installs into `Engine/Plugins/Fab` and normally arrives with the engine. If `fab(status)` answers `errorCode: PLUGIN_NOT_AVAILABLE`, its `note` says what is missing: open Unreal Engine > Library in the launcher, find Fab UE Plugin, choose Install to Engine and pick your engine version, then restart the editor. A project that disables Fab gets the same code with the step to enable it.

The owned-content actions (`list_owned`, `download` and the job actions) need UE 5.8 or newer.

## The flow

1. **Sign in.** `fab(login)` opens the Epic account portal. When a session is already active it opens nothing and answers `unchanged: true`. Poll `fab(status)` until `authenticated` is true. The session stays inside the Fab plugin: ue-mcp never accepts, stores or returns a token.
2. **Sync your library.** `fab(sync_library)` fetches every page of your owned library and publishes it to the Fab table the Content Browser shows. It returns at once; poll `fab(status).librarySync` until `state` is `complete` (or `failed`, with a sanitized `error`). The previous snapshot stays in place until every page has arrived, and a failed sync keeps it.
3. **Find the item.** `fab(list_owned, {query: "rock"})` filters your synced library by title. Each row carries `listingId` (what `download` takes), `availableForThisEditor` (whether this editor's engine version and platform are among the item's `projectVersions`) and the item's own fields.
4. **Download it.** `fab(download, {listingId})` starts a job and returns its `jobId`. `engineVersion` and `platform` default to this editor's; pass them to pick another build, and a mismatch lists the versions the listing offers. Poll `fab(download_status, {jobId})` for `phase`, `progress`, bytes and speed until `state` is `complete`. `pause_download`, `resume_download` and `cancel_download` control it; resuming fetches a fresh signed URL and reuses what was already verified. The default output folder is under the Fab cache; `outputDirectory` (absolute) puts it elsewhere.
5. **Install it.** `fab(install_pack, {jobId})` copies the verified pack's Content packages into the project at their original paths, so their references hold, and rescans them so they appear in the Content Browser. It checks every file and every collision before copying anything: a different file already at a pack path is refused, an identical one is skipped. Config, plugins, executables and project settings are never installed. The inverse is `fab(remove_installation, {jobId})`, which removes only the files this job created, and refuses when one was modified, is loaded, or is referenced from outside the pack.

When a step is missing, the next action says which: `FAB_NOT_SIGNED_IN` before a sign-in, `FAB_LIBRARY_NOT_SYNCED` before a sync.

Loose source files (fbx, gltf, textures) that you downloaded yourself go through `fab(import_file, {source, destination})` instead.

## What is checked

Downloads run through the engine's BuildPatchServices with SHA verification of every file. `verifyOnly: true` checks an existing download with a separate read-only pass that never repairs or deletes, so a corrupt file stays for inspection. Job IDs last for the editor session; after a restart, repeat `download` with the same listing, version and folder and the verified files are reused. Signed URLs and distribution responses are never written to disk or returned.

`UE.MCP.Fab.EndToEnd.SyncDownloadInstall` runs this whole flow against a local server standing in for Fab and its CDN: a two-page library sync published to the Fab table, `list_owned`, a signed download through BuildPatchServices, installation of a real engine package checked in the Asset Registry, and its removal. `UE.MCP.Fab.Download.NativeTransferAndInstallation` covers pause and resume, idempotent installation, edited-file preservation and corrupt-file detection, and `UE.MCP.Fab.Library.Protocol` the library response and pagination rules.
