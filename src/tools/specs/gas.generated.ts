// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd gas handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_ability_system_component": {
    "category": "gas",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path"
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "Name of the AbilitySystemComponent (default AbilitySystemComp)"
      }
    ]
  },
  "add_attribute": {
    "category": "gas",
    "params": [
      {
        "name": "attributeSetPath",
        "type": "string",
        "required": true,
        "description": "AttributeSet Blueprint asset path"
      },
      {
        "name": "attributeName",
        "type": "string",
        "required": true,
        "description": "FGameplayAttributeData variable to add"
      }
    ]
  },
  "add_effect_cue": {
    "category": "gas",
    "params": [
      {
        "name": "effectPath",
        "type": "string",
        "required": true,
        "description": "GameplayEffect Blueprint asset path",
        "aliases": [
          "effectClass"
        ]
      },
      {
        "name": "cueTag",
        "type": "string",
        "required": true,
        "description": "Registered GameplayCue tag, under the GameplayCue root"
      },
      {
        "name": "minLevel",
        "type": "number",
        "required": false,
        "description": "Lowest effect level this cue covers, used to normalise the magnitude"
      },
      {
        "name": "maxLevel",
        "type": "number",
        "required": false,
        "description": "Highest effect level this cue covers"
      },
      {
        "name": "magnitudeAttribute",
        "type": "string",
        "required": false,
        "description": "Attribute the cue takes its magnitude from (SetName.Attribute), instead of the effect level"
      }
    ]
  },
  "add_loose_gameplay_tag": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "tag",
        "type": "string",
        "required": true,
        "description": "A registered gameplay tag"
      },
      {
        "name": "count",
        "type": "integer",
        "required": false,
        "description": "References to add, at least 1 (default 1)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "apply_effect": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "effectClass",
        "type": "string",
        "required": true,
        "description": "GameplayEffect content path or class name",
        "aliases": [
          "effectPath"
        ]
      },
      {
        "name": "level",
        "type": "number",
        "required": false,
        "description": "Effect level (default 1)"
      },
      {
        "name": "setByCaller",
        "type": "object",
        "required": false,
        "description": "SetByCaller magnitudes keyed by gameplay tag or name"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "audit_attributes": {
    "category": "gas",
    "params": [
      {
        "name": "attributeSet",
        "type": "string",
        "required": false,
        "description": "AttributeSet content path or class name. Pass this, a live actor, or both"
      },
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "probeClamping",
        "type": "boolean",
        "required": false,
        "description": "Measure an existing clamp by driving the set's own PreAttributeChange. Needs a live registered set (default false)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "bind_ability_input": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "abilityClass",
        "type": "string",
        "required": true,
        "description": "Granted GameplayAbility class to bind"
      },
      {
        "name": "inputId",
        "type": "integer",
        "required": true,
        "description": "Input id to bind; -1 leaves the ability unbound"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "capture_gas_state": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "snapshotId",
        "type": "string",
        "required": false,
        "description": "Id to store the snapshot under (generated when omitted)"
      },
      {
        "name": "compareWith",
        "type": "string",
        "required": false,
        "description": "Earlier snapshot id to diff this capture against"
      },
      {
        "name": "registerOwnerSets",
        "type": "boolean",
        "required": false,
        "description": "Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "clear_ability_input": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "abilityClass",
        "type": "string",
        "required": true,
        "description": "Granted GameplayAbility class to unbind"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "compare_gas_states": {
    "category": "gas",
    "params": [
      {
        "name": "beforeId",
        "type": "string",
        "required": false,
        "description": "Snapshot id of the earlier reading. Pass this or beforeSnapshot"
      },
      {
        "name": "beforeSnapshot",
        "type": "object",
        "required": false,
        "description": "The earlier snapshot object itself"
      },
      {
        "name": "afterId",
        "type": "string",
        "required": false,
        "description": "Snapshot id of the later reading. Pass this or afterSnapshot"
      },
      {
        "name": "afterSnapshot",
        "type": "object",
        "required": false,
        "description": "The later snapshot object itself"
      }
    ]
  },
  "create_attribute_set": {
    "category": "gas",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/GAS/Attributes)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      }
    ],
    "contractExempt": "Creates, compiles and saves a Blueprint under the contract values; nothing it reads fails first"
  },
  "create_gameplay_ability": {
    "category": "gas",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/GAS/Abilities)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      }
    ],
    "contractExempt": "Creates, compiles and saves a Blueprint under the contract values; nothing it reads fails first"
  },
  "create_gameplay_cue": {
    "category": "gas",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/GAS/Cues)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      },
      {
        "name": "cueType",
        "type": "string",
        "required": false,
        "description": "Static (default, GameplayCueNotify_Static) | Actor (GameplayCueNotify_Actor)"
      }
    ],
    "contractExempt": "Creates, compiles and saves a Blueprint under the contract values; nothing it reads fails first"
  },
  "create_gameplay_effect": {
    "category": "gas",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Blueprint name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Content folder (default /Game/GAS/Effects)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "skip (default, returns the existing asset) or error when the asset already exists"
      },
      {
        "name": "durationPolicy",
        "type": "string",
        "required": false,
        "description": "Echoed back as durationPolicy (default Instant); it is not written onto the effect"
      }
    ],
    "contractExempt": "Creates, compiles and saves a Blueprint under the contract values; nothing it reads fails first"
  },
  "delete_gas_snapshot": {
    "category": "gas",
    "params": [
      {
        "name": "snapshotId",
        "type": "string",
        "required": true,
        "description": "Snapshot id to drop"
      }
    ]
  },
  "get_active_effects": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "get_asc_state": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "get_attribute": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "attribute",
        "type": "string",
        "required": false,
        "description": "Attribute to read. Omit to list every attribute"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "get_gas_info": {
    "category": "gas",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path to inspect"
      }
    ]
  },
  "get_live_attribute_value": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "attributeSet",
        "type": "string",
        "required": true,
        "description": "AttributeSet content path or class name"
      },
      {
        "name": "attribute",
        "type": "string",
        "required": true,
        "description": "Attribute property name, or Set.Property"
      },
      {
        "name": "registerOwnerSets",
        "type": "boolean",
        "required": false,
        "description": "Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "grant_ability": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "abilityClass",
        "type": "string",
        "required": true,
        "description": "GameplayAbility Blueprint path, generated class path, or native class name"
      },
      {
        "name": "level",
        "type": "number",
        "required": false,
        "description": "Ability level (default 1)"
      },
      {
        "name": "inputId",
        "type": "integer",
        "required": false,
        "description": "InputID for the spec (default -1, unbound)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "init_asc": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "attributeSet",
        "type": "string",
        "required": false,
        "description": "AttributeSet content path or class name to make sure is registered"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "list_gas_snapshots": {
    "category": "gas",
    "params": [
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Only snapshots of this actor object path"
      },
      {
        "name": "includeSnapshots",
        "type": "boolean",
        "required": false,
        "description": "Return the full snapshot bodies rather than a summary row each"
      }
    ]
  },
  "remove_effect": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "effectHandle",
        "type": "string",
        "required": false,
        "description": "Active-effect handle apply_effect reported. Removes exactly that effect"
      },
      {
        "name": "effectClass",
        "type": "string",
        "required": false,
        "description": "GameplayEffect content path or class name. Removes every active effect of that class",
        "aliases": [
          "effectPath"
        ]
      },
      {
        "name": "stacksToRemove",
        "type": "integer",
        "required": false,
        "description": "Stacks to take off (default -1, the whole effect)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "remove_effect_cue": {
    "category": "gas",
    "params": [
      {
        "name": "effectPath",
        "type": "string",
        "required": true,
        "description": "GameplayEffect Blueprint asset path",
        "aliases": [
          "effectClass"
        ]
      },
      {
        "name": "cueTag",
        "type": "string",
        "required": true,
        "description": "GameplayCue tag to unlink. May be one that is no longer registered"
      }
    ]
  },
  "remove_loose_gameplay_tag": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "tag",
        "type": "string",
        "required": true,
        "description": "A registered gameplay tag"
      },
      {
        "name": "count",
        "type": "integer",
        "required": false,
        "description": "References to remove, at least 1 (default 1)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "revoke_ability": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "abilityClass",
        "type": "string",
        "required": true,
        "description": "GameplayAbility class to revoke"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "send_ability_input": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "inputEvent",
        "type": "string",
        "required": false,
        "description": "pressed (default) | released | confirm | cancel. pressed and released address an inputId; confirm and cancel take none"
      },
      {
        "name": "inputId",
        "type": "integer",
        "required": false,
        "description": "Input id to send pressed or released to"
      },
      {
        "name": "abilityClass",
        "type": "string",
        "required": false,
        "description": "Granted ability whose bound input id to use instead of inputId"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "set_ability_tags": {
    "category": "gas",
    "params": [
      {
        "name": "abilityPath",
        "type": "string",
        "required": true,
        "description": "GameplayAbility Blueprint asset path"
      },
      {
        "name": "ability_tags",
        "type": "array",
        "required": false,
        "description": "AbilityTags container, written whole",
        "items": "string"
      },
      {
        "name": "cancel_abilities_with_tag",
        "type": "array",
        "required": false,
        "description": "CancelAbilitiesWithTag container, written whole",
        "items": "string"
      },
      {
        "name": "block_abilities_with_tag",
        "type": "array",
        "required": false,
        "description": "BlockAbilitiesWithTag container, written whole",
        "items": "string"
      },
      {
        "name": "activation_required_tags",
        "type": "array",
        "required": false,
        "description": "ActivationRequiredTags container, written whole",
        "items": "string"
      },
      {
        "name": "activation_blocked_tags",
        "type": "array",
        "required": false,
        "description": "ActivationBlockedTags container, written whole",
        "items": "string"
      }
    ]
  },
  "set_asc_defaults": {
    "category": "gas",
    "params": [
      {
        "name": "blueprintPath",
        "type": "string",
        "required": true,
        "description": "Blueprint asset path carrying the AbilitySystemComponent"
      },
      {
        "name": "attributeSet",
        "type": "string",
        "required": true,
        "description": "AttributeSet content path or class name",
        "aliases": [
          "attributeSetPath"
        ]
      },
      {
        "name": "componentName",
        "type": "string",
        "required": false,
        "description": "AbilitySystemComponent to wire (default: the first one)"
      },
      {
        "name": "initDataTable",
        "type": "string",
        "required": false,
        "description": "DataTable of starting attribute values"
      }
    ]
  },
  "set_attribute": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "attribute",
        "type": "string",
        "required": true,
        "description": "Attribute name: Health or SetName.Health"
      },
      {
        "name": "value",
        "type": "number",
        "required": true,
        "description": "New base value"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "set_effect_modifier": {
    "category": "gas",
    "params": [
      {
        "name": "effectPath",
        "type": "string",
        "required": true,
        "description": "GameplayEffect Blueprint asset path"
      },
      {
        "name": "attribute",
        "type": "string",
        "required": true,
        "description": "Attribute to modify: SetName.Attribute, or a unique attribute name"
      },
      {
        "name": "operation",
        "type": "string",
        "required": false,
        "description": "Additive (default) | Multiplicative | Division | Override"
      },
      {
        "name": "magnitude",
        "type": "number",
        "required": false,
        "description": "Static magnitude (default 0)"
      }
    ]
  },
  "set_live_attribute_value": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "attributeSet",
        "type": "string",
        "required": true,
        "description": "AttributeSet content path or class name"
      },
      {
        "name": "attribute",
        "type": "string",
        "required": true,
        "description": "Attribute property name, or Set.Property"
      },
      {
        "name": "value",
        "type": "number",
        "required": true,
        "description": "Value to write"
      },
      {
        "name": "valueType",
        "type": "string",
        "required": false,
        "description": "current (default, writes the attribute data in place) | base (writes through the ASC aggregator)"
      },
      {
        "name": "registerOwnerSets",
        "type": "boolean",
        "required": false,
        "description": "Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "trace_ability_activation": {
    "category": "gas",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Live actor label, internal name or object path. Pass this or actorPath"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"
      },
      {
        "name": "abilityClass",
        "type": "string",
        "required": true,
        "description": "GameplayAbility class to trace"
      },
      {
        "name": "activate",
        "type": "boolean",
        "required": false,
        "description": "Also call TryActivateAbility, to prove the verdict (default false)"
      },
      {
        "name": "world",
        "type": "string",
        "required": false,
        "description": "Runtime world scope: auto (default) | pie | editor"
      }
    ]
  },
  "validate_cue_coverage": {
    "category": "gas",
    "params": [
      {
        "name": "directory",
        "type": "string",
        "required": false,
        "description": "Content path to scan (default /Game)"
      },
      {
        "name": "effectPath",
        "type": "string",
        "required": false,
        "description": "Audit this one GameplayEffect instead of scanning",
        "aliases": [
          "effectClass"
        ]
      },
      {
        "name": "maxEffects",
        "type": "integer",
        "required": false,
        "description": "Cap on effect classes scanned (default 500, max 5000)"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_ability_system_component: "Params: blueprintPath, componentName?",
  add_attribute: "Params: attributeSetPath, attributeName",
  add_effect_cue: "Params: effectPath (or effectClass), cueTag, minLevel?, maxLevel?, magnitudeAttribute?",
  add_loose_gameplay_tag: "Params: actorLabel?, actorPath?, tag, count?, world?",
  apply_effect: "Params: actorLabel?, actorPath?, effectClass (or effectPath), level?, setByCaller?, world?",
  audit_attributes: "Params: attributeSet?, actorLabel?, actorPath?, probeClamping?, world?",
  bind_ability_input: "Params: actorLabel?, actorPath?, abilityClass, inputId, world?",
  capture_gas_state: "Params: actorLabel?, actorPath?, snapshotId?, compareWith?, registerOwnerSets?, world?",
  clear_ability_input: "Params: actorLabel?, actorPath?, abilityClass, world?",
  compare_gas_states: "Params: beforeId?, beforeSnapshot?, afterId?, afterSnapshot?",
  create_attribute_set: "Params: name, packagePath?, onConflict?",
  create_gameplay_ability: "Params: name, packagePath?, onConflict?",
  create_gameplay_cue: "Params: name, packagePath?, onConflict?, cueType?",
  create_gameplay_effect: "Params: name, packagePath?, onConflict?, durationPolicy?",
  delete_gas_snapshot: "Params: snapshotId",
  get_active_effects: "Params: actorLabel?, actorPath?, world?",
  get_asc_state: "Params: actorLabel?, actorPath?, world?",
  get_attribute: "Params: actorLabel?, actorPath?, attribute?, world?",
  get_gas_info: "Params: blueprintPath",
  get_live_attribute_value: "Params: actorLabel?, actorPath?, attributeSet, attribute, registerOwnerSets?, world?",
  grant_ability: "Params: actorLabel?, actorPath?, abilityClass, level?, inputId?, world?",
  init_asc: "Params: actorLabel?, actorPath?, attributeSet?, world?",
  list_gas_snapshots: "Params: actorPath?, includeSnapshots?",
  remove_effect: "Params: actorLabel?, actorPath?, effectHandle?, effectClass? (or effectPath), stacksToRemove?, world?",
  remove_effect_cue: "Params: effectPath (or effectClass), cueTag",
  remove_loose_gameplay_tag: "Params: actorLabel?, actorPath?, tag, count?, world?",
  revoke_ability: "Params: actorLabel?, actorPath?, abilityClass, world?",
  send_ability_input: "Params: actorLabel?, actorPath?, inputEvent?, inputId?, abilityClass?, world?",
  set_ability_tags: "Params: abilityPath, ability_tags?, cancel_abilities_with_tag?, block_abilities_with_tag?, activation_required_tags?, activation_blocked_tags?",
  set_asc_defaults: "Params: blueprintPath, attributeSet (or attributeSetPath), componentName?, initDataTable?",
  set_attribute: "Params: actorLabel?, actorPath?, attribute, value, world?",
  set_effect_modifier: "Params: effectPath, attribute, operation?, magnitude?",
  set_live_attribute_value: "Params: actorLabel?, actorPath?, attributeSet, attribute, value, valueType?, registerOwnerSets?, world?",
  trace_ability_activation: "Params: actorLabel?, actorPath?, abilityClass, activate?, world?",
  validate_cue_coverage: "Params: directory?, effectPath? (or effectClass), maxEffects?",
};

/** Every key the spec'd gas handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  ability_tags: z.array(z.string()).optional().describe("AbilityTags container, written whole"),
  abilityClass: z.string().optional().describe("Granted GameplayAbility class to bind (bind_ability_input). Granted GameplayAbility class to unbind (clear_ability_input). GameplayAbility Blueprint path, generated class path, or native class name (grant_ability). GameplayAbility class to revoke (revoke_ability). Granted ability whose bound input id to use instead of inputId (send_ability_input). GameplayAbility class to trace (trace_ability_activation)"),
  abilityPath: z.string().optional().describe("GameplayAbility Blueprint asset path"),
  activate: z.boolean().optional().describe("Also call TryActivateAbility, to prove the verdict (default false)"),
  activation_blocked_tags: z.array(z.string()).optional().describe("ActivationBlockedTags container, written whole"),
  activation_required_tags: z.array(z.string()).optional().describe("ActivationRequiredTags container, written whole"),
  actorLabel: z.string().optional().describe("Live actor label, internal name or object path. Pass this or actorPath"),
  actorPath: z.string().optional().describe("Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given (add_loose_gameplay_tag, apply_effect, audit_attributes, bind_ability_input, capture_gas_state, clear_ability_input, get_active_effects, get_asc_state, get_attribute, get_live_attribute_value, grant_ability, init_asc, remove_effect, remove_loose_gameplay_tag, revoke_ability, send_ability_input, set_attribute, set_live_attribute_value, trace_ability_activation). Only snapshots of this actor object path (list_gas_snapshots)"),
  afterId: z.string().optional().describe("Snapshot id of the later reading. Pass this or afterSnapshot"),
  afterSnapshot: z.record(z.unknown()).optional().describe("The later snapshot object itself"),
  attribute: z.string().optional().describe("Attribute to read. Omit to list every attribute (get_attribute). Attribute property name, or Set.Property (get_live_attribute_value, set_live_attribute_value). Attribute name: Health or SetName.Health (set_attribute). Attribute to modify: SetName.Attribute, or a unique attribute name (set_effect_modifier)"),
  attributeName: z.string().optional().describe("FGameplayAttributeData variable to add"),
  attributeSet: z.string().optional().describe("AttributeSet content path or class name. Pass this, a live actor, or both (audit_attributes). AttributeSet content path or class name (get_live_attribute_value, set_asc_defaults, set_live_attribute_value). AttributeSet content path or class name to make sure is registered (init_asc)"),
  attributeSetPath: z.string().optional().describe("AttributeSet Blueprint asset path (add_attribute). Alias for attributeSet (set_asc_defaults)"),
  beforeId: z.string().optional().describe("Snapshot id of the earlier reading. Pass this or beforeSnapshot"),
  beforeSnapshot: z.record(z.unknown()).optional().describe("The earlier snapshot object itself"),
  block_abilities_with_tag: z.array(z.string()).optional().describe("BlockAbilitiesWithTag container, written whole"),
  blueprintPath: z.string().optional().describe("Blueprint asset path (add_ability_system_component). Blueprint asset path to inspect (get_gas_info). Blueprint asset path carrying the AbilitySystemComponent (set_asc_defaults)"),
  cancel_abilities_with_tag: z.array(z.string()).optional().describe("CancelAbilitiesWithTag container, written whole"),
  compareWith: z.string().optional().describe("Earlier snapshot id to diff this capture against"),
  componentName: z.string().optional().describe("Name of the AbilitySystemComponent (default AbilitySystemComp) (add_ability_system_component). AbilitySystemComponent to wire (default: the first one) (set_asc_defaults)"),
  count: z.number().int().optional().describe("References to add, at least 1 (default 1) (add_loose_gameplay_tag). References to remove, at least 1 (default 1) (remove_loose_gameplay_tag)"),
  cueTag: z.string().optional().describe("Registered GameplayCue tag, under the GameplayCue root (add_effect_cue). GameplayCue tag to unlink. May be one that is no longer registered (remove_effect_cue)"),
  cueType: z.string().optional().describe("Static (default, GameplayCueNotify_Static) | Actor (GameplayCueNotify_Actor)"),
  directory: z.string().optional().describe("Content path to scan (default /Game)"),
  durationPolicy: z.string().optional().describe("Echoed back as durationPolicy (default Instant); it is not written onto the effect"),
  effectClass: z.string().optional().describe("Alias for effectPath (add_effect_cue, remove_effect_cue, validate_cue_coverage). GameplayEffect content path or class name (apply_effect). GameplayEffect content path or class name. Removes every active effect of that class (remove_effect)"),
  effectHandle: z.string().optional().describe("Active-effect handle apply_effect reported. Removes exactly that effect"),
  effectPath: z.string().optional().describe("GameplayEffect Blueprint asset path (add_effect_cue, remove_effect_cue, set_effect_modifier). Alias for effectClass (apply_effect, remove_effect). Audit this one GameplayEffect instead of scanning (validate_cue_coverage)"),
  includeSnapshots: z.boolean().optional().describe("Return the full snapshot bodies rather than a summary row each"),
  initDataTable: z.string().optional().describe("DataTable of starting attribute values"),
  inputEvent: z.string().optional().describe("pressed (default) | released | confirm | cancel. pressed and released address an inputId; confirm and cancel take none"),
  inputId: z.number().int().optional().describe("Input id to bind; -1 leaves the ability unbound (bind_ability_input). InputID for the spec (default -1, unbound) (grant_ability). Input id to send pressed or released to (send_ability_input)"),
  level: z.number().optional().describe("Effect level (default 1) (apply_effect). Ability level (default 1) (grant_ability)"),
  magnitude: z.number().optional().describe("Static magnitude (default 0)"),
  magnitudeAttribute: z.string().optional().describe("Attribute the cue takes its magnitude from (SetName.Attribute), instead of the effect level"),
  maxEffects: z.number().int().optional().describe("Cap on effect classes scanned (default 500, max 5000)"),
  maxLevel: z.number().optional().describe("Highest effect level this cue covers"),
  minLevel: z.number().optional().describe("Lowest effect level this cue covers, used to normalise the magnitude"),
  name: z.string().optional().describe("Blueprint name"),
  onConflict: z.string().optional().describe("skip (default, returns the existing asset) or error when the asset already exists"),
  operation: z.string().optional().describe("Additive (default) | Multiplicative | Division | Override"),
  packagePath: z.string().optional().describe("Content folder (default /Game/GAS/Attributes) (create_attribute_set). Content folder (default /Game/GAS/Abilities) (create_gameplay_ability). Content folder (default /Game/GAS/Cues) (create_gameplay_cue). Content folder (default /Game/GAS/Effects) (create_gameplay_effect)"),
  probeClamping: z.boolean().optional().describe("Measure an existing clamp by driving the set's own PreAttributeChange. Needs a live registered set (default false)"),
  registerOwnerSets: z.boolean().optional().describe("Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)"),
  setByCaller: z.record(z.unknown()).optional().describe("SetByCaller magnitudes keyed by gameplay tag or name"),
  snapshotId: z.string().optional().describe("Id to store the snapshot under (generated when omitted) (capture_gas_state). Snapshot id to drop (delete_gas_snapshot)"),
  stacksToRemove: z.number().int().optional().describe("Stacks to take off (default -1, the whole effect)"),
  tag: z.string().optional().describe("A registered gameplay tag"),
  value: z.number().optional().describe("New base value (set_attribute). Value to write (set_live_attribute_value)"),
  valueType: z.string().optional().describe("current (default, writes the attribute data in place) | base (writes through the ASC aggregator)"),
  world: z.string().optional().describe("Runtime world scope: auto (default) | pie | editor"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
