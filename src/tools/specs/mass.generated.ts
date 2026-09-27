// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd mass handler. */
export const handlerSpecs: HandlerSpecs = {
  "ensure_mass_entity_config": {
    "category": "mass",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "MassEntityConfigAsset path, /Game/Folder/Name[.Name]"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Asset name, when assetPath is not given"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder for name (default /Game)"
      },
      {
        "name": "traits",
        "type": "array",
        "required": true,
        "description": "Ordered trait list; the order is the asset's order and a class may appear once",
        "items": "object",
        "fields": [
          {
            "name": "class",
            "type": "string",
            "required": false,
            "description": "Concrete UMassEntityTraitBase subclass, short name or /Script path; this or traitClass"
          },
          {
            "name": "traitClass",
            "type": "string",
            "required": false,
            "description": "Same as class"
          },
          {
            "name": "properties",
            "type": "object",
            "required": false,
            "description": "Fields written onto the trait instance; dotted paths allowed"
          }
        ]
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip | error | update (default): what an existing asset gets"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "assetPath"
          ],
          [
            "name"
          ]
        ]
      }
    ]
  },
  "read_mass_entity_config": {
    "category": "mass",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "MassEntityConfigAsset path"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  ensure_mass_entity_config: "Params: assetPath OR name, packagePath?, traits, onConflict?",
  read_mass_entity_config: "Params: assetPath",
};

/** Every key the spec'd mass handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
