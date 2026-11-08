// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd fab handler. */
export const handlerSpecs: HandlerSpecs = {
  "fab_cache_info": {
    "category": "fab",
    "params": []
  },
  "fab_cancel_download": {
    "category": "fab",
    "params": [
      {
        "name": "jobId",
        "type": "string",
        "required": true,
        "description": "Job to cancel; verified files and native staging remain resumable"
      }
    ]
  },
  "fab_clear_cache": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and deletes the Fab download cache when called"
  },
  "fab_download": {
    "category": "fab",
    "params": [
      {
        "name": "listingId",
        "type": "string",
        "required": true,
        "description": "Owned Fab listing UUID"
      },
      {
        "name": "engineVersion",
        "type": "string",
        "required": true,
        "description": "Exact supported UE version, e.g. 5.8"
      },
      {
        "name": "platform",
        "type": "string",
        "required": true,
        "description": "Exact target platform from projectVersions, e.g. Mac or Windows"
      },
      {
        "name": "outputDirectory",
        "type": "string",
        "required": false,
        "description": "Absolute pack output directory (default native Fab cache/UE-MCP/listing/artifact)"
      },
      {
        "name": "verifyOnly",
        "type": "boolean",
        "required": false,
        "description": "SHA verify existing files without repairing or downloading (default false)"
      }
    ]
  },
  "fab_download_status": {
    "category": "fab",
    "params": [
      {
        "name": "jobId",
        "type": "string",
        "required": false,
        "description": "Job identifier; omit to list all editor-session jobs"
      }
    ]
  },
  "fab_import_file": {
    "category": "fab",
    "params": [
      {
        "name": "source",
        "type": "string",
        "required": true,
        "description": "Absolute path of the source file on disk",
        "aliases": [
          "sourceFile"
        ]
      },
      {
        "name": "destination",
        "type": "string",
        "required": true,
        "description": "Destination content path, e.g. /Game/Fab/Imported",
        "aliases": [
          "destPath"
        ]
      }
    ]
  },
  "fab_install_pack": {
    "category": "fab",
    "params": [
      {
        "name": "jobId",
        "type": "string",
        "required": true,
        "description": "Completed, SHA-verified asset-pack download to copy into project Content, preserving paths"
      }
    ]
  },
  "fab_list_cached": {
    "category": "fab",
    "params": []
  },
  "fab_list_owned": {
    "category": "fab",
    "params": [
      {
        "name": "query",
        "type": "string",
        "required": false,
        "description": "Filter owned item titles, no catalog requests"
      },
      {
        "name": "offset",
        "type": "integer",
        "required": false,
        "description": "Result offset (default 0)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Result limit, 1-200 (default 50)"
      }
    ]
  },
  "fab_login": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and opens the EOS account-portal login flow when called"
  },
  "fab_logout": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and clears the stored Fab credentials when called"
  },
  "fab_open_search": {
    "category": "fab",
    "params": [
      {
        "name": "query",
        "type": "string",
        "required": true,
        "description": "Search text"
      },
      {
        "name": "listingType",
        "type": "string",
        "required": false,
        "description": "Listing type filter, e.g. 3d-model, material, tool-and-plugin"
      },
      {
        "name": "assetFormat",
        "type": "string",
        "required": false,
        "description": "Asset format filter (default unreal-engine; empty string drops the filter)"
      },
      {
        "name": "free",
        "type": "boolean",
        "required": false,
        "description": "Only free listings (default false)"
      },
      {
        "name": "open",
        "type": "boolean",
        "required": false,
        "description": "Open the page in the default browser (default true); false only returns the URL"
      }
    ],
    "contractExempt": "Opens the fab.com search page in the default browser when called"
  },
  "fab_pause_download": {
    "category": "fab",
    "params": [
      {
        "name": "jobId",
        "type": "string",
        "required": true,
        "description": "Active download job identifier"
      }
    ]
  },
  "fab_remove_installation": {
    "category": "fab",
    "params": [
      {
        "name": "jobId",
        "type": "string",
        "required": true,
        "description": "Remove only unchanged, unloaded files that this job created in project Content"
      }
    ]
  },
  "fab_resume_download": {
    "category": "fab",
    "params": [
      {
        "name": "jobId",
        "type": "string",
        "required": true,
        "description": "Paused, failed or cancelled job identifier; obtains a fresh distribution signature"
      }
    ]
  },
  "fab_status": {
    "category": "fab",
    "params": []
  },
  "fab_sync_library": {
    "category": "fab",
    "params": [
      {
        "name": "batchSize",
        "type": "integer",
        "required": false,
        "description": "Library items per request, 1-1000 (default 100)"
      }
    ],
    "contractExempt": "Queues a Fab library sync whatever the batch size"
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  fab_cache_info: "Params: none",
  fab_cancel_download: "Params: jobId",
  fab_clear_cache: "Params: none",
  fab_download: "Params: listingId, engineVersion, platform, outputDirectory?, verifyOnly?",
  fab_download_status: "Params: jobId?",
  fab_import_file: "Params: source (or sourceFile), destination (or destPath)",
  fab_install_pack: "Params: jobId",
  fab_list_cached: "Params: none",
  fab_list_owned: "Params: query?, offset?, limit?",
  fab_login: "Params: none",
  fab_logout: "Params: none",
  fab_open_search: "Params: query, listingType?, assetFormat?, free?, open?",
  fab_pause_download: "Params: jobId",
  fab_remove_installation: "Params: jobId",
  fab_resume_download: "Params: jobId",
  fab_status: "Params: none",
  fab_sync_library: "Params: batchSize?",
};

/** Every key the spec'd fab handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  assetFormat: z.string().optional().describe("Asset format filter (default unreal-engine; empty string drops the filter)"),
  batchSize: z.number().int().optional().describe("Library items per request, 1-1000 (default 100)"),
  destination: z.string().optional().describe("Destination content path, e.g. /Game/Fab/Imported"),
  destPath: z.string().optional().describe("Alias for destination"),
  engineVersion: z.string().optional().describe("Exact supported UE version, e.g. 5.8"),
  free: z.boolean().optional().describe("Only free listings (default false)"),
  jobId: z.string().optional().describe("Job to cancel; verified files and native staging remain resumable (fab_cancel_download). Job identifier; omit to list all editor-session jobs (fab_download_status). Completed, SHA-verified asset-pack download to copy into project Content, preserving paths (fab_install_pack). Active download job identifier (fab_pause_download). Remove only unchanged, unloaded files that this job created in project Content (fab_remove_installation). Paused, failed or cancelled job identifier; obtains a fresh distribution signature (fab_resume_download)"),
  limit: z.number().int().optional().describe("Result limit, 1-200 (default 50)"),
  listingId: z.string().optional().describe("Owned Fab listing UUID"),
  listingType: z.string().optional().describe("Listing type filter, e.g. 3d-model, material, tool-and-plugin"),
  offset: z.number().int().optional().describe("Result offset (default 0)"),
  open: z.boolean().optional().describe("Open the page in the default browser (default true); false only returns the URL"),
  outputDirectory: z.string().optional().describe("Absolute pack output directory (default native Fab cache/UE-MCP/listing/artifact)"),
  platform: z.string().optional().describe("Exact target platform from projectVersions, e.g. Mac or Windows"),
  query: z.string().optional().describe("Filter owned item titles, no catalog requests (fab_list_owned). Search text (fab_open_search)"),
  source: z.string().optional().describe("Absolute path of the source file on disk"),
  sourceFile: z.string().optional().describe("Alias for source"),
  verifyOnly: z.boolean().optional().describe("SHA verify existing files without repairing or downloading (default false)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
