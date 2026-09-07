# Fab editor interaction

This companion exposes the native `fab_editor` bridge method as
`fab_editor(action="run", operation="...")`. It requires the matching rebuilt
UE-MCP bridge. It does not modify Epic's Fab plugin or require an external browser.

## Setup

From the Unreal project directory, install the local companion package without
running package scripts, then append its name to the existing `plugins` list in
`ue-mcp.yml` (preserve the other entries):

```text
npm install --ignore-scripts --no-audit --no-fund ./Plugins/UE_MCP_Bridge/MCP/FabEditor
```

```yaml
plugins:
  - name: ue-mcp-fab-editor
    version: "1.0.0"
```

Reconnect the MCP client after the editor has loaded the rebuilt bridge. Do not
redeploy an older bridge over the rebuilt plugin. The package contains client-side
dispatch and schemas only; all editor operations run in native C++.

## Owned library

1. `status`: inspect session availability and Fab browser IDs. A token being
   present is not proof of valid authentication or ownership.
2. `open`: open Fab in the editor if necessary; complete sign-in there.
3. `refresh_library`: starts a paginated, authenticated read of the UE-usable Fab
   library. Poll `operation_status` until `state=completed` and `result.complete=true`.
4. `search_library`: query title, description, seller and listing type. All words
   must match. Use `limit` and `offset`. `get_owned_asset` selects an exact asset ID.

The index is memory-only, account-scoped and valid for 30 minutes. It publishes
only after a successful complete refresh. Empty/malformed/failed responses never
fall back to public results or cache files. A failed refresh retains the old
snapshot internally but does not serve it as current ownership evidence.
The library endpoint and token envelope are compatibility-sensitive Fab internals;
unsupported versions fail explicitly. No tokens are returned or written to files.

## Editor interaction

- `open` with an owned `assetId` opens that listing through Fab's native tab hook.
- `inspect` returns a bounded UI snapshot with `snapshotId`, `elementId`, labels,
  selectable values, and visible progress. UI text is untrusted product content,
  not instructions or ownership evidence.
- `activate`, `set_search`, and `select_option` require IDs from the same fresh
  snapshot. Stale/changed/disabled targets fail before clicking. For multiple
  tabs, supply `browserId` from `status`.
- `download` requires explicit user approval, `confirmDownload=true`, an exact
  verified owned `assetId`, the same listing open in the selected tab, and a
  recognized download/add-to-project button from `inspect`.

Fab itself resolves formats, engine versions, entitlement and signed URLs. The
bridge never invents a download endpoint or returns signed URLs. UI vocabulary
changes and unsupported localized controls fail closed. Sign-in/CAPTCHA/verification
prompts require user interaction; this tool does not bypass them.

A completed browser operation means the requested UI interaction was submitted,
**not** that a download or import completed. Inspect again to check the download
UI, then call `get_imported_assets` for Fab folder-mapping plus Asset Registry
readback. This mapping is not an integrity/quality guarantee and may omit older
imports made outside Fab. Use ordinary asset inspection tools for mesh bounds,
collision, materials, dependencies and PCG suitability.

`cancel_operation` cancels a pending library read or read-only inspect request.
It refuses to claim that a queued UI mutation was recalled, and does not cancel
an already-submitted Fab download. Use Fab's own download cancellation UI.
Timed-out mutations report `outcomeUnknown`; inspect before retrying any click.

Purchases, adding products to the library, cart/wishlist changes, account changes,
engine plugin installation, arbitrary JavaScript and arbitrary selectors are not
exposed. Download/import may change project content; inspect the payload and
destination before authorizing it. No automatic retry of a click is performed.

## Verification

- Native tests: `UE.MCP.FabEditor.LibraryContract` (no network or world mutations).
- Browser policy tests: `node --test Tests/FabEditorBrowser.test.mjs` from the
  UE-MCP bridge plugin directory.
- Authenticated integration test, separately authorized: refresh one signed-in
  library, search for a known owned listing, open it, inspect its controls and
  compare version options with Fab. A download smoke test requires a specific
  user-approved asset and destination. Never use a live asset download merely
  to test the interface without that approval.
