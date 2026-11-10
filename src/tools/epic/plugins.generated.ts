// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_add_additional_plugin_directory = {"properties":{"directory":{"type":"string"}},"required":["directory"]} as const;
const S_epic_add_disallowed_plugin = {"properties":{"pluginName":{"type":"string"},"disallowedPluginName":{"type":"string"},"comment":{"type":"string"}},"required":["pluginName","disallowedPluginName","comment"]} as const;
const S_epic_add_plugin_dependency = {"properties":{"pluginName":{"type":"string"},"dependencyName":{"type":"string"},"bOptional":{"type":"boolean"},"bEnabled":{"type":"boolean"},"bActivate":{"type":"boolean"}},"required":["pluginName","dependencyName","bOptional","bEnabled"]} as const;
const S_epic_create_plugin = {"properties":{"pluginName":{"type":"string"},"relativePluginLocation":{"type":"string"},"bPlaceInEngine":{"type":"boolean"},"templateInfo":{"type":"object","properties":{"name":{"type":"string"},"description":{"type":"string"},"onDiskPath":{"type":"string"},"defaultTemplateName":{"type":"string"},"bCanBePlacedInEngine":{"type":"boolean"}}},"description":{"type":"string"},"pluginVersePath":{"type":"string"},"pythonScriptAdditionalArgs":{"type":"string"}},"required":["pluginName","relativePluginLocation","bPlaceInEngine","templateInfo","description"]} as const;
const S_epic_get_additional_plugin_directories = {"properties":{}} as const;
const S_epic_get_capabilities = {"properties":{"capabilityIds":{"type":"array","items":{"type":"string"}}},"required":["capabilityIds"]} as const;
const S_epic_get_configured_additional_metadata_keys = {"properties":{}} as const;
const S_epic_get_disallowed_plugins = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_get_game_feature_plugin_details = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
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
const S_epic_list_capabilities = {"properties":{}} as const;
const S_epic_list_discovered_game_feature_plugins = {"properties":{}} as const;
const S_epic_list_discovered_plugins = {"properties":{}} as const;
const S_epic_list_enabled_game_feature_plugins = {"properties":{}} as const;
const S_epic_list_enabled_plugins = {"properties":{}} as const;
const S_epic_remove_additional_plugin_directory = {"properties":{"directory":{"type":"string"}},"required":["directory"]} as const;
const S_epic_remove_disallowed_plugin = {"properties":{"pluginName":{"type":"string"},"disallowedPluginName":{"type":"string"}},"required":["pluginName","disallowedPluginName"]} as const;
const S_epic_remove_game_feature_plugin_metadata_value = {"properties":{"pluginName":{"type":"string"},"key":{"type":"string"}},"required":["pluginName","key"]} as const;
const S_epic_remove_plugin_dependency = {"properties":{"pluginName":{"type":"string"},"dependencyName":{"type":"string"}},"required":["pluginName","dependencyName"]} as const;
const S_epic_request_activate_game_feature = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_request_deactivate_game_feature = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_request_load_game_feature = {"properties":{"pluginName":{"type":"string"}},"required":["pluginName"]} as const;
const S_epic_request_unload_game_feature = {"properties":{"pluginName":{"type":"string"},"bKeepRegistered":{"type":"boolean"}},"required":["pluginName"]} as const;
const S_epic_set_game_feature_plugin_built_in_auto_state = {"properties":{"pluginName":{"type":"string"},"newState":{"type":"string","enum":["Invalid","Installed","Registered","Loaded","Active"]}},"required":["pluginName","newState"]} as const;
const S_epic_set_game_feature_plugin_metadata_value = {"properties":{"pluginName":{"type":"string"},"key":{"type":"string"},"jsonValueLiteral":{"type":"string"}},"required":["pluginName","key","jsonValueLiteral"]} as const;
const S_epic_set_plugin_enabled = {"properties":{"pluginName":{"type":"string"},"bEnabled":{"type":"boolean"}},"required":["pluginName","bEnabled"]} as const;
const S_epic_update_plugin_dependency = {"properties":{"pluginName":{"type":"string"},"dependencyName":{"type":"string"},"bOptional":{"type":"boolean"},"bEnabled":{"type":"boolean"},"bActivate":{"type":"boolean"}},"required":["pluginName","dependencyName"]} as const;
const S_epic_update_plugin_descriptor = {"properties":{"pluginName":{"type":"string"},"newDescriptor":{"type":"object","properties":{"friendlyName":{"type":"string"},"description":{"type":"string"},"category":{"type":"string"},"versionName":{"type":"string"},"version":{"type":"integer"},"createdBy":{"type":"string"},"createdByURL":{"type":"string"},"docsURL":{"type":"string"},"marketplaceURL":{"type":"string"},"supportURL":{"type":"string"},"bCanContainContent":{"type":"boolean"},"bIsBetaVersion":{"type":"boolean"},"bIsExperimentalVersion":{"type":"boolean"},"bIsSealed":{"type":"boolean"}}}},"required":["pluginName","newDescriptor"]} as const;
const S_epic_validate_new_plugin_name_and_location = {"properties":{"pluginName":{"type":"string"},"relativePluginLocation":{"type":"string"},"bPlaceInEngine":{"type":"boolean"},"templateInfo":{"type":"object","properties":{"name":{"type":"string"},"description":{"type":"string"},"onDiskPath":{"type":"string"},"defaultTemplateName":{"type":"string"},"bCanBePlacedInEngine":{"type":"boolean"}}}},"required":["pluginName","relativePluginLocation","bPlaceInEngine","templateInfo"]} as const;

/** 40 wrapped engine tools routed to the `plugins` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_additional_plugin_directory: {
    epicSchema: S_epic_add_additional_plugin_directory,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.AddAdditionalPluginDirectory", engines: ["5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Adds a directory the project searches for plugins, saves the .uproject file, and rescans so plugins in the directory are discovered without a restart. No-ops if the directory is already listed. Available on UE 5.9; absent from UE 5.8. Params: directory", "epic_call_tool"),
  },
  epic_add_disallowed_plugin: {
    epicSchema: S_epic_add_disallowed_plugin,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.AddDisallowedPlugin", engines: ["5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Adds an entry to a plugin's DisallowedPlugins array in its .uplugin file, or updates its comment if the entry already exists. The disallowed plugin does not need to be currently discovered. Available on UE 5.9; absent from UE 5.8. Params: pluginName, disallowedPluginName, comment", "epic_call_tool"),
  },
  epic_add_plugin_dependency: {
    epicSchema: S_epic_add_plugin_dependency,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.AddPluginDependency", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Adds a dependency entry to a plugin's Plugins array in its .uplugin file. No-ops if a dependency with that name already exists with matching settings. The dependency plugin does not need to be currently discovered. Params: pluginName, dependencyName, bOptional, bEnabled, bActivate?", "epic_call_tool"),
  },
  epic_create_plugin: {
    epicSchema: S_epic_create_plugin,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.CreatePlugin", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Creates a new plugin from a template and loads it into the editor. Use GetPluginTemplateDescriptions to obtain a valid TemplateInfo. Params: pluginName, relativePluginLocation, bPlaceInEngine, templateInfo, description, pluginVersePath?, pythonScriptAdditionalArgs?", "epic_call_tool"),
  },
  epic_get_additional_plugin_directories: {
    epicSchema: S_epic_get_additional_plugin_directories,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetAdditionalPluginDirectories", engines: ["5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Lists the extra directories the project searches for plugins (the .uproject's AdditionalPluginDirectories). Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_capabilities: {
    epicSchema: S_epic_get_capabilities,
    epicTool: { toolset: "AgentPluginToolset.AgentPluginToolset", name: "AgentPluginToolset.AgentPluginToolset.GetCapabilities", engines: ["5.9"] },
    ...bp("read", "[Epic AgentPluginToolset.AgentPluginToolset] Returns details for specific capabilities. Discovery fails after 60 seconds. Available on UE 5.9; absent from UE 5.8. Params: capabilityIds", "epic_call_tool"),
  },
  epic_get_configured_additional_metadata_keys: {
    epicSchema: S_epic_get_configured_additional_metadata_keys,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.GetConfiguredAdditionalMetadataKeys", engines: ["5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Lists the additional metadata keys the GameFeatures subsystem is configured to read from each .uplugin (UGameFeaturesSubsystemSettings::AdditionalPluginMetadataKeys), sorted. These are the only keys SetGameFeaturePluginMetadataValue and RemoveGameFeaturePluginMetadataValue will accept. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_disallowed_plugins: {
    epicSchema: S_epic_get_disallowed_plugins,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetDisallowedPlugins", engines: ["5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Returns the entries from a plugin's DisallowedPlugins array in its .uplugin file. Available on UE 5.9; absent from UE 5.8. Params: pluginName", "epic_call_tool"),
  },
  epic_get_game_feature_plugin_details: {
    epicSchema: S_epic_get_game_feature_plugin_details,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.GetGameFeaturePluginDetails", engines: ["5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Reads the editable metadata on a Game Feature Plugin's .uplugin descriptor. Returns the BuiltInInitialFeatureState and the configured additional metadata keys (UGameFeaturesSubsystemSettings::AdditionalPluginMetadataKeys) with their values rendered as JSON literals. Available on UE 5.9; absent from UE 5.8. Params: pluginName", "epic_call_tool"),
  },
  epic_get_game_feature_state: {
    epicSchema: S_epic_get_game_feature_state,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.GetGameFeatureState", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Gets the current state of a Game Feature Plugin. Params: pluginName", "epic_call_tool"),
  },
  epic_get_plugin_dependencies: {
    epicSchema: S_epic_get_plugin_dependencies,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetPluginDependencies", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Returns the dependency entries from a plugin's Plugins array in its .uplugin file. Params: pluginName", "epic_call_tool"),
  },
  epic_get_plugin_dependents: {
    epicSchema: S_epic_get_plugin_dependents,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetPluginDependents", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Returns the names of all discovered plugins that declare a dependency on the given plugin. Params: pluginName", "epic_call_tool"),
  },
  epic_get_plugin_descriptor: {
    epicSchema: S_epic_get_plugin_descriptor,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetPluginDescriptor", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Gets the editable descriptor fields for a discovered plugin. Params: pluginName", "epic_call_tool"),
  },
  epic_get_plugin_for_asset: {
    epicSchema: S_epic_get_plugin_for_asset,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetPluginForAsset", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Returns the name of the enabled plugin whose content mount point contains the given asset path. Accepts full asset paths or mount point prefixes (e.g. /PluginName/ or /Game/Path/To/Asset). Params: assetPath", "epic_call_tool"),
  },
  epic_get_plugin_info: {
    epicSchema: S_epic_get_plugin_info,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetPluginInfo", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Gets metadata for a discovered plugin, including description, version, base directory, content directory, descriptor path, and mounted asset path. Params: pluginName", "epic_call_tool"),
  },
  epic_get_plugin_template_descriptions: {
    epicSchema: S_epic_get_plugin_template_descriptions,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.GetPluginTemplateDescriptions", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Returns the list of available plugin templates. Pass one of the results to CreatePlugin to create a new plugin from that template. Params: none", "epic_call_tool"),
  },
  epic_is_enabled: {
    epicSchema: S_epic_is_enabled,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.IsEnabled", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Checks whether a discovered plugin is currently enabled. Params: pluginName", "epic_call_tool"),
  },
  epic_is_game_feature_active: {
    epicSchema: S_epic_is_game_feature_active,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.IsGameFeatureActive", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Checks whether a Game Feature Plugin is active. Raises an error if the subsystem is unavailable or the plugin is not found. Use GetGameFeatureState if you need the current state when the plugin is not active. Params: pluginName", "epic_call_tool"),
  },
  epic_is_game_feature_plugin: {
    epicSchema: S_epic_is_game_feature_plugin,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.IsGameFeaturePlugin", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Return whether or not a plugin is a Game Feature Plugin. Will error if no plugin of this name can be found by the Plugin Manager. Params: pluginName", "epic_call_tool"),
  },
  epic_is_plugin_creation_allowed: {
    epicSchema: S_epic_is_plugin_creation_allowed,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.IsPluginCreationAllowed", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Checks whether the editor settings permit plugin creation from the plugin browser. Params: none", "epic_call_tool"),
  },
  epic_is_plugin_modification_allowed: {
    epicSchema: S_epic_is_plugin_modification_allowed,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.IsPluginModificationAllowed", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Checks whether the editor settings permit modifying plugins from the plugin browser. Params: none", "epic_call_tool"),
  },
  epic_list_capabilities: {
    epicSchema: S_epic_list_capabilities,
    epicTool: { toolset: "AgentPluginToolset.AgentPluginToolset", name: "AgentPluginToolset.AgentPluginToolset.ListCapabilities", engines: ["5.9"] },
    ...bp("read", "[Epic AgentPluginToolset.AgentPluginToolset] Lists available capabilities from the current agent plugin directories and working directory. Discovery fails after 60 seconds. Warnings report rejected sources and discovery limits. Treat capability IDs as opaque values and pass them unchanged to GetCapabilities. Use an agent's Reference, not its ID, when a skill asks for a 'subagent_type'. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_discovered_game_feature_plugins: {
    epicSchema: S_epic_list_discovered_game_feature_plugins,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.ListDiscoveredGameFeaturePlugins", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Lists all discovered Game Feature Plugins sorted by name. This includes disabled plugins and plugins the Game Features system has not registered, which the other tools cannot resolve. Use the Plugins toolset to do general plugin enable/disable tasks. Params: none", "epic_call_tool"),
  },
  epic_list_discovered_plugins: {
    epicSchema: S_epic_list_discovered_plugins,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.ListDiscoveredPlugins", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Lists the names of all discovered plugins (enabled and disabled), sorted alphabetically. Params: none", "epic_call_tool"),
  },
  epic_list_enabled_game_feature_plugins: {
    epicSchema: S_epic_list_enabled_game_feature_plugins,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.ListEnabledGameFeaturePlugins", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic GameFeaturesToolset.GameFeaturesToolset] Lists enabled Game Feature Plugins that the Game Features system has registered, sorted by name. The project's policy can keep an enabled plugin from registering; ListDiscoveredGameFeaturePlugins still includes those. Use the Plugins toolset to do general plugin enable/disable tasks. Params: none", "epic_call_tool"),
  },
  epic_list_enabled_plugins: {
    epicSchema: S_epic_list_enabled_plugins,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.ListEnabledPlugins", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Lists the names of all enabled plugins, sorted alphabetically. Params: none", "epic_call_tool"),
  },
  epic_remove_additional_plugin_directory: {
    epicSchema: S_epic_remove_additional_plugin_directory,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.RemoveAdditionalPluginDirectory", engines: ["5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Removes a directory the project searches for plugins, saves the .uproject file, and rescans. Plugins from the directory that are enabled stay loaded until the editor restarts. Available on UE 5.9; absent from UE 5.8. Params: directory", "epic_call_tool"),
  },
  epic_remove_disallowed_plugin: {
    epicSchema: S_epic_remove_disallowed_plugin,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.RemoveDisallowedPlugin", engines: ["5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Removes an entry from a plugin's DisallowedPlugins array in its .uplugin file. Available on UE 5.9; absent from UE 5.8. Params: pluginName, disallowedPluginName", "epic_call_tool"),
  },
  epic_remove_game_feature_plugin_metadata_value: {
    epicSchema: S_epic_remove_game_feature_plugin_metadata_value,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.RemoveGameFeaturePluginMetadataValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Removes one of the configured additional metadata keys from the GFP's .uplugin. After removal the key round-trips through AdditionalMetadata as an empty string -- that is the engine parser's default for missing keys, surfaced honestly. Available on UE 5.9; absent from UE 5.8. Params: pluginName, key", "epic_call_tool"),
  },
  epic_remove_plugin_dependency: {
    epicSchema: S_epic_remove_plugin_dependency,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.RemovePluginDependency", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Removes a dependency entry from a plugin's Plugins array in its .uplugin file. Params: pluginName, dependencyName", "epic_call_tool"),
  },
  epic_request_activate_game_feature: {
    epicSchema: S_epic_request_activate_game_feature,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.RequestActivateGameFeature", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Requests activation of a Game Feature Plugin, loading it first if it is not already loaded. Params: pluginName", "epic_call_tool"),
  },
  epic_request_deactivate_game_feature: {
    epicSchema: S_epic_request_deactivate_game_feature,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.RequestDeactivateGameFeature", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Requests deactivation of a Game Feature Plugin, which leaves it loaded. Params: pluginName", "epic_call_tool"),
  },
  epic_request_load_game_feature: {
    epicSchema: S_epic_request_load_game_feature,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.RequestLoadGameFeature", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Requests load of a Game Feature Plugin without activating it. Use RequestActivateGameFeature instead to make the plugin's features live. Available on UE 5.9; absent from UE 5.8. Params: pluginName", "epic_call_tool"),
  },
  epic_request_unload_game_feature: {
    epicSchema: S_epic_request_unload_game_feature,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.RequestUnloadGameFeature", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Requests unload of a Game Feature Plugin, taking it out of the Loaded state. Available on UE 5.9; absent from UE 5.8. Params: pluginName, bKeepRegistered?", "epic_call_tool"),
  },
  epic_set_game_feature_plugin_built_in_auto_state: {
    epicSchema: S_epic_set_game_feature_plugin_built_in_auto_state,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.SetGameFeaturePluginBuiltInAutoState", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Sets the BuiltInInitialFeatureState on the GFP's .uplugin descriptor and writes the descriptor back to disk. EPluginToolsetGFPAutoState::Invalid is not accepted. Available on UE 5.9; absent from UE 5.8. Params: pluginName, newState", "epic_call_tool"),
  },
  epic_set_game_feature_plugin_metadata_value: {
    epicSchema: S_epic_set_game_feature_plugin_metadata_value,
    epicTool: { toolset: "GameFeaturesToolset.GameFeaturesToolset", name: "GameFeaturesToolset.GameFeaturesToolset.SetGameFeaturePluginMetadataValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic GameFeaturesToolset.GameFeaturesToolset] Writes a value for one of the configured additional metadata keys to the GFP's .uplugin. JsonValueLiteral must be a valid JSON literal (e.g. true, 42, \"hello\", {\"Major\":1}); booleans and numbers are stored as native JSON types rather than strings. Available on UE 5.9; absent from UE 5.8. Params: pluginName, key, jsonValueLiteral", "epic_call_tool"),
  },
  epic_set_plugin_enabled: {
    epicSchema: S_epic_set_plugin_enabled,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.SetPluginEnabled", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Enables or disables a plugin in the project and saves the .uproject file, checking it out via source control if enabled. Params: pluginName, bEnabled", "epic_call_tool"),
  },
  epic_update_plugin_dependency: {
    epicSchema: S_epic_update_plugin_dependency,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.UpdatePluginDependency", engines: ["5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Changes the settings of an existing dependency entry in a plugin's Plugins array in its .uplugin file. Anything not provided keeps its current value, as do fields the plugin editor doesn't show, such as platform lists. Available on UE 5.9; absent from UE 5.8. Params: pluginName, dependencyName, bOptional?, bEnabled?, bActivate?", "epic_call_tool"),
  },
  epic_update_plugin_descriptor: {
    epicSchema: S_epic_update_plugin_descriptor,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.UpdatePluginDescriptor", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic PluginToolset.PluginToolset] Updates a plugin's descriptor fields and writes them to its .uplugin file. Checks out the file via source control if source control is enabled. No-ops if the serialized descriptor is unchanged (file is not touched). Params: pluginName, newDescriptor", "epic_call_tool"),
  },
  epic_validate_new_plugin_name_and_location: {
    epicSchema: S_epic_validate_new_plugin_name_and_location,
    epicTool: { toolset: "PluginToolset.PluginToolset", name: "PluginToolset.PluginToolset.ValidateNewPluginNameAndLocation", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic PluginToolset.PluginToolset] Validates that PluginName and RelativePluginLocation are acceptable for a new plugin. Params: pluginName, relativePluginLocation, bPlaceInEngine, templateInfo", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  assetPath: z.string().optional().describe("The asset or mount point path to look up."),
  bActivate: z.boolean().optional().describe("Whether the dependency is activated along with this plugin. Only applies when the dependency is a Game Feature plugin."),
  bEnabled: z.boolean().optional().describe("Whether the dependency should be enabled."),
  bKeepRegistered: z.boolean().optional().describe("Leave the plugin registered so its assets stay discoverable and it can be reloaded without rescanning. Pass false to also unregister it, which is what makes th…"),
  bOptional: z.boolean().optional().describe("Whether the dependency is optional."),
  bPlaceInEngine: z.boolean().optional().describe("Use Engine Plugins directory rather than Game Plugins directory location. Only some Templates allow placing in Engine. See the TemplateInfo's bCanBePlacedInEng…"),
  capabilityIds: z.array(z.unknown()).optional().describe("Capability IDs previously returned by ListCapabilities. Unknown IDs are omitted."),
  comment: z.string().optional().describe("Why the plugin is disallowed. May be empty."),
  dependencyName: z.string().optional().describe("The name of the plugin to add as a dependency."),
  description: z.string().optional().describe("A description for the new plugin."),
  directory: z.string().optional().describe("The directory to add. Relative paths are resolved against the editor's working directory."),
  disallowedPluginName: z.string().optional().describe("The name of the plugin that PluginName must not use."),
  jsonValueLiteral: z.string().optional().describe("JSON literal text for the new value."),
  key: z.string().optional().describe("Metadata key (must appear in UGameFeaturesSubsystemSettings::AdditionalPluginMetadataKeys)."),
  newDescriptor: z.record(z.string(), z.unknown()).optional().describe("The new descriptor field values to apply."),
  newState: z.string().optional().describe("New BuiltInInitialFeatureState (must not be Invalid)."),
  pluginName: z.string().optional().describe("Name of the Game Feature Plugin."),
  pluginVersePath: z.string().optional().describe("Optional Verse path for the new plugin (eg, /Example.com/MyPlugin), or an empty Verse path to not assign one (the template's own Verse path, if any, still appl…"),
  pythonScriptAdditionalArgs: z.string().optional().describe("Optional additional arguments for the python post-create script"),
  relativePluginLocation: z.string().optional().describe("Parent directory for the new plugin relative to template's default location. This should be empty unless you wish to specify a subdirectory."),
  templateInfo: z.record(z.string(), z.unknown()).optional().describe("The plugin template to create from. Only its OnDiskPath identifies the template."),
};
