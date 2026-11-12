import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { z } from "zod";
import { specBp, schema as specSchema } from "./specs/fab.generated.js";
import { listEpicFabTools } from "./fab-epic.js";
import { actions as epicActions, schema as epicSchema } from "./epic/fab.generated.js";

// Uses the Fab plugin's own session (the launcher installs it as the "Fab UE
// Plugin" item) and the engine's native download/import services. Catalog
// search and import of listings are what Epic's built-in Fab tools do (the
// plugin's FabToolset, UE 5.9, declared below as epic_* actions from the
// recorded catalog); list_epic_tools finds them live, and nothing here
// may tell an agent that the editor cannot do what those tools do.
export const fabTool: ToolDef = categoryTool(
  "fab",
  "Fab (Epic marketplace) content in the editor. Searching the Fab catalog and importing listings, free ones included, is what Epic's built-in Fab tools do (the Fab plugin's FabToolset.FabToolset, UE 5.9). They are declared here as epic_* actions from the recorded catalog, and list_epic_tools lists the ones this editor actually has with the call that runs each. ue-mcp's own actions add: plugin/login status, login/logout, syncing your owned library into the Content Browser, downloading and verifying owned UE packs with pause/resume, inspecting/clearing the native cache, installing verified Content safely, importing local source files, and opening the fab.com search page at a query.",
  {
    list_epic_tools: listEpicFabTools,
    status:       specBp("read", "Report Fab plugin state: authenticated (whether a native Fab session is available), whether the plugin is installed and enabled (pluginDir, pluginVersion), whether the module is loaded, whether the native import/cache API is linked in this build, whether the Fab window has been opened this session, the download cache location/size, and librarySync state with pages, snapshotItems, publishedRows and any sanitized failure. The launcher installs Fab as its own 'Fab UE Plugin' item, so an engine can lack it; then errorCode is PLUGIN_NOT_AVAILABLE and note says what is missing and how to install it, the same reason the other Fab actions refuse with. Call this first.", "fab_status"),
    login: specBp("mutate", "Open the native Fab EOS account portal. With a session already active it opens nothing and answers unchanged=true. Returns before authentication completes; finish any prompt, then poll status.authenticated. No inverse: opening a portal does not own the resulting account session. idempotencyObservable=false because this call cannot attribute a later login to itself.", "fab_login"),
    logout: specBp("mutate", "Request native Fab logout and clear this editor's owned-library snapshot. EOS completes asynchronously; poll status.authenticated until false. No inverse: cleared credentials require a person to sign back in.", "fab_logout"),
    sync_library: specBp("mutate", "Sync the owned Fab library into the native Content Browser table (UE 5.8+). Requires a native login. Returns immediately; poll status.librarySync until complete or failed. Fetches all pages before replacing the previous snapshot, preserving it on HTTP, JSON, pagination or publication failure. No project files change.", "fab_sync_library"),
    list_owned: specBp("read", "Read the current account's synced owned metadata and supported projectVersions, with local title filtering and pagination. Each row adds listingId (what download takes) and availableForThisEditor (this editor's engine and platform are among its projectVersions). Complete sync_library first; before it, errorCode FAB_NOT_SIGNED_IN or FAB_LIBRARY_NOT_SYNCED says which step is missing. Only the owned library: to search the public catalog use Epic's built-in Fab tools (list_epic_tools).", "fab_list_owned"),
    download: specBp("mutate", "Start an owned native UE asset-pack download. engineVersion and platform default to this editor's; a listing without that build answers with the versions it offers. Returns job immediately; poll download_status for complete or failed. Native BuildPatchServices verifies SHA hashes and reuses partial staging. Defaults to the native Fab cache. verifyOnly checks an existing tree without repairing it. Repeat the same listing/version/outputDirectory after an editor restart to resume. No project content changes until install_pack.", "fab_download"),
    download_status: specBp("read", "Read asynchronous job state, native phase, progress, required/downloaded bytes and failure code; omit jobId to list this editor session's jobs. No tokens or signed URLs are returned.", "fab_download_status"),
    pause_download: specBp("mutate", "Stop network/assembly work while keeping native verified files and staging. Wait for paused before resuming. Can pause distribution resolution too.", "fab_pause_download"),
    resume_download: specBp("mutate", "Resume a paused, failed or cancelled job. Reuses the native Fab session and obtains a fresh distribution signature before native verification and repair.", "fab_resume_download"),
    cancel_download: specBp("mutate", "Cancel a job while retaining its verified data for an explicit later resume. Does not delete downloaded or installed content.", "fab_cancel_download"),
    install_pack: specBp("mutate", "Copy a completed, SHA-verified pack's native Content packages into this project, preserving package paths. Checks every source and collision first; different existing files are refused. Skips identical files and installs no Config, engine plugins or settings. Records exactly which files it created for remove_installation.", "fab_install_pack"),
    remove_installation: specBp("mutate", "Remove only unchanged, unloaded project files that this session's install_pack job created. Refuses modified or loaded packages before deleting anything; preserves pre-existing identical files.", "fab_remove_installation"),
    list_cached:  specBp("read", "List the entries currently in the local Fab download cache (already-downloaded owned assets).", "fab_list_cached"),
    cache_info:   specBp("read", "Report the Fab download cache location, total size, and entry count.", "fab_cache_info"),
    clear_cache:  specBp("mutate", "Delete the local Fab download cache to reclaim disk. Does not affect assets already imported into the project. Reports unchanged=true when the cache was already empty (where the native Fab API is linked). No inverse: the downloads come back only by downloading them again.", "fab_clear_cache"),
    import_file:  specBp("mutate", "Import a source file into the project through the Fab Interchange import pipeline. Use for owned assets that are downloaded/cached locally, or any local source file (fbx, textures). Single files import synchronously and report the created asset paths; pack/quixel workflows may run asynchronously. A synchronous import rolls back by deleting exactly the assets it created, taken from the paths the importer reported (a force delete, because the imported set references itself). An ASYNCHRONOUS import emits no inverse: its paths are not known yet, and a record naming the destination folder would delete whatever else already lives there.", "fab_import_file"),
    open_search:  specBp("read", "Open fab.com's own search page at a query in the user's default browser. Builds https://www.fab.com/search?q=<query>&listing_types=<listingType>&asset_formats=<assetFormat>&is_free=1, leaving out unset filters. This action itself sends no request to Fab and returns no listing data; to search the catalog and import listings from the editor, use Epic's built-in Fab tools (list_epic_tools). open=false returns the URL without opening it, for showing as a link. Returns url and opened.", "fab_open_search"),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key the fab handlers declare, generated from their C++
    // registrations.
    ...specSchema,
    includeSchemas: z.boolean().optional().describe("list_epic_tools: return each Fab tool's input schema (default false)"),
  },
);
