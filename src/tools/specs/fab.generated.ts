// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd fab handler. */
export const handlerSpecs: HandlerSpecs = {
  "fab_cache_info": {
    "category": "fab",
    "params": []
  },
  "fab_clear_cache": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and deletes the Fab download cache when called"
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
  "fab_list_cached": {
    "category": "fab",
    "params": []
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
        "description": "Library items to pull per sync request (default the plugin's own)"
      }
    ],
    "contractExempt": "Queues a Fab library sync whatever the batch size"
  }
};

/** Every key the spec'd fab handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
