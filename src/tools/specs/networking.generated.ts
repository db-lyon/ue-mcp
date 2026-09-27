// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd networking handler. */
export const handlerSpecs: HandlerSpecs = {
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

/** Every key the spec'd networking handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
