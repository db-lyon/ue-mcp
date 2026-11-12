// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd networking handler. */
export const handlerSpecs: HandlerSpecs = {
  "audit_replication": {
    "category": "networking",
    "params": [
      {
        "name": "path",
        "type": "string",
        "required": false,
        "description": "Scope: a content path for Blueprints (default /Game) or a /Script/<Module> path for native classes; filters both"
      },
      {
        "name": "className",
        "type": "string",
        "required": false,
        "description": "Only this class and its subclasses: a native class name or path, or a Blueprint asset path"
      },
      {
        "name": "includeNative",
        "type": "boolean",
        "required": false,
        "description": "Audit native classes (default true)"
      },
      {
        "name": "includeBlueprints",
        "type": "boolean",
        "required": false,
        "description": "Audit Blueprint classes (default true)"
      },
      {
        "name": "includeEngine",
        "type": "boolean",
        "required": false,
        "description": "Also audit engine and engine-plugin native classes, not only project modules and project plugins (default false)"
      },
      {
        "name": "requireServerValidation",
        "type": "boolean",
        "required": false,
        "description": "Report every Server RPC without WithValidation (default false)"
      },
      {
        "name": "onlyWithFindings",
        "type": "boolean",
        "required": false,
        "description": "Keep only the classes that have findings (default false)"
      },
      {
        "name": "maxBlueprints",
        "type": "integer",
        "required": false,
        "description": "Cap on Blueprints loaded (default 500, max 5000)"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Class rows to return inline (default 200, max 5000); totals stay complete"
      },
      {
        "name": "dumpToFile",
        "type": "boolean",
        "required": false,
        "description": "Write every row to a JSON file instead of returning them inline"
      },
      {
        "name": "outputPath",
        "type": "string",
        "required": false,
        "description": "Absolute or Saved-relative JSON path for the dump (default Saved/UE_MCP/Audits/replication_audit.json)"
      }
    ]
  },
  "configure_net_cull_distance": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "netCullDistanceSquared",
        "type": "number",
        "required": false,
        "description": "NetCullDistanceSquared (default 225000000)"
      }
    ]
  },
  "configure_net_update_frequency": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "netUpdateFrequency",
        "type": "number",
        "required": false,
        "description": "NetUpdateFrequency in updates per second (omit to leave it)"
      },
      {
        "name": "minNetUpdateFrequency",
        "type": "number",
        "required": false,
        "description": "MinNetUpdateFrequency in updates per second (omit to leave it)"
      }
    ]
  },
  "get_networking_info": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      }
    ]
  },
  "set_always_relevant": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "alwaysRelevant",
        "type": "boolean",
        "required": false,
        "description": "bAlwaysRelevant (default false)"
      }
    ]
  },
  "set_net_dormancy": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "dormancy",
        "type": "string",
        "required": true,
        "description": "DORM_Never | DORM_Awake | DORM_DormantAll | DORM_DormantPartial | DORM_Initial"
      }
    ]
  },
  "set_net_load_on_client": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "loadOnClient",
        "type": "boolean",
        "required": false,
        "description": "bNetLoadOnClient (default true)"
      }
    ]
  },
  "set_net_priority": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "netPriority",
        "type": "number",
        "required": false,
        "description": "NetPriority (default 1.0)"
      }
    ]
  },
  "set_only_relevant_to_owner": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "onlyRelevantToOwner",
        "type": "boolean",
        "required": false,
        "description": "bOnlyRelevantToOwner (default false)"
      }
    ]
  },
  "set_property_replicated": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "variableName",
        "type": "string",
        "required": true,
        "description": "Blueprint variable name",
        "aliases": [
          "propertyName"
        ]
      },
      {
        "name": "replicationType",
        "type": "string",
        "required": false,
        "description": "None | Replicated | RepNotify (default None). Wins over replicated and repNotify"
      },
      {
        "name": "replicated",
        "type": "boolean",
        "required": false,
        "description": "Shorthand: true is Replicated, false is None"
      },
      {
        "name": "repNotify",
        "type": "boolean",
        "required": false,
        "description": "Shorthand: true is RepNotify, and wins over replicated"
      }
    ]
  },
  "set_replicate_movement": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "replicateMovement",
        "type": "boolean",
        "required": false,
        "description": "Replicate movement (default false)"
      }
    ]
  },
  "set_replicates": {
    "category": "networking",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Actor Blueprint asset path"
      },
      {
        "name": "replicates",
        "type": "boolean",
        "required": false,
        "description": "Replicate the actor (default false)"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  audit_replication: "Params: path?, className?, includeNative?, includeBlueprints?, includeEngine?, requireServerValidation?, onlyWithFindings?, maxBlueprints?, limit?, dumpToFile?, outputPath?",
  configure_net_cull_distance: "Params: blueprintPath, netCullDistanceSquared?",
  configure_net_update_frequency: "Params: blueprintPath, netUpdateFrequency?, minNetUpdateFrequency?",
  get_networking_info: "Params: blueprintPath",
  set_always_relevant: "Params: blueprintPath, alwaysRelevant?",
  set_net_dormancy: "Params: blueprintPath, dormancy",
  set_net_load_on_client: "Params: blueprintPath, loadOnClient?",
  set_net_priority: "Params: blueprintPath, netPriority?",
  set_only_relevant_to_owner: "Params: blueprintPath, onlyRelevantToOwner?",
  set_property_replicated: "Params: blueprintPath, variableName (or propertyName), replicationType?, replicated?, repNotify?",
  set_replicate_movement: "Params: blueprintPath, replicateMovement?",
  set_replicates: "Params: blueprintPath, replicates?",
};

/** Every key the spec'd networking handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  alwaysRelevant: z.boolean().optional().describe("bAlwaysRelevant (default false)"),
  blueprintPath: z.string().optional().describe("Actor Blueprint asset path"),
  className: z.string().optional().describe("Only this class and its subclasses: a native class name or path, or a Blueprint asset path"),
  dormancy: z.string().optional().describe("DORM_Never | DORM_Awake | DORM_DormantAll | DORM_DormantPartial | DORM_Initial"),
  dumpToFile: z.boolean().optional().describe("Write every row to a JSON file instead of returning them inline"),
  includeBlueprints: z.boolean().optional().describe("Audit Blueprint classes (default true)"),
  includeEngine: z.boolean().optional().describe("Also audit engine and engine-plugin native classes, not only project modules and project plugins (default false)"),
  includeNative: z.boolean().optional().describe("Audit native classes (default true)"),
  limit: z.number().int().optional().describe("Class rows to return inline (default 200, max 5000); totals stay complete"),
  loadOnClient: z.boolean().optional().describe("bNetLoadOnClient (default true)"),
  maxBlueprints: z.number().int().optional().describe("Cap on Blueprints loaded (default 500, max 5000)"),
  minNetUpdateFrequency: z.number().optional().describe("MinNetUpdateFrequency in updates per second (omit to leave it)"),
  netCullDistanceSquared: z.number().optional().describe("NetCullDistanceSquared (default 225000000)"),
  netPriority: z.number().optional().describe("NetPriority (default 1.0)"),
  netUpdateFrequency: z.number().optional().describe("NetUpdateFrequency in updates per second (omit to leave it)"),
  onlyRelevantToOwner: z.boolean().optional().describe("bOnlyRelevantToOwner (default false)"),
  onlyWithFindings: z.boolean().optional().describe("Keep only the classes that have findings (default false)"),
  outputPath: z.string().optional().describe("Absolute or Saved-relative JSON path for the dump (default Saved/UE_MCP/Audits/replication_audit.json)"),
  path: z.string().optional().describe("Scope: a content path for Blueprints (default /Game) or a /Script/<Module> path for native classes; filters both"),
  propertyName: z.string().optional().describe("Alias for variableName"),
  replicated: z.boolean().optional().describe("Shorthand: true is Replicated, false is None"),
  replicateMovement: z.boolean().optional().describe("Replicate movement (default false)"),
  replicates: z.boolean().optional().describe("Replicate the actor (default false)"),
  replicationType: z.string().optional().describe("None | Replicated | RepNotify (default None). Wins over replicated and repNotify"),
  repNotify: z.boolean().optional().describe("Shorthand: true is RepNotify, and wins over replicated"),
  requireServerValidation: z.boolean().optional().describe("Report every Server RPC without WithValidation (default false)"),
  variableName: z.string().optional().describe("Blueprint variable name"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
