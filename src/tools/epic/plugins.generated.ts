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

const S_epic_add_plugin_dependency = {"properties":{"pluginName":{"type":"string"},"dependencyName":{"type":"string"},"bOptional":{"type":"boolean"},"bEnabled":{"type":"boolean"}},"required":["pluginName","dependencyName","bOptional","bEnabled"]} as const;
const S_epic_create_plugin = {"properties":{"pluginName":{"type":"string"},"relativePluginLocation":{"type":"string"},"bPlaceInEngine":{"type":"boolean"},"templateInfo":{"type":"object"},"description":{"type":"string"}},"required":["pluginName","relativePluginLocation","bPlaceInEngine","templateInfo","description"]} as const;
const S_epic_get_game_feature_state = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_get_plugin_dependencies = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_get_plugin_dependents = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_get_plugin_descriptor = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_get_plugin_for_asset = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_get_plugin_info = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_get_plugin_template_descriptions = {"properties":{}} as const;
const S_epic_is_enabled = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_is_game_feature_active = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_is_game_feature_plugin = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_is_plugin_creation_allowed = {"properties":{}} as const;
const S_epic_is_plugin_modification_allowed = {"properties":{}} as const;
const S_epic_list_discovered_game_feature_plugins = {"properties":{}} as const;
const S_epic_list_discovered_plugins = {"properties":{}} as const;
const S_epic_list_enabled_game_feature_plugins = {"properties":{}} as const;
const S_epic_list_enabled_plugins = {"properties":{}} as const;
const S_epic_remove_plugin_dependency = {"properties":{"pluginName":{"type":"string"},"dependencyName":{"type":"string"}},"required":["pluginName","dependencyName"]} as const;
const S_epic_request_activate_game_feature = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_request_deactivate_game_feature = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_set_plugin_enabled = {"properties":{"pluginName":{"type":"string"},"bEnabled":{"type":"boolean"}},"required":["pluginName","bEnabled"]} as const;
const S_epic_update_plugin_descriptor = {"properties":{"pluginName":{"type":"string"},"newDescriptor":{"type":"object"}},"required":["pluginName","newDescriptor"]} as const;
const S_epic_validate_new_plugin_name_and_location = {"properties":{"pluginName":{"type":"string"},"relativePluginLocation":{"type":"string"},"bPlaceInEngine":{"type":"boolean"},"templateInfo":{"type":"object"}},"required":["pluginName","relativePluginLocation","bPlaceInEngine","templateInfo"]} as const;

/** 24 wrapped engine tools routed to the `plugins` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_plugin_dependency: bp(
    "mutate",
    "[Epic PluginToolset.PluginToolset] Adds a dependency entry to a plugin's Plugins array in its .uplugin file. No-ops if a dependency with that name already exists with matching settings. The dependency plugin does not need to be currently discovered. Params: pluginName, dependencyName, bOptional, bEnabled",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.AddPluginDependency", S_epic_add_plugin_dependency, p),
  ),
  epic_create_plugin: bp(
    "mutate",
    "[Epic PluginToolset.PluginToolset] Creates a new plugin from a template and loads it into the editor. Use GetPluginTemplateDescriptions to obtain a valid TemplateInfo. Params: pluginName, relativePluginLocation, bPlaceInEngine, templateInfo, description",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.CreatePlugin", S_epic_create_plugin, p),
  ),
  epic_get_game_feature_state: bp(
    "read",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Gets the current state of a Game Feature Plugin. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.GetGameFeatureState", S_epic_get_game_feature_state, p),
  ),
  epic_get_plugin_dependencies: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Returns the dependency entries from a plugin's Plugins array in its .uplugin file. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.GetPluginDependencies", S_epic_get_plugin_dependencies, p),
  ),
  epic_get_plugin_dependents: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Returns the names of all discovered plugins that declare a dependency on the given plugin. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.GetPluginDependents", S_epic_get_plugin_dependents, p),
  ),
  epic_get_plugin_descriptor: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Gets the editable descriptor fields for a discovered plugin. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.GetPluginDescriptor", S_epic_get_plugin_descriptor, p),
  ),
  epic_get_plugin_for_asset: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Returns the name of the enabled plugin whose content mount point contains the given asset path. Accepts full asset paths or mount point prefixes (e.g. /PluginName/ or /Game/Path/To/Asset). Params: assetPath",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.GetPluginForAsset", S_epic_get_plugin_for_asset, p),
  ),
  epic_get_plugin_info: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Gets metadata for a discovered plugin, including description, version, base directory, content directory, descriptor path, and mounted asset path. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.GetPluginInfo", S_epic_get_plugin_info, p),
  ),
  epic_get_plugin_template_descriptions: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Returns the list of available plugin templates. Pass one of the results to CreatePlugin to create a new plugin from that template. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.GetPluginTemplateDescriptions", S_epic_get_plugin_template_descriptions, p),
  ),
  epic_is_enabled: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Checks whether a discovered plugin is currently enabled. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.IsEnabled", S_epic_is_enabled, p),
  ),
  epic_is_game_feature_active: bp(
    "read",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Checks whether a Game Feature Plugin is active. Raises an error if the subsystem is unavailable or the plugin is not found. Use GetGameFeatureState if you need the current state when the plugin is not active. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.IsGameFeatureActive", S_epic_is_game_feature_active, p),
  ),
  epic_is_game_feature_plugin: bp(
    "read",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Return whether or not a plugin is a Game Feature Plugin. Will error if no plugin of this name can be found by the Plugin Manager. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.IsGameFeaturePlugin", S_epic_is_game_feature_plugin, p),
  ),
  epic_is_plugin_creation_allowed: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Checks whether the editor settings permit plugin creation from the plugin browser. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.IsPluginCreationAllowed", S_epic_is_plugin_creation_allowed, p),
  ),
  epic_is_plugin_modification_allowed: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Checks whether the editor settings permit modifying plugins from the plugin browser. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.IsPluginModificationAllowed", S_epic_is_plugin_modification_allowed, p),
  ),
  epic_list_discovered_game_feature_plugins: bp(
    "read",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Lists all discovered Game Feature Plugins sorted by name. This includes enabled and disabled plugins. Only enabled plugins are known by the Game Features system beyond identifying if a plugin is a Game Feature Plugin. Use the Plugins toolset to do general plugin enable/disable tasks. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.ListDiscoveredGameFeaturePlugins", S_epic_list_discovered_game_feature_plugins, p),
  ),
  epic_list_discovered_plugins: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Lists the names of all discovered plugins (enabled and disabled), sorted alphabetically. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.ListDiscoveredPlugins", S_epic_list_discovered_plugins, p),
  ),
  epic_list_enabled_game_feature_plugins: bp(
    "read",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Lists all enabled Game Feature Plugins sorted by name. Enabled plugins are the only plugins known by the Game Features system beyond identifying if a plugin is a Game Feature Plugin. Use the Plugins toolset to do general plugin enable/disable tasks. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.ListEnabledGameFeaturePlugins", S_epic_list_enabled_game_feature_plugins, p),
  ),
  epic_list_enabled_plugins: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Lists the names of all enabled plugins, sorted alphabetically. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.ListEnabledPlugins", S_epic_list_enabled_plugins, p),
  ),
  epic_remove_plugin_dependency: bp(
    "mutate",
    "[Epic PluginToolset.PluginToolset] Removes a dependency entry from a plugin's Plugins array in its .uplugin file. Params: pluginName, dependencyName",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.RemovePluginDependency", S_epic_remove_plugin_dependency, p),
  ),
  epic_request_activate_game_feature: bp(
    "mutate",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Requests activation of a Game Feature Plugin. Returns true if the activation request was submitted successfully. The actual activation happens asynchronously -- poll GetGameFeatureState() or IsGameFeatureActive() to confirm completion. Raises an error if the subsystem is unavailable or the plugin is not found. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.RequestActivateGameFeature", S_epic_request_activate_game_feature, p),
  ),
  epic_request_deactivate_game_feature: bp(
    "mutate",
    "[Epic GameFeaturesToolset.GameFeaturesToolset] Requests deactivation of a Game Feature Plugin. Returns true if the deactivation request was submitted successfully. The actual deactivation happens asynchronously -- poll GetGameFeatureState() to confirm completion. Raises an error if the subsystem is unavailable or the plugin is not found. Params: pluginName",
    "epic_call_tool",
    (p) => epicToolCall("GameFeaturesToolset.GameFeaturesToolset", "GameFeaturesToolset.GameFeaturesToolset.RequestDeactivateGameFeature", S_epic_request_deactivate_game_feature, p),
  ),
  epic_set_plugin_enabled: bp(
    "mutate",
    "[Epic PluginToolset.PluginToolset] Enables or disables a plugin in the project config. The change takes effect on the next editor restart. Params: pluginName, bEnabled",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.SetPluginEnabled", S_epic_set_plugin_enabled, p),
  ),
  epic_update_plugin_descriptor: bp(
    "mutate",
    "[Epic PluginToolset.PluginToolset] Updates a plugin's descriptor fields and writes them to its .uplugin file. Checks out the file via source control if source control is enabled. No-ops if the serialized descriptor is unchanged (file is not touched). Params: pluginName, newDescriptor",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.UpdatePluginDescriptor", S_epic_update_plugin_descriptor, p),
  ),
  epic_validate_new_plugin_name_and_location: bp(
    "read",
    "[Epic PluginToolset.PluginToolset] Validates that PluginName and RelativePluginLocation are acceptable for a new plugin. Params: pluginName, relativePluginLocation, bPlaceInEngine, templateInfo",
    "epic_call_tool",
    (p) => epicToolCall("PluginToolset.PluginToolset", "PluginToolset.PluginToolset.ValidateNewPluginNameAndLocation", S_epic_validate_new_plugin_name_and_location, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  assetPath: z.string().optional().describe("The asset or mount point path to look up."),
  bEnabled: z.boolean().optional().describe("Whether the dependency should be enabled."),
  bOptional: z.boolean().optional().describe("Whether the dependency is optional."),
  bPlaceInEngine: z.boolean().optional().describe("Use Engine Plugins directory rather than Game Plugins directory location. Only some Templates allow placing in Engine. See the TemplateInfo's bCanBePlacedInEng…"),
  dependencyName: z.string().optional().describe("The name of the plugin to add as a dependency."),
  description: z.string().optional().describe("A description for the new plugin."),
  newDescriptor: z.record(z.unknown()).optional().describe("The new descriptor field values to apply."),
  pluginName: z.string().optional().describe("Name of the Game Feature Plugin."),
  relativePluginLocation: z.string().optional().describe("Parent directory for the new plugin relative to template's default location. This should be empty unless you wish to specify a subdirectory."),
  templateInfo: z.record(z.unknown()).optional().describe("The plugin template to create from."),
};
