# Owned Fab content

Use `fab(login)` to sign into the engine-bundled Fab plugin. Authentication stays inside the editor; MCP does not accept or return tokens. `fab(status).authenticated` reports whether a supported native session is available.

## Synchronizing the Content Browser

`fab(sync_library, {batchSize: 100})` starts a paginated sync on UE 5.8 or newer. Poll `fab(status).librarySync` until `state` is `complete` or `failed`. Starting a request is not proof of completion. The result includes page count, snapshot item count, published row count, HTTP status and a sanitized error when one occurs.

Production requests use `https://www.fab.com`, matching the current owned-library endpoint. Pagination cursors are URL encoded without extra quotes. HTTP failures, unexpected response types, malformed items and repeated cursors fail the sync. Every page must arrive before the native Fab Content Browser table is replaced. Failed requests leave the previous successful snapshot intact. Empty owned libraries are valid snapshots.

Only metadata for the signed-in account's owned content is requested. Search and purchases remain in the Fab website. No engine installation files or project assets are modified by synchronization. Older engines return an explicit unsupported message rather than claiming a console command completed successfully.
