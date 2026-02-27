import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const gasTool: ToolDef = categoryTool(
  "gas",
  "Gameplay Ability System: abilities, effects, attribute sets, cues.",
  {
    add_asc:             bp("add_ability_system_component"),
    create_attribute_set: bp("create_attribute_set"),
    add_attribute:       bp("add_attribute"),
    create_ability:      bp("create_gameplay_ability"),
    set_ability_tags:    bp("set_ability_tags"),
    create_effect:       bp("create_gameplay_effect"),
    set_effect_modifier: bp("set_effect_modifier"),
    create_cue:          bp("create_gameplay_cue"),
    get_info:            bp("get_gas_info"),
  },
  `- add_asc: Add AbilitySystemComponent. Params: blueprintPath, componentName?
- create_attribute_set: Create AttributeSet BP. Params: name, packagePath?
- add_attribute: Add attribute to set. Params: attributeSetPath, attributeName, defaultValue?
- create_ability: Create GameplayAbility BP. Params: name, packagePath?, parentClass?
- set_ability_tags: Set tags on ability. Params: abilityPath, ability_tags?, cancel_abilities_with_tag?, activation_required_tags?, activation_blocked_tags?
- create_effect: Create GameplayEffect BP. Params: name, packagePath?, durationPolicy? (Instant|HasDuration|Infinite)
- set_effect_modifier: Add modifier. Params: effectPath, attribute, operation?, magnitude?
- create_cue: Create GameplayCue. Params: name, packagePath?, cueType? (Static|Actor)
- get_info: Inspect GAS setup. Params: blueprintPath`,
  {
    blueprintPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    componentName: z.string().optional(),
    attributeSetPath: z.string().optional(),
    attributeName: z.string().optional(),
    defaultValue: z.number().optional(),
    parentClass: z.string().optional(),
    abilityPath: z.string().optional(),
    ability_tags: z.array(z.string()).optional(),
    cancel_abilities_with_tag: z.array(z.string()).optional(),
    block_abilities_with_tag: z.array(z.string()).optional(),
    activation_required_tags: z.array(z.string()).optional(),
    activation_blocked_tags: z.array(z.string()).optional(),
    effectPath: z.string().optional(),
    attribute: z.string().optional(),
    operation: z.string().optional(),
    magnitude: z.number().optional(),
    durationPolicy: z.string().optional(),
    cueType: z.string().optional(),
  },
);
