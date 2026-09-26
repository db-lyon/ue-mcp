import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../category-tool.js";
import { specBp, schema as specSchema } from "./specs/fab.generated.js";

// Fab asset importer. Fab is Epic's unified content marketplace; UE ships an
// editor plugin whose window is a web frontend (catalog browse, library,
// purchase, and signed-URL resolution all live on Epic's servers) sitting on a
// thin native download+import layer. This category drives the native pieces
// that don't need the web frontend: login lifecycle, syncing the user's owned
// library into the Content Browser, inspecting/clearing the download cache, and
// importing owned/local source files through the Fab import pipeline.
//
// Store catalog browsing and buying are intentionally out of scope: they need
// the authenticated Fab backend (no official consumer REST API exists) and stay
// in the web window. Log in there once, add items to your library, then use
// sync_library + import_file here.
export const fabTool: ToolDef = categoryTool(
  "fab",
  "Import Fab (Epic marketplace) content: check plugin/login status, trigger login/logout, sync your owned library into the Content Browser, inspect/clear the download cache, and import owned or local source files into the project.",
  {
    status:       specBp("read", "Report Fab plugin state: whether the module is loaded, whether the native import/cache API is linked in this build, whether the Fab window has been opened this session, and the download cache location/size. Call this first.", "fab_status"),
    login:        specBp("mutate", "Trigger the Fab login flow (EOS account portal). Asynchronous - returns once the flow is opened, not once authenticated. Complete any prompt, then call status. No inverse: it creates no state of its own, and logout would clear a session that may predate the call, so the response says rollbackPossible=false. It carries no unchanged/already* flag either, and says so with idempotencyObservable=false: the Fab module publishes no authentication state this build can read back, so a flag here would be a claim rather than a reading.", "fab_login"),
    logout:       specBp("mutate", "Clear the persistent Fab authentication for this device. No inverse: logging back in needs a person at the EOS account portal, and no call restores cleared credentials. idempotencyObservable=false, because whether a session was there to clear is not readable from here.", "fab_logout"),
    sync_library: specBp("mutate", "Load the user's owned Fab library (\"My Folder\") into the Content Browser via TEDS. Requires an active login; items appear asynchronously. No inverse: it populates an in-editor index of what the account already owns, nothing un-lists it, and nothing on disk or in the project changes. idempotencyObservable=false, because the sync lands after this call returns.", "fab_sync_library"),
    list_cached:  specBp("read", "List the entries currently in the local Fab download cache (already-downloaded owned assets).", "fab_list_cached"),
    cache_info:   specBp("read", "Report the Fab download cache location, total size, and entry count.", "fab_cache_info"),
    clear_cache:  specBp("mutate", "Delete the local Fab download cache to reclaim disk. Does not affect assets already imported into the project. Reports unchanged=true when the cache was already empty (where the native Fab API is linked). No inverse: the downloads come back only by downloading them again.", "fab_clear_cache"),
    import_file:  specBp("mutate", "Import a source file into the project through the Fab Interchange import pipeline. Use for owned assets that are downloaded/cached locally, or any local source file (fbx, textures). Single files import synchronously and report the created asset paths; pack/quixel workflows may run asynchronously. A synchronous import rolls back by deleting exactly the assets it created, taken from the paths the importer reported (a force delete, because the imported set references itself). An ASYNCHRONOUS import emits no inverse: its paths are not known yet, and a record naming the destination folder would delete whatever else already lives there.", "fab_import_file"),
  },
  {
    // #1057: every key the fab handlers declare, generated from their C++
    // registrations.
    ...specSchema,
  },
);
