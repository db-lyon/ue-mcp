// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd metahuman handler. */
export const handlerSpecs: HandlerSpecs = {
  "metahuman_assemble": {
    "category": "metahuman",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaHumanCharacter asset path",
        "aliases": [
          "characterPath"
        ]
      },
      {
        "name": "pipelineType",
        "type": "string",
        "required": false,
        "description": "Cinematic | Optimized (default) | UEFN"
      },
      {
        "name": "quality",
        "type": "string",
        "required": false,
        "description": "Pipeline quality level, e.g. Cinematic | High | Medium (default) | Low"
      },
      {
        "name": "nameOverride",
        "type": "string",
        "required": false,
        "description": "Name for the assembled assets instead of the character's"
      },
      {
        "name": "commonFolderPath",
        "type": "string",
        "required": false,
        "description": "Content folder for the shared MetaHuman assets"
      }
    ]
  },
  "metahuman_create_character": {
    "category": "metahuman",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Name of the new MetaHumanCharacter asset"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game/MetaHumans)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default) returns an existing asset untouched, error refuses"
      }
    ],
    "contractExempt": "creates a MetaHumanCharacter asset"
  },
  "metahuman_read_character": {
    "category": "metahuman",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaHumanCharacter asset path",
        "aliases": [
          "characterPath"
        ]
      },
      {
        "name": "includeBody",
        "type": "boolean",
        "required": false,
        "description": "Open the character for editing to read body constraints, wardrobe and canBuild (default true)"
      }
    ]
  },
  "metahuman_request_rig": {
    "category": "metahuman",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaHumanCharacter asset path",
        "aliases": [
          "characterPath"
        ]
      },
      {
        "name": "rigType",
        "type": "string",
        "required": false,
        "description": "JointsOnly (default) | JointsAndBlendshapes"
      },
      {
        "name": "blocking",
        "type": "boolean",
        "required": false,
        "description": "Wait for the cloud service to return the rig (default true)"
      }
    ]
  },
  "metahuman_request_textures": {
    "category": "metahuman",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaHumanCharacter asset path",
        "aliases": [
          "characterPath"
        ]
      },
      {
        "name": "blocking",
        "type": "boolean",
        "required": false,
        "description": "Wait for the cloud service to return the textures (default true)"
      }
    ]
  },
  "metahuman_set_body": {
    "category": "metahuman",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaHumanCharacter asset path",
        "aliases": [
          "characterPath"
        ]
      },
      {
        "name": "constraints",
        "type": "object",
        "required": true,
        "description": "{ConstraintName: target} or {ConstraintName: {target, active}}, e.g. {\"Height\": 180, \"Muscularity\": 0.9}"
      }
    ]
  },
  "metahuman_set_wardrobe": {
    "category": "metahuman",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MetaHumanCharacter asset path",
        "aliases": [
          "characterPath"
        ]
      },
      {
        "name": "items",
        "type": "object",
        "required": true,
        "description": "{slotName: wardrobeItemPath}, e.g. {\"Top Garment\": \"/Game/Fab/MetaHuman/WI_Crewneck\"}"
      }
    ]
  },
  "metahuman_status": {
    "category": "metahuman",
    "params": []
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  metahuman_assemble: "Params: assetPath (or characterPath), pipelineType?, quality?, nameOverride?, commonFolderPath?",
  metahuman_create_character: "Params: name, packagePath?, onConflict?",
  metahuman_read_character: "Params: assetPath (or characterPath), includeBody?",
  metahuman_request_rig: "Params: assetPath (or characterPath), rigType?, blocking?",
  metahuman_request_textures: "Params: assetPath (or characterPath), blocking?",
  metahuman_set_body: "Params: assetPath (or characterPath), constraints",
  metahuman_set_wardrobe: "Params: assetPath (or characterPath), items",
  metahuman_status: "Params: none",
};

/** Every key the spec'd metahuman handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  assetPath: z.string().optional().describe("MetaHumanCharacter asset path"),
  blocking: z.boolean().optional().describe("Wait for the cloud service to return the rig (default true) (metahuman_request_rig). Wait for the cloud service to return the textures (default true) (metahuman_request_textures)"),
  characterPath: z.string().optional().describe("Alias for assetPath"),
  commonFolderPath: z.string().optional().describe("Content folder for the shared MetaHuman assets"),
  constraints: z.record(z.string(), z.unknown()).optional().describe("{ConstraintName: target} or {ConstraintName: {target, active}}, e.g. {\"Height\": 180, \"Muscularity\": 0.9}"),
  includeBody: z.boolean().optional().describe("Open the character for editing to read body constraints, wardrobe and canBuild (default true)"),
  items: z.record(z.string(), z.unknown()).optional().describe("{slotName: wardrobeItemPath}, e.g. {\"Top Garment\": \"/Game/Fab/MetaHuman/WI_Crewneck\"}"),
  name: z.string().optional().describe("Name of the new MetaHumanCharacter asset"),
  nameOverride: z.string().optional().describe("Name for the assembled assets instead of the character's"),
  onConflict: z.string().optional().describe("skip (default) returns an existing asset untouched, error refuses"),
  packagePath: z.string().optional().describe("Destination folder (default /Game/MetaHumans)"),
  pipelineType: z.string().optional().describe("Cinematic | Optimized (default) | UEFN"),
  quality: z.string().optional().describe("Pipeline quality level, e.g. Cinematic | High | Medium (default) | Low"),
  rigType: z.string().optional().describe("JointsOnly (default) | JointsAndBlendshapes"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
