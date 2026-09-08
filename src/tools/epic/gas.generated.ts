// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog.json
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
import { z } from "zod";
import { bp, type ActionSpec } from "../../types.js";
import { epicToolCall } from "../../epic-input.js";

const S_epic_add_cue_tag = {"properties":{"cueTag":{"type":"string"},"comment":{"type":"string"}},"required":["cueTag"]} as const;
const S_epic_create_cue_notify_asset = {"properties":{"cueTag":{"type":"string"},"packagePath":{"type":"string"},"assetName":{"type":"string"},"bIsActor":{"type":"boolean"}},"required":["cueTag","packagePath","assetName","bIsActor"]} as const;
const S_epic_execute_cue_on_selected_actor = {"properties":{"cueTag":{"type":"string"},"normalizedMagnitude":{"type":"number"},"location":{"type":"object"},"normal":{"type":"object"}},"required":["cueTag","normalizedMagnitude","location","normal"]} as const;
const S_epic_find_attribute_set_classes = {"properties":{}} as const;
const S_epic_find_cue_notify_assets = {"properties":{"parentTag":{"type":"string"}},"required":["parentTag"]} as const;
const S_epic_find_cue_tags_without_notifies = {"properties":{}} as const;
const S_epic_get_active_effects = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_active_tags = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_attribute_values = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_get_cue_info = {"properties":{"cueTag":{"type":"string"}},"required":["cueTag"]} as const;
const S_epic_get_granted_abilities = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_list_attributes = {"properties":{"className":{"type":"string"}},"required":["className"]} as const;
const S_epic_list_cues = {"properties":{"parentTag":{"type":"string"}},"required":["parentTag"]} as const;
const S_epic_remove_cue_tag = {"properties":{"cueTag":{"type":"string"}},"required":["cueTag"]} as const;

/** 14 wrapped engine tools routed to the `gas` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_cue_tag: bp(
    "mutate",
    "[Epic GASToolsets.GameplayCueToolset] Adds a new gameplay cue tag to the project. This should ONLY be called after getting explicit direction or permission from the user. Params: cueTag, comment?",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.AddCueTag", S_epic_add_cue_tag, p),
  ),
  epic_create_cue_notify_asset: bp(
    "mutate",
    "[Epic GASToolsets.GameplayCueToolset] Creates a new GameplayCueNotify Blueprint asset at the specified content browser location. This should ONLY be called after getting explicit direction or permission from the user. Params: cueTag, packagePath, assetName, bIsActor",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.CreateCueNotifyAsset", S_epic_create_cue_notify_asset, p),
  ),
  epic_execute_cue_on_selected_actor: bp(
    "mutate",
    "[Epic GASToolsets.GameplayCueToolset] Executes a gameplay cue non-replicated on the currently selected actor in the editor. Useful for previewing cue effects without network replication. Requires a PIE session or a configured GameplayCueManager to produce visible results. Params: cueTag, normalizedMagnitude, location, normal",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.ExecuteCueOnSelectedActor", S_epic_execute_cue_on_selected_actor, p),
  ),
  epic_find_attribute_set_classes: bp(
    "read",
    "[Epic GASToolsets.AttributeSetToolset] Returns all AttributeSet subclasses found in the project, including their attributes. Covers both native C++ subclasses and Blueprint subclasses discovered via the asset registry. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.AttributeSetToolset", "GASToolsets.AttributeSetToolset.FindAttributeSetClasses", S_epic_find_attribute_set_classes, p),
  ),
  epic_find_cue_notify_assets: bp(
    "read",
    "[Epic GASToolsets.GameplayCueToolset] Returns all GameplayCueNotify assets found in the project via the asset registry. Params: parentTag",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.FindCueNotifyAssets", S_epic_find_cue_notify_assets, p),
  ),
  epic_find_cue_tags_without_notifies: bp(
    "read",
    "[Epic GASToolsets.GameplayCueToolset] Returns gameplay cue tags that have no corresponding GameplayCueNotify asset in the project. Tags without notifies produce no visible effect when triggered at runtime. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.FindCueTagsWithoutNotifies", S_epic_find_cue_tags_without_notifies, p),
  ),
  epic_get_active_effects: bp(
    "read",
    "[Epic GASToolsets.AbilitySystemInspectorToolset] Returns all gameplay effects currently active on the actor's AbilitySystemComponent. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.AbilitySystemInspectorToolset", "GASToolsets.AbilitySystemInspectorToolset.GetActiveEffects", S_epic_get_active_effects, p),
  ),
  epic_get_active_tags: bp(
    "read",
    "[Epic GASToolsets.AbilitySystemInspectorToolset] Returns the gameplay tags currently owned by the actor's AbilitySystemComponent (includes loose tags, effect-granted tags, etc.). Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.AbilitySystemInspectorToolset", "GASToolsets.AbilitySystemInspectorToolset.GetActiveTags", S_epic_get_active_tags, p),
  ),
  epic_get_attribute_values: bp(
    "read",
    "[Epic GASToolsets.AbilitySystemInspectorToolset] Returns the current base and modified values of all gameplay attributes on the actor's AbilitySystemComponent. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.AbilitySystemInspectorToolset", "GASToolsets.AbilitySystemInspectorToolset.GetAttributeValues", S_epic_get_attribute_values, p),
  ),
  epic_get_cue_info: bp(
    "read",
    "[Epic GASToolsets.GameplayCueToolset] Returns information about a specific gameplay cue, including its notify asset. Params: cueTag",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.GetCueInfo", S_epic_get_cue_info, p),
  ),
  epic_get_granted_abilities: bp(
    "read",
    "[Epic GASToolsets.AbilitySystemInspectorToolset] Returns all abilities granted to the actor's AbilitySystemComponent. Params: actor",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.AbilitySystemInspectorToolset", "GASToolsets.AbilitySystemInspectorToolset.GetGrantedAbilities", S_epic_get_granted_abilities, p),
  ),
  epic_list_attributes: bp(
    "read",
    "[Epic GASToolsets.AttributeSetToolset] Returns the gameplay attributes defined on a specific AttributeSet class. Params: className",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.AttributeSetToolset", "GASToolsets.AttributeSetToolset.ListAttributes", S_epic_list_attributes, p),
  ),
  epic_list_cues: bp(
    "read",
    "[Epic GASToolsets.GameplayCueToolset] Returns gameplay cue tags registered in the project. Params: parentTag",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.ListCues", S_epic_list_cues, p),
  ),
  epic_remove_cue_tag: bp(
    "mutate",
    "[Epic GASToolsets.GameplayCueToolset] Removes a gameplay cue tag from the project. This should ONLY be called after getting explicit direction or permission from the user. Params: cueTag",
    "epic_call_tool",
    (p) => epicToolCall("GASToolsets.GameplayCueToolset", "GASToolsets.GameplayCueToolset.RemoveCueTag", S_epic_remove_cue_tag, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  actor: z.union([z.string(), z.record(z.unknown())]).optional().describe("The target actor."),
  assetName: z.string().optional().describe("The file name for the new asset, e.g. \"GCN_CharacterDeath\"."),
  bIsActor: z.boolean().optional().describe("If true, creates a GameplayCueNotify_Actor (spawned actor in the world). If false, creates a GameplayCueNotify_Static (instant effect, no spawned actor)."),
  className: z.string().optional().describe("The UClass name to look up, e.g. \"UMyHealthSet\". Raises a script error if the class is not found or is not an AttributeSet subclass."),
  comment: z.string().optional().describe("An optional developer comment describing the cue's purpose."),
  cueTag: z.string().optional().describe("The fully-qualified tag to add. Must begin with \"GameplayCue.\" (e.g. \"GameplayCue.Character.Death\")."),
  location: z.record(z.unknown()).optional().describe("World-space location parameter passed to the cue."),
  normal: z.record(z.unknown()).optional().describe("World-space direction parameter passed to the cue."),
  normalizedMagnitude: z.number().optional().describe("A normalized (0.0-1.0) magnitude value passed to the cue."),
  packagePath: z.string().optional().describe("The content browser folder for the asset, e.g. \"/Game/Effects/Cues\"."),
  parentTag: z.string().optional().describe("If non-empty, only notifies whose cue tag descends from this tag are returned. Pass an empty string to return all notify assets in the project."),
};
