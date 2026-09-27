// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
        "description": "Instant, HasDuration or Infinite, written onto the effect's defaults (default Instant)"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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
      },
      {
        "name": "pieInstance",
        "type": "number",
        "required": false,
        "description": "PIE world instance (0 = server/primary); omit for the primary world"
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

/** Every key the spec'd gas handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
