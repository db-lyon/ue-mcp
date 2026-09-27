// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd foliage handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_foliage_instances": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": true,
        "description": "FoliageType asset path, or the name of a type already placed in the open level"
      },
      {
        "name": "transforms",
        "type": "array",
        "required": false,
        "description": "Place one instance per entry at exactly {location, rotation?, scale?}",
        "items": "object"
      },
      {
        "name": "center",
        "type": "vec3",
        "required": false,
        "description": "Centre of the scatter disc"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Scatter disc radius in centimetres"
      },
      {
        "name": "count",
        "type": "integer",
        "required": false,
        "description": "Instances to scatter over the disc"
      },
      {
        "name": "seed",
        "type": "integer",
        "required": false,
        "description": "Random seed for scatter placement (default 0)"
      },
      {
        "name": "projectToGround",
        "type": "boolean",
        "required": false,
        "description": "Trace each candidate onto the geometry beneath it (default true)"
      },
      {
        "name": "traceUp",
        "type": "number",
        "required": false,
        "description": "How far above each candidate the ground trace starts, in centimetres (default 10000)"
      },
      {
        "name": "traceDown",
        "type": "number",
        "required": false,
        "description": "How far below each candidate the ground trace ends, in centimetres (default 100000)"
      },
      {
        "name": "applyTypeRules",
        "type": "boolean",
        "required": false,
        "description": "Apply the FoliageType's own scale, rotation, align, slope, height and collision rules (default true)"
      },
      {
        "name": "skipCollision",
        "type": "boolean",
        "required": false,
        "description": "Skip the type's CollisionWithWorld test (default false)"
      }
    ]
  },
  "add_foliage_type_to_level": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": true,
        "description": "FoliageType asset path"
      }
    ]
  },
  "batch_set_foliage_settings_where": {
    "category": "foliage",
    "params": [
      {
        "name": "settings",
        "type": "object",
        "required": true,
        "description": "Property name to value, dotted paths supported"
      },
      {
        "name": "where",
        "type": "array",
        "required": true,
        "description": "Predicate over each type's current values: [{field, op, value}]",
        "items": "object"
      },
      {
        "name": "whereMode",
        "type": "string",
        "required": false,
        "description": "all (default) | any"
      },
      {
        "name": "foliageTypePaths",
        "type": "array",
        "required": false,
        "description": "Explicit FoliageType asset paths",
        "items": "string",
        "role": "editTarget"
      },
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content directory to scan for FoliageType assets"
      },
      {
        "name": "recursive",
        "type": "boolean",
        "required": false,
        "description": "Scan directory recursively (default true)"
      },
      {
        "name": "fromLevel",
        "type": "boolean",
        "required": false,
        "description": "Take the foliage types placed in the open level"
      },
      {
        "name": "propertyNames",
        "type": "array",
        "required": false,
        "description": "Extra properties to report on each row without filtering on them",
        "items": "string"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Preview without writing (default TRUE)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save each changed FoliageType package (default true)"
      },
      {
        "name": "maxTypes",
        "type": "integer",
        "required": false,
        "description": "Refuse to scan more than this many types (default 2000)"
      }
    ]
  },
  "clear_procedural_foliage": {
    "category": "foliage",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of a ProceduralFoliageVolume, or of any actor with a ProceduralFoliageComponent"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Object path of that actor"
      },
      {
        "name": "spawnerPath",
        "type": "string",
        "required": false,
        "description": "Clear every volume in the open level bound to this ProceduralFoliageSpawner"
      }
    ]
  },
  "create_foliage_type": {
    "category": "foliage",
    "params": [
      {
        "name": "meshPath",
        "type": "string",
        "required": true,
        "description": "StaticMesh the foliage type places"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name (default FT_<mesh name>)"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder for the asset (default /Game/Foliage)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the asset already exists: skip (default) | error"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "Property name to value, applied to the new FoliageType"
      }
    ]
  },
  "get_foliage_instances": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": false,
        "description": "FoliageType asset path, or the name of a type already placed in the open level"
      },
      {
        "name": "center",
        "type": "vec3",
        "required": false,
        "description": "Centre of the filter sphere"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Filter sphere radius in centimetres"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Maximum instances to return (default 200, max 20000)"
      },
      {
        "name": "startIndex",
        "type": "integer",
        "required": false,
        "description": "Skip this many matching instances first"
      },
      {
        "name": "includeTransforms",
        "type": "boolean",
        "required": false,
        "description": "Return each instance's location, rotation and scale (default true)"
      }
    ]
  },
  "get_foliage_type_settings": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": true,
        "description": "FoliageType asset path",
        "aliases": [
          "foliageTypeName"
        ]
      }
    ]
  },
  "list_foliage_types": {
    "category": "foliage",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows on this page (default 200, max 2000)"
      }
    ]
  },
  "read_procedural_foliage_spawner": {
    "category": "foliage",
    "params": [
      {
        "name": "spawnerPath",
        "type": "string",
        "required": true,
        "description": "ProceduralFoliageSpawner asset path"
      }
    ]
  },
  "remove_foliage_instances": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": true,
        "description": "FoliageType asset path, or the name of a type already placed in the open level"
      },
      {
        "name": "instanceIndices",
        "type": "array",
        "required": false,
        "description": "Instance indices from get_foliage_instances",
        "items": "integer"
      },
      {
        "name": "transforms",
        "type": "array",
        "required": false,
        "description": "Remove the instance nearest each location, within matchTolerance",
        "items": "object"
      },
      {
        "name": "center",
        "type": "vec3",
        "required": false,
        "description": "Centre of the removal sphere"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Removal sphere radius in centimetres"
      },
      {
        "name": "all",
        "type": "boolean",
        "required": false,
        "description": "Remove every instance of the type"
      },
      {
        "name": "matchTolerance",
        "type": "number",
        "required": false,
        "description": "How close an instance must be to a transforms location, in centimetres (default 1)"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Only this InstancedFoliageActor"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Report what would be removed without removing it"
      }
    ]
  },
  "remove_foliage_type_from_level": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": true,
        "description": "FoliageType asset path, or the name of a type already placed in the open level"
      },
      {
        "name": "force",
        "type": "boolean",
        "required": false,
        "description": "Remove the type even though that destroys its instances"
      }
    ]
  },
  "sample_foliage": {
    "category": "foliage",
    "params": [
      {
        "name": "center",
        "type": "vec3",
        "required": true,
        "description": "Centre of the sample sphere"
      },
      {
        "name": "radius",
        "type": "number",
        "required": false,
        "description": "Sphere radius in centimetres (default 1000)"
      }
    ]
  },
  "set_foliage_type_settings": {
    "category": "foliage",
    "params": [
      {
        "name": "foliageTypePath",
        "type": "string",
        "required": true,
        "description": "FoliageType asset path, or the name of a type already placed in the open level",
        "aliases": [
          "foliageTypeName"
        ],
        "role": "editTarget"
      },
      {
        "name": "settings",
        "type": "object",
        "required": true,
        "description": "Property name to value; each is imported as text onto the FoliageType"
      }
    ]
  },
  "set_procedural_foliage_spawner_types": {
    "category": "foliage",
    "params": [
      {
        "name": "spawnerPath",
        "type": "string",
        "required": true,
        "description": "ProceduralFoliageSpawner asset path",
        "role": "editTarget"
      },
      {
        "name": "foliageTypePaths",
        "type": "array",
        "required": true,
        "description": "FoliageType assets, or Blueprints whose class derives from FoliageType",
        "items": "string"
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "replace (default) | add | remove"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the spawner package after a change (default true)"
      }
    ]
  },
  "simulate_procedural_foliage": {
    "category": "foliage",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Label of a ProceduralFoliageVolume, or of any actor with a ProceduralFoliageComponent"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Object path of that actor"
      },
      {
        "name": "spawnerPath",
        "type": "string",
        "required": false,
        "description": "Run every volume in the open level bound to this ProceduralFoliageSpawner"
      },
      {
        "name": "clearExisting",
        "type": "boolean",
        "required": false,
        "description": "Remove what the component spawned before simulating again (default true)"
      },
      {
        "name": "skipCollision",
        "type": "boolean",
        "required": false,
        "description": "Skip the type's CollisionWithWorld test (default false)"
      }
    ]
  }
};

/** Every key the spec'd foliage handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
