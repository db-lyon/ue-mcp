// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd internal handler. */
export const handlerSpecs: HandlerSpecs = {
  "internal_create_constant_material": {
    "category": "internal",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Material asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new material (default /Game/Materials)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset exists: skip (default, report it) | error"
      },
      {
        "name": "baseColor",
        "type": "color",
        "required": false,
        "description": "Linear base colour {r,g,b,a}"
      },
      {
        "name": "metallic",
        "type": "number",
        "required": false,
        "description": "Metallic constant"
      },
      {
        "name": "roughness",
        "type": "number",
        "required": false,
        "description": "Roughness constant"
      },
      {
        "name": "emissiveColor",
        "type": "color",
        "required": false,
        "description": "Linear emissive colour {r,g,b,a}"
      },
      {
        "name": "emissiveStrength",
        "type": "number",
        "required": false,
        "description": "Multiplier on emissiveColor (default 1)"
      }
    ],
    "contractExempt": "Creates and saves a material from constants alone, with nothing to load that could fail first"
  },
  "internal_spawn_actor": {
    "category": "internal",
    "params": [
      {
        "name": "actorClass",
        "type": "string",
        "required": true,
        "description": "Actor class: short name, /Script path or Blueprint class path"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Actor label, applied even when another actor carries it"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location"
      },
      {
        "name": "rotation",
        "type": "rotator",
        "required": false,
        "description": "World rotation"
      },
      {
        "name": "scale",
        "type": "vec3",
        "required": false,
        "description": "Actor scale"
      },
      {
        "name": "staticMesh",
        "type": "string",
        "required": false,
        "description": "Static mesh for a StaticMeshActor"
      },
      {
        "name": "material",
        "type": "string",
        "required": false,
        "description": "Material applied at slot 0"
      },
      {
        "name": "folderPath",
        "type": "string",
        "required": false,
        "description": "World Outliner folder"
      }
    ]
  }
};

/** Every key the spec'd internal handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
