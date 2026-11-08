# Owned Fab content

Use `fab(login)` to sign into the engine-bundled Fab plugin. Authentication stays inside the editor; MCP does not accept or return tokens. `fab(status).authenticated` reports whether a supported native session is available.

## Synchronizing the Content Browser

`fab(sync_library, {batchSize: 100})` starts a paginated sync on UE 5.8 or newer. Poll `fab(status).librarySync` until `state` is `complete` or `failed`. Starting a request is not proof of completion. The result includes page count, snapshot item count, published row count, HTTP status and a sanitized error when one occurs.

Production requests use `https://www.fab.com`, matching the current owned-library endpoint. Pagination cursors are URL encoded without extra quotes. HTTP failures, unexpected response types, malformed items and repeated cursors fail the sync. Every page must arrive before the native Fab Content Browser table is replaced. Failed requests leave the previous successful snapshot intact. Empty owned libraries are valid snapshots.

Only metadata for the signed-in account's owned content is requested. Search and purchases remain in the Fab website. No engine installation files or project assets are modified by synchronization. Older engines return an explicit unsupported message rather than claiming a console command completed successfully.

## Downloading an owned asset pack

1. Complete `sync_library`, then use `list_owned` with an optional title `query`. Each result includes the engine and platform combinations in `projectVersions`.
2. Call `download` with `listingId`, `engineVersion` and `platform`. Choose an exact supported version, for example `5.8` and `Mac`. Optional `outputDirectory` must be absolute; the default is under the native Fab cache. The directory must be empty or carry this artifact's download marker.
3. Poll `download_status` using the returned `jobId`. `complete` means the native installer succeeded with SHA verification. `failed` includes an error code; a started request or a non-cancelled installer is not treated as success.
4. Use `pause_download`, wait for `paused`, then `resume_download`. Resuming obtains fresh distribution authorization and reuses verified native files/staging. `cancel_download` retains those files too. After restarting the editor, job IDs expire; repeat `download` with the same version and directory to resume. Credentials and signed distribution URLs are not stored in MCP job metadata.
5. Call `install_pack` explicitly to copy verified native packages into project Content. Original package paths are preserved so references remain valid. All source hashes and destination collisions are checked before copying. Different existing files are refused, identical files are skipped. Config, project settings, executable files, full projects and engine plugins are excluded. Use the existing `import_file` for loose source files.

`verifyOnly: true` checks native manifest hashes with a separate, read-only streaming SHA pass. It retains failed files rather than using the native install verifier, which can remove corrupt files. It neither repairs nor downloads the output tree. This can check a pack downloaded earlier. `download_status` without a job ID lists this editor session's jobs. Progress reports the native phase, fraction, downloaded/required bytes and speed.

`remove_installation` is the inverse of installation: it removes only files that this session's job created, and refuses modified files, loaded packages or external package referencers. Pre-existing identical files are retained. Native staging and downloads are kept so the pack can be installed again. Job controls do not reverse already-transferred bytes. Pause or cancel active jobs before `clear_cache`.

These actions use the engine's BuildPatchServices module and its manifest/chunk verification, not an external downloader, credential store or manifest parser. Owned UE Content/Config packs are supported on UE 5.8+. Catalog scraping, purchases, plugin installation and settings migration remain outside this interface.

## Focused validation

The native automation tests under `UE.MCP.Fab` cover protocol errors, opaque legacy namespaces, exact version/platform selection, cursor and CDN signature encoding, generated native chunks, pause/resume, byte-exact reconstruction, installation idempotency, edited-file preservation, removal ownership and corrupt-file verification failure. They require no marketplace account or downloaded commercial assets. Authenticated distribution resolution and real Content Browser publication require a separate signed-in manual check.
