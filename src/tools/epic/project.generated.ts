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

const S_epic_discover_tests = {"properties":{"bForceRediscover":{"type":"boolean"}}} as const;
const S_epic_get_section_property_values = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"},"propertyNames":{"type":"array"}},"required":["containerName","categoryName","sectionName","propertyNames"]} as const;
const S_epic_get_section_schema = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"}},"required":["containerName","categoryName","sectionName"]} as const;
const S_epic_get_test_results = {"properties":{}} as const;
const S_epic_get_test_status = {"properties":{}} as const;
const S_epic_list_categories = {"properties":{"containerName":{"type":"string"}},"required":["containerName"]} as const;
const S_epic_list_containers = {"properties":{}} as const;
const S_epic_list_sections = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"}},"required":["containerName","categoryName"]} as const;
const S_epic_list_tests = {"properties":{"nameFilter":{"type":"string"},"tagFilter":{"type":"string"},"limit":{"type":"integer"}},"required":["nameFilter","tagFilter"]} as const;
const S_epic_reset_section_to_defaults = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"}},"required":["containerName","categoryName","sectionName"]} as const;
const S_epic_run_tests = {"properties":{"testNames":{"type":"array"}},"required":["testNames"]} as const;
const S_epic_run_tests_by_filter = {"properties":{"filterExpression":{"type":"string"}},"required":["filterExpression"]} as const;
const S_epic_save_section = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"}},"required":["containerName","categoryName","sectionName"]} as const;
const S_epic_set_section_properties = {"properties":{"containerName":{"type":"string"},"categoryName":{"type":"string"},"sectionName":{"type":"string"},"propertiesJson":{"type":"string"}},"required":["containerName","categoryName","sectionName","propertiesJson"]} as const;
const S_epic_stop_tests = {"properties":{}} as const;

/** 15 wrapped engine tools routed to the `project` category. */
export const actions: Record<string, ActionSpec> = {
  epic_discover_tests: bp(
    "mutate",
    "[Epic AutomationTestToolset.AutomationTestToolset] Initialize automation worker discovery and load the test list. Must be called once before ListTests or RunTests. Takes several seconds as it discovers the local automation worker and enumerates all registered tests. Returns an async result that completes with a JSON status object when tests are available, or an error if discovery fails. Params: bForceRediscover?",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.DiscoverTests", S_epic_discover_tests, p),
  ),
  epic_get_section_property_values: bp(
    "read",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Returns the current values of the specified properties as a JSON object. Raises an error if the section does not exist, has no settings object, or any requested property cannot be read. Params: containerName, categoryName, sectionName, propertyNames",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionPropertyValues", S_epic_get_section_property_values, p),
  ),
  epic_get_section_schema: bp(
    "read",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Returns a JSON Schema describing the user-visible properties of a settings section. The schema maps each property name to its type, description, and constraints. Raises an error if the section does not exist or has no backing settings object (e.g. uses a custom widget instead). Params: containerName, categoryName, sectionName",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.GetSectionSchema", S_epic_get_section_schema, p),
  ),
  epic_get_test_results: bp(
    "read",
    "[Epic AutomationTestToolset.AutomationTestToolset] Get detailed results for the current or most recent test run. Requires DiscoverTests() to have completed. Returns a JSON object with per-test state, duration, errors, and warnings. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.GetTestResults", S_epic_get_test_results, p),
  ),
  epic_get_test_status: bp(
    "read",
    "[Epic AutomationTestToolset.AutomationTestToolset] Get a lightweight status snapshot of the automation controller. Requires DiscoverTests() to have completed. Returns a JSON object with the controller state, enabled test count, and completion/pass/fail counts. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.GetTestStatus", S_epic_get_test_status, p),
  ),
  epic_list_categories: bp(
    "read",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the names of all categories within a settings container, sorted alphabetically. Raises an error if the container does not exist. Params: containerName",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.ListCategories", S_epic_list_categories, p),
  ),
  epic_list_containers: bp(
    "read",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the names of all known settings containers, sorted alphabetically. Common containers are \"Editor\" and \"Project\". Params: none",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.ListContainers", S_epic_list_containers, p),
  ),
  epic_list_sections: bp(
    "read",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Lists the names of all sections within a settings category, sorted alphabetically. Raises an error if the container or category does not exist. Params: containerName, categoryName",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.ListSections", S_epic_list_sections, p),
  ),
  epic_list_tests: bp(
    "read",
    "[Epic AutomationTestToolset.AutomationTestToolset] List available automation tests. Requires DiscoverTests() to have completed. Returns a JSON object: {\"tests\": [\"path1\", ...], \"total\": N, \"returned\": N}. Params: nameFilter, tagFilter, limit?",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.ListTests", S_epic_list_tests, p),
  ),
  epic_reset_section_to_defaults: bp(
    "mutate",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Resets the settings in a section to their default values. Raises an error if the section does not exist or reset is not supported. Params: containerName, categoryName, sectionName",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.ResetSectionToDefaults", S_epic_reset_section_to_defaults, p),
  ),
  epic_run_tests: bp(
    "mutate",
    "[Epic AutomationTestToolset.AutomationTestToolset] Run a set of automation tests by name. Requires DiscoverTests() to have completed. Starts executing the specified tests and returns an async result that completes with a JSON summary when all tests finish. Params: testNames",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.RunTests", S_epic_run_tests, p),
  ),
  epic_run_tests_by_filter: bp(
    "mutate",
    "[Epic AutomationTestToolset.AutomationTestToolset] Run automation tests selected by a filter expression. Requires DiscoverTests() to have completed. Much faster than RunTests when targeting a large batch because the engine narrows the report tree in a single pass instead of running a per-leaf membership check against the requested name list. Filter syntax (multiple expressions joined by '+'): \"StartsWith:System.Engine\" prefix match against the full test path \"^Foo\" prefix anchor (equivalent to StartsWith:) \"Bar$\" suffix anchor \"Substring\" bare token matches anywhere in the path \"Group:Smoke\" expand a named group from AutomationControllerSettings ini Groups Returns an async result that completes with the same JSON summary as RunTests. Params: filterExpression",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.RunTestsByFilter", S_epic_run_tests_by_filter, p),
  ),
  epic_save_section: bp(
    "mutate",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Saves the settings in a section. Raises an error if the section does not exist or saving is not supported. Params: containerName, categoryName, sectionName",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.SaveSection", S_epic_save_section, p),
  ),
  epic_set_section_properties: bp(
    "mutate",
    "[Epic ConfigSettingsToolset.ConfigSettingsToolset] Sets one or more properties on a settings section from a JSON object and saves. PropertiesJson must be a JSON object mapping property names to new values, in the same format returned by GetSectionPropertyValues. Raises an error if the section does not exist, cannot be edited, has no settings object, the default config file is not writable, or any property cannot be set. Params: containerName, categoryName, sectionName, propertiesJson",
    "epic_call_tool",
    (p) => epicToolCall("ConfigSettingsToolset.ConfigSettingsToolset", "ConfigSettingsToolset.ConfigSettingsToolset.SetSectionProperties", S_epic_set_section_properties, p),
  ),
  epic_stop_tests: bp(
    "mutate",
    "[Epic AutomationTestToolset.AutomationTestToolset] Stop all currently running tests. Requires DiscoverTests() to have completed. If a RunTests async result is pending, it will be completed with an error. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("AutomationTestToolset.AutomationTestToolset", "AutomationTestToolset.AutomationTestToolset.StopTests", S_epic_stop_tests, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  bForceRediscover: z.boolean().optional().describe("When true, bypass the cached report tree and re-poll workers. Used after reloading Python test modules mid-session."),
  categoryName: z.string().optional().describe("The name of the category (e.g. \"Engine\")."),
  containerName: z.string().optional().describe("The name of the container (e.g. \"Project\")."),
  filterExpression: z.string().optional().describe("Filter expression as described above."),
  limit: z.number().optional().describe("Maximum number of tests to return (0 = unlimited, default 200)."),
  nameFilter: z.string().optional().describe("Optional substring filter applied to the test's full path."),
  propertiesJson: z.string().optional().describe("JSON object with property name to new value pairs."),
  propertyNames: z.array(z.unknown()).optional().describe("The names of the properties to read."),
  sectionName: z.string().optional().describe("The name of the section (e.g. \"General\")."),
  tagFilter: z.string().optional().describe("Optional substring filter applied to the test's tags."),
  testNames: z.array(z.unknown()).optional().describe("Array of full test paths as returned by ListTests."),
};
