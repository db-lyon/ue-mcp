// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd physics handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_impulse": {
    "category": "physics",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor editor label; pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "impulse",
        "type": "vec3",
        "required": true,
        "description": "Impulse or force vector {x,y,z}",
        "aliases": [
          "force",
          "vector"
        ]
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "impulse (default) | force"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component to push (default: the root, else the first primitive)"
      },
      {
        "name": "boneName",
        "type": "string",
        "required": false,
        "description": "Physics bone to push"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World point to apply the impulse at; ignores mode"
      },
      {
        "name": "velChange",
        "type": "boolean",
        "required": false,
        "description": "Treat the impulse as a velocity change"
      },
      {
        "name": "accelChange",
        "type": "boolean",
        "required": false,
        "description": "Treat the force as an acceleration change"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "World scope: auto (default) | pie | editor"
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "set_collision": {
    "category": "physics",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor editor label; pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "Blueprint whose component template to edit; needs componentName"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Primitive component name (prefix match on an actor, default every one); required with assetPath"
      },
      {
        "name": "collisionProfile",
        "type": "string",
        "required": false,
        "description": "Collision profile (preset) name, applied before the overrides"
      },
      {
        "name": "collisionEnabled",
        "type": "string",
        "required": false,
        "description": "NoCollision | QueryOnly | PhysicsOnly | QueryAndPhysics"
      },
      {
        "name": "objectType",
        "type": "string",
        "required": false,
        "description": "Object-type channel name, e.g. WorldStatic, Pawn, ECC_GameTraceChannel1"
      },
      {
        "name": "responseToAllChannels",
        "type": "string",
        "required": false,
        "description": "Block | Overlap | Ignore, applied to every channel before the per-channel responses"
      },
      {
        "name": "responses",
        "type": "object",
        "required": false,
        "description": "Per-channel responses, {channelName: Block | Overlap | Ignore}"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ],
          [
            "assetPath"
          ]
        ]
      }
    ]
  },
  "set_collision_enabled": {
    "category": "physics",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor editor label; pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "collisionEnabled",
        "type": "string",
        "required": true,
        "description": "NoCollision | QueryOnly | PhysicsOnly | QueryAndPhysics",
        "aliases": [
          "collisionType"
        ]
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "set_collision_profile": {
    "category": "physics",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor editor label; pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "profileName",
        "type": "string",
        "required": true,
        "description": "Collision profile (preset) name"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "set_physics_properties": {
    "category": "physics",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor editor label; pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "mass",
        "type": "number",
        "required": false,
        "description": "Mass override in kg"
      },
      {
        "name": "linearDamping",
        "type": "number",
        "required": false,
        "description": "Linear damping"
      },
      {
        "name": "angularDamping",
        "type": "number",
        "required": false,
        "description": "Angular damping"
      },
      {
        "name": "enableGravity",
        "type": "boolean",
        "required": false,
        "description": "Whether gravity applies to the body"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "set_simulate_physics": {
    "category": "physics",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Actor editor label; pass actorLabel or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "simulate",
        "type": "boolean",
        "required": true,
        "description": "Turn physics simulation on or off",
        "aliases": [
          "enabled"
        ]
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_impulse: "Params: actorLabel OR actorPath, impulse (or force, or vector), mode?, componentName?, boneName?, location?, velChange?, accelChange?, world?, pieInstance?",
  set_collision: "Params: actorLabel OR actorPath OR assetPath, componentName?, collisionProfile?, collisionEnabled?, objectType?, responseToAllChannels?, responses?",
  set_collision_enabled: "Params: actorLabel OR actorPath, collisionEnabled (or collisionType)",
  set_collision_profile: "Params: actorLabel OR actorPath, profileName",
  set_physics_properties: "Params: actorLabel OR actorPath, mass?, linearDamping?, angularDamping?, enableGravity?",
  set_simulate_physics: "Params: actorLabel OR actorPath, simulate (or enabled)",
};

/** Every key the spec'd physics handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
