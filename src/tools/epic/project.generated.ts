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

const S_epic_compile_live_coding = {"properties":{}} as const;
const S_epic_create_directory = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_delete_file = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_discover_tests = {"properties":{"bForceRediscover":{"type":"boolean"}}} as const;
const S_epic_download_url = {"properties":{"url":{"type":"string"},"destinationPath":{"type":"string"},"bOverwrite":{"type":"boolean"}},"required":["url","destinationPath","bOverwrite"]} as const;
const S_epic_edit_text_by_anchor = {"properties":{"path":{"type":"string"},"anchorText":{"type":"string"},"newText":{"type":"string"},"editMode":{"type":"string","enum":["Replace","InsertBefore","InsertAfter"]}},"required":["path","anchorText","newText","editMode"]} as const;
const S_epic_edit_text_by_line_range = {"properties":{"path":{"type":"string"},"firstLine":{"type":"integer"},"lastLine":{"type":"integer"},"newText":{"type":"string"},"editMode":{"type":"string","enum":["Replace","InsertBefore","InsertAfter"]}},"required":["path","firstLine","lastLine","newText","editMode"]} as const;
const S_epic_enable_live_coding = {"properties":{}} as const;
const S_epic_get_section_property_values = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"},"propertyNames":{"type":"array","items":{"type":"string"}}},"required":["containerName","categoryName","sectionName","propertyNames"]} as const;
const S_epic_get_section_schema = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"}},"required":["containerName","categoryName","sectionName"]} as const;
const S_epic_get_supported_platforms = {"properties":{}} as const;
const S_epic_get_test_results = {"properties":{}} as const;
const S_epic_get_test_status = {"properties":{}} as const;
const S_epic_is_live_coding_ready = {"properties":{}} as const;
const S_epic_list_available_platforms = {"properties":{}} as const;
const S_epic_list_categories = {"properties":{"containerName":{"type":"string"}},"required":["containerName"]} as const;
const S_epic_list_containers = {"properties":{}} as const;
const S_epic_list_directory = {"properties":{"path":{"type":"string"},"firstEntry":{"type":"integer"},"maximumEntries":{"type":"integer"}},"required":["path"]} as const;
const S_epic_list_sections = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"}},"required":["containerName","categoryName"]} as const;
const S_epic_list_tests = {"properties":{"nameFilter":{"type":"string"},"tagFilter":{"type":"string"},"limit":{"type":"integer"}}} as const;
const S_epic_read_text_file = {"properties":{"path":{"type":"string"},"firstLine":{"type":"integer"},"maximumLines":{"type":"integer"},"continuationCursor":{"type":"string"},"maximumResponseCharacters":{"type":"integer"}},"required":["path"]} as const;
const S_epic_reset_section_to_defaults = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"}},"required":["containerName","categoryName","sectionName"]} as const;
const S_epic_resolve_path = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_run_tests = {"properties":{"testNames":{"type":"array","items":{"type":"string"}}},"required":["testNames"]} as const;
const S_epic_run_tests_by_filter = {"properties":{"filterExpression":{"type":"string"}},"required":["filterExpression"]} as const;
const S_epic_save_section = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"}},"required":["containerName","categoryName","sectionName"]} as const;
const S_epic_search_settings = {"properties":{"query":{"type":"string"}},"required":["query"]} as const;
const S_epic_set_section_properties = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"},"propertiesJson":{"type":"string"}},"required":["containerName","categoryName","sectionName","propertiesJson"]} as const;
const S_epic_set_supported_platforms = {"properties":{"platforms":{"type":"array","items":{"type":"string"}}},"required":["platforms"]} as const;
const S_epic_stop_tests = {"properties":{}} as const;
const S_epic_write_text_file = {"properties":{"path":{"type":"string"},"contents":{"type":"string"},"bOverwrite":{"type":"boolean"}},"required":["path","contents","bOverwrite"]} as const;

/** 31 wrapped engine tools routed to the `project` category. */
export const actions: Record<string, ActionSpec> = {
  epic_compile_live_coding: {
    epicSchema: S_epic_compile_live_coding,
    epicTool: { toolset: "LiveCodingToolset.LiveCodingToolset", name: "LiveCodingToolset.LiveCodingToolset.CompileLiveCoding", engines: ["5.9"] },
    ...bp("unknown", "[Epic LiveCodingToolset.LiveCodingToolset] Triggers a Live Coding compile and returns a pending result that resolves once the compile finishes. Live Coding must be enabled in Editor Preferences. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_create_directory: {
    epicSchema: S_epic_create_directory,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.CreateDirectory", engines: ["5.9"] },
    ...bp("mutate", "[Epic FileSystemToolset.FileSystemToolset] Creates a directory and any missing parents. A directory that already exists is a success. Available on UE 5.9; absent from UE 5.8. Params: path", "epic_call_tool"),
  },
  epic_delete_file: {
    epicSchema: S_epic_delete_file,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.DeleteFile", engines: ["5.9"] },
    ...bp("mutate", "[Epic FileSystemToolset.FileSystemToolset] Deletes one file. Directories are rejected. Recursive deletion is unavailable. Source-controlled files are marked for deletion when source control is available. Available on UE 5.9; absent from UE 5.8. Params: path", "epic_call_tool"),
  },
  epic_discover_tests: {
    epicSchema: S_epic_discover_tests,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.DiscoverTests", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic AutomationTestToolset.AutomationTestToolset] Initialize automation worker discovery and load the test list. Must be called once before ListTests or RunTests. Takes several seconds as it discovers the local automation worker and enumerates all registered tests. Returns an async result that completes with true when tests are available, or an error if discovery fails. Params: bForceRediscover?", "epic_call_tool"),
  },
  epic_download_url: {
    epicSchema: S_epic_download_url,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.DownloadUrl", engines: ["5.9"] },
    ...bp("mutate", "[Epic FileSystemToolset.FileSystemToolset] Downloads an HTTPS URL to a file. Redirects are not followed. Responses above 1 GiB are rejected. Local, private, and selected special-use addresses are rejected. Only HTTP 200 responses succeed. Host resolution and the HTTP request each time out after 30 seconds. Existing read-only files are checked out through source control before replacement. Available on UE 5.9; absent from UE 5.8. Params: url, destinationPath, bOverwrite", "epic_call_tool"),
  },
  epic_edit_text_by_anchor: {
    epicSchema: S_epic_edit_text_by_anchor,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.EditTextByAnchor", engines: ["5.9"] },
    ...bp("mutate", "[Epic FileSystemToolset.FileSystemToolset] Edits the unique occurrence of a piece of text. Source files and results above 1 MiB are rejected. Big-endian UTF-16 files are rejected. Existing read-only files are checked out through source control before replacement. AnchorText is matched exactly first. If that fails, LF and CRLF variants are tried. NewText uses the file's existing line ending. Replace changes the anchor text itself. InsertBefore and InsertAfter insert whole lines and preserve the anchor. Available on UE 5.9; absent from UE 5.8. Params: path, anchorText, newText, editMode", "epic_call_tool"),
  },
  epic_edit_text_by_line_range: {
    epicSchema: S_epic_edit_text_by_line_range,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.EditTextByLineRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic FileSystemToolset.FileSystemToolset] Edits an inclusive range of one-based lines. Source files and results above 1 MiB are rejected. Big-endian UTF-16 files are rejected. Existing read-only files are checked out through source control before replacement. Lines outside the range remain unchanged, including their line endings. NewText uses the file's existing line ending. Available on UE 5.9; absent from UE 5.8. Params: path, firstLine, lastLine, newText, editMode", "epic_call_tool"),
  },
  epic_enable_live_coding: {
    epicSchema: S_epic_enable_live_coding,
    epicTool: { toolset: "LiveCodingToolset.LiveCodingToolset", name: "LiveCodingToolset.LiveCodingToolset.EnableLiveCoding", engines: ["5.9"] },
    ...bp("unknown", "[Epic LiveCodingToolset.LiveCodingToolset] Call before CompileLiveCoding on a session launched with Live Coding off; starts the Broker/Agent. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_section_property_values: {
    epicSchema: S_epic_get_section_property_values,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionPropertyValues", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Returns the current values of the specified properties as a JSON object. Params: containerName, categoryName, sectionName, propertyNames", "epic_call_tool"),
  },
  epic_get_section_schema: {
    epicSchema: S_epic_get_section_schema,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionSchema", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Returns a JSON Schema describing the user-visible properties of a settings section. The schema maps each property name to its type, description, and constraints. A section that uses a custom widget has no settings object and so no schema. Params: containerName, categoryName, sectionName", "epic_call_tool"),
  },
  epic_get_supported_platforms: {
    epicSchema: S_epic_get_supported_platforms,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.GetSupportedPlatforms", engines: ["5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Returns the platforms the project declares support for in its .uproject file. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_test_results: {
    epicSchema: S_epic_get_test_results,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.GetTestResults", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic AutomationTestToolset.AutomationTestToolset] Get detailed results for the current or most recent test run. Requires DiscoverTests() to have completed. Params: none", "epic_call_tool"),
  },
  epic_get_test_status: {
    epicSchema: S_epic_get_test_status,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.GetTestStatus", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic AutomationTestToolset.AutomationTestToolset] Get a lightweight status snapshot of the automation controller. Requires DiscoverTests() to have completed. Params: none", "epic_call_tool"),
  },
  epic_is_live_coding_ready: {
    epicSchema: S_epic_is_live_coding_ready,
    epicTool: { toolset: "LiveCodingToolset.LiveCodingToolset", name: "LiveCodingToolset.LiveCodingToolset.IsLiveCodingReady", engines: ["5.9"] },
    ...bp("read", "[Epic LiveCodingToolset.LiveCodingToolset] Poll before calling CompileLiveCoding to avoid blocking on the Broker's initial module load. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_available_platforms: {
    epicSchema: S_epic_list_available_platforms,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.ListAvailablePlatforms", engines: ["5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the platforms a project can declare support for, the choices in the Project Settings Supported Platforms section. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_categories: {
    epicSchema: S_epic_list_categories,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.ListCategories", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the names of all categories within a settings container, sorted alphabetically. Params: containerName", "epic_call_tool"),
  },
  epic_list_containers: {
    epicSchema: S_epic_list_containers,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.ListContainers", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the names of all known settings containers, sorted alphabetically. Common containers are \"Editor\" and \"Project\". Params: none", "epic_call_tool"),
  },
  epic_list_directory: {
    epicSchema: S_epic_list_directory,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.ListDirectory", engines: ["5.9"] },
    ...bp("read", "[Epic FileSystemToolset.FileSystemToolset] Lists a range from the immediate contents of a directory. Directories with more than 100000 immediate entries are rejected before filtering or range selection. Available on UE 5.9; absent from UE 5.8. Params: path, firstEntry?, maximumEntries?", "epic_call_tool"),
  },
  epic_list_sections: {
    epicSchema: S_epic_list_sections,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.ListSections", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the names of all sections within a settings category, sorted alphabetically. Params: containerName, categoryName", "epic_call_tool"),
  },
  epic_list_tests: {
    epicSchema: S_epic_list_tests,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.ListTests", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic AutomationTestToolset.AutomationTestToolset] List available automation tests. Requires DiscoverTests() to have completed. Params: nameFilter?, tagFilter?, limit?", "epic_call_tool"),
  },
  epic_read_text_file: {
    epicSchema: S_epic_read_text_file,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.ReadTextFile", engines: ["5.9"] },
    ...bp("read", "[Epic FileSystemToolset.FileSystemToolset] Reads lines or fragments without line numbers, budgeting the complete JSON response for EDA. Decode Contents once as JSON when ContentsEncoding is \"json-string\" to recover NUL and U+FFFE/U+FFFF without transport substitution. Oversized lines are returned in character-aligned fragments. Pass NextCursor to continue without losing text. Supports UTF-8 and byte-order-marked UTF-16. Legacy single-byte text is not supported on all platforms. Available on UE 5.9; absent from UE 5.8. Params: path, firstLine?, maximumLines?, continuationCursor?, maximumResponseCharacters?", "epic_call_tool"),
  },
  epic_reset_section_to_defaults: {
    epicSchema: S_epic_reset_section_to_defaults,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.ResetSectionToDefaults", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Resets the settings in a section to their default values. Project sections saved to a Default*.ini cannot be reset, matching the Settings editor. Params: containerName, categoryName, sectionName", "epic_call_tool"),
  },
  epic_resolve_path: {
    epicSchema: S_epic_resolve_path,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.ResolvePath", engines: ["5.9"] },
    ...bp("read", "[Epic FileSystemToolset.FileSystemToolset] Resolves a native path, Unreal virtual-content path, or plugin path that the plugin manager finds to an absolute native path. Available on UE 5.9; absent from UE 5.8. Params: path", "epic_call_tool"),
  },
  epic_run_tests: {
    epicSchema: S_epic_run_tests,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.RunTests", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic AutomationTestToolset.AutomationTestToolset] Run a set of automation tests by name. Requires DiscoverTests() to have completed. Starts executing the specified tests and returns an async result that completes with a summary when all tests finish. Params: testNames", "epic_call_tool"),
  },
  epic_run_tests_by_filter: {
    epicSchema: S_epic_run_tests_by_filter,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.RunTestsByFilter", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic AutomationTestToolset.AutomationTestToolset] Run automation tests selected by a filter expression. Requires DiscoverTests() to have completed. Much faster than RunTests when targeting a large batch because the engine narrows the report tree in a single pass instead of running a per-leaf membership check against the requested name list. Filter syntax (multiple expressions joined by '+'): \"StartsWith:System.Engine\" prefix match against the full test path \"^Foo\" prefix anchor (equivalent to StartsWith:) \"Bar$\" suffix anchor \"Substring\" bare token matches anywhere in the path \"Group:Smoke\" expand a named group from AutomationControllerSettings ini Groups Returns an async result that completes with the same summary shape as RunTests. Params: filterExpression", "epic_call_tool"),
  },
  epic_save_section: {
    epicSchema: S_epic_save_section,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.SaveSection", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Saves the settings in a section. The default config file is checked out first when source control is enabled. Params: containerName, categoryName, sectionName", "epic_call_tool"),
  },
  epic_search_settings: {
    epicSchema: S_epic_search_settings,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.SearchSettings", engines: ["5.9"] },
    ...bp("read", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Finds settings by the label shown in the Settings editor, the property name, the tooltip, or the console variable the property is bound to, across every container. Every word in the query must match, ignoring case. A section whose own name matches is returned with an empty PropertyName. Available on UE 5.9; absent from UE 5.8. Params: query", "epic_call_tool"),
  },
  epic_set_section_properties: {
    epicSchema: S_epic_set_section_properties,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.SetSectionProperties", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Sets one or more properties on a settings section from a JSON object and saves. PropertiesJson must be a JSON object mapping property names to new values, in the same format returned by GetSectionPropertyValues. If any value is rejected, none are applied and nothing is saved. The default config file is checked out first when source control is enabled. Params: containerName, categoryName, sectionName, propertiesJson", "epic_call_tool"),
  },
  epic_set_supported_platforms: {
    epicSchema: S_epic_set_supported_platforms,
    epicTool: { toolset: "ConfigSettingsToolset.ConfigSettingsToolset", name: "ConfigSettingsToolset.ConfigSettingsToolset.SetSupportedPlatforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Replaces the platforms the project declares support for and saves the .uproject file, checking it out first when source control is enabled. Packaging, running or cooking for a platform not in the list warns. Available on UE 5.9; absent from UE 5.8. Params: platforms", "epic_call_tool"),
  },
  epic_stop_tests: {
    epicSchema: S_epic_stop_tests,
    epicTool: { toolset: "AutomationTestToolset.AutomationTestToolset", name: "AutomationTestToolset.AutomationTestToolset.StopTests", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic AutomationTestToolset.AutomationTestToolset] Stop all currently running tests. Requires DiscoverTests() to have completed. If a RunTests async result is pending, it will be completed with an error. Params: none", "epic_call_tool"),
  },
  epic_write_text_file: {
    epicSchema: S_epic_write_text_file,
    epicTool: { toolset: "FileSystemToolset.FileSystemToolset", name: "FileSystemToolset.FileSystemToolset.WriteTextFile", engines: ["5.9"] },
    ...bp("mutate", "[Epic FileSystemToolset.FileSystemToolset] Writes a complete UTF-8 text file and creates missing parent directories. Existing read-only files are checked out through source control before replacement. Available on UE 5.9; absent from UE 5.8. Params: path, contents, bOverwrite", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  anchorText: z.string().optional().describe("Text locating the edit. It cannot be empty and must be unique."),
  bForceRediscover: z.boolean().optional().describe("When true, bypass the cached report tree and re-poll workers. Used after reloading Python test modules mid-session."),
  bOverwrite: z.boolean().optional().describe("Whether an existing file can be replaced."),
  categoryName: z.string().optional().describe("The name of the category (e.g. \"Engine\")."),
  containerName: z.string().optional().describe("The name of the container (e.g. \"Project\")."),
  contents: z.string().optional().describe("New file contents."),
  continuationCursor: z.string().optional().describe("Cursor from the previous result, or empty for a new read. Detectable size/timestamp changes invalidate it; it is not a snapshot."),
  destinationPath: z.string().optional().describe("Native, Unreal virtual-content, or plugin destination path."),
  editMode: z.string().optional().describe("Whether to replace the anchor or insert around its line."),
  filterExpression: z.string().optional().describe("Filter expression as described above."),
  firstEntry: z.number().optional().describe("One-based first entry to return after sorting by path."),
  firstLine: z.number().optional().describe("One-based first line in the selected range."),
  lastLine: z.number().optional().describe("One-based last line in the selected range, inclusive."),
  limit: z.number().optional().describe("Maximum number of tests to return (0 = unlimited, default 200)."),
  maximumEntries: z.number().optional().describe("Result limit. bTruncated reports whether more entries remain."),
  maximumLines: z.number().optional().describe("Result limit. bTruncated reports whether more text remains."),
  maximumResponseCharacters: z.number().optional().describe("Maximum Unicode characters in the compact JSON response, including returnValue and escaping. Defaults to EDA/TCS's 80000-character limit; use a smaller value i…"),
  nameFilter: z.string().optional().describe("Optional substring filter applied to the test's full path."),
  newText: z.string().optional().describe("Replacement or inserted text. Empty text with Replace deletes the anchor."),
  path: z.string().optional().describe("Native, Unreal virtual-content, or plugin directory path."),
  platforms: z.array(z.unknown()).optional().describe("Names from ListAvailablePlatforms, or ones the project already lists whose platform data isn't installed on this machine. Empty supports every platform (\"All P…"),
  propertiesJson: z.string().optional().describe("JSON object with property name to new value pairs."),
  propertyNames: z.array(z.unknown()).optional().describe("The names of the properties to read."),
  query: z.string().optional().describe("Space-separated words to search for, e.g. \"window title\" or \"gc.TimeBetweenPurgingPendingKillObjects\"."),
  sectionName: z.string().optional().describe("The name of the section (e.g. \"General\")."),
  tagFilter: z.string().optional().describe("Optional substring filter applied to the test's tags."),
  testNames: z.array(z.unknown()).optional().describe("Array of full test paths as returned by ListTests."),
  url: z.string().optional().describe("HTTPS URL to download."),
};
