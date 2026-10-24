// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd datasmith handler. */
export const handlerSpecs: HandlerSpecs = {
  "datasmith_import": {
    "category": "datasmith",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Absolute path of the .udatasmith file to import"
      },
      {
        "name": "destinationPath",
        "type": "string",
        "required": true,
        "description": "Content folder for the scene asset and its assets, e.g. /Game/Datasmith/Site"
      },
      {
        "name": "options",
        "type": "object",
        "required": false,
        "description": "{dotted.option.path: value} on DatasmithImportOptions, e.g. {\"BaseOptions.bIncludeLight\": false, \"BaseOptions.SceneHandling\": \"AssetsOnly\"}; datasmith(inspect) lists them"
      }
    ]
  },
  "datasmith_inspect": {
    "category": "datasmith",
    "params": [
      {
        "name": "filePath",
        "type": "string",
        "required": true,
        "description": "Absolute path of the .udatasmith (or other Datasmith-translatable) file"
      }
    ]
  },
  "datasmith_query_actors": {
    "category": "datasmith",
    "params": [
      {
        "name": "tagPrefix",
        "type": "string",
        "required": false,
        "description": "Match actors with a tag starting with this, e.g. Revit.Element.Id."
      },
      {
        "name": "tags",
        "type": "array",
        "required": false,
        "description": "Match actors carrying any of these exact tags",
        "items": "string"
      },
      {
        "name": "metadataKey",
        "type": "string",
        "required": false,
        "description": "Match actors whose Datasmith metadata has this key"
      },
      {
        "name": "metadataValue",
        "type": "string",
        "required": false,
        "description": "With metadataKey, match only this value"
      },
      {
        "name": "includeMetadata",
        "type": "boolean",
        "required": false,
        "description": "Return each actor's Datasmith user-data metadata (default true)"
      },
      {
        "name": "includeBounds",
        "type": "boolean",
        "required": false,
        "description": "Return each actor's world bounds and the union (default true)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return (default 200, max 5000); matchedCount counts them all"
      }
    ]
  },
  "datasmith_reimport": {
    "category": "datasmith",
    "params": [
      {
        "name": "sceneAssetPath",
        "type": "string",
        "required": true,
        "description": "The DatasmithScene asset to reimport"
      },
      {
        "name": "filePath",
        "type": "string",
        "required": false,
        "description": "New source file path, when the source moved or changed name"
      },
      {
        "name": "syncActors",
        "type": "boolean",
        "required": false,
        "description": "Update the scene's actors in the level from the new source (default true)"
      },
      {
        "name": "respawnDeleted",
        "type": "boolean",
        "required": false,
        "description": "Respawn scene actors that were deleted from the level (default false)"
      },
      {
        "name": "options",
        "type": "object",
        "required": false,
        "description": "{dotted.option.path: value} applied before the reimport"
      }
    ]
  },
  "datasmith_status": {
    "category": "datasmith",
    "params": []
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  datasmith_import: "Params: filePath, destinationPath, options?",
  datasmith_inspect: "Params: filePath",
  datasmith_query_actors: "Params: tagPrefix?, tags?, metadataKey?, metadataValue?, includeMetadata?, includeBounds?, limit?",
  datasmith_reimport: "Params: sceneAssetPath, filePath?, syncActors?, respawnDeleted?, options?",
  datasmith_status: "Params: none",
};

/** Every key the spec'd datasmith handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  destinationPath: z.string().optional().describe("Content folder for the scene asset and its assets, e.g. /Game/Datasmith/Site"),
  filePath: z.string().optional().describe("Absolute path of the .udatasmith file to import (datasmith_import). Absolute path of the .udatasmith (or other Datasmith-translatable) file (datasmith_inspect). New source file path, when the source moved or changed name (datasmith_reimport)"),
  includeBounds: z.boolean().optional().describe("Return each actor's world bounds and the union (default true)"),
  includeMetadata: z.boolean().optional().describe("Return each actor's Datasmith user-data metadata (default true)"),
  limit: z.number().int().optional().describe("Rows to return (default 200, max 5000); matchedCount counts them all"),
  metadataKey: z.string().optional().describe("Match actors whose Datasmith metadata has this key"),
  metadataValue: z.string().optional().describe("With metadataKey, match only this value"),
  options: z.record(z.string(), z.unknown()).optional().describe("{dotted.option.path: value} on DatasmithImportOptions, e.g. {\"BaseOptions.bIncludeLight\": false, \"BaseOptions.SceneHandling\": \"AssetsOnly\"}; datasmith(inspect) lists them (datasmith_import). {dotted.option.path: value} applied before the reimport (datasmith_reimport)"),
  respawnDeleted: z.boolean().optional().describe("Respawn scene actors that were deleted from the level (default false)"),
  sceneAssetPath: z.string().optional().describe("The DatasmithScene asset to reimport"),
  syncActors: z.boolean().optional().describe("Update the scene's actors in the level from the new source (default true)"),
  tagPrefix: z.string().optional().describe("Match actors with a tag starting with this, e.g. Revit.Element.Id."),
  tags: z.array(z.string()).optional().describe("Match actors carrying any of these exact tags"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
