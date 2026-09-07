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

const S_epic_create_skill = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"description":{"type":"string"},"details":{"type":"object"}},"required":["folderPath","assetName","description","details"]} as const;
const S_epic_execute_tool_script = {"properties":{"script":{"type":"string"}},"required":["script"]} as const;
const S_epic_get_execution_environment = {"properties":{}} as const;
const S_epic_get_skills = {"properties":{"skillPaths":{"type":"array"}},"required":["skillPaths"]} as const;
const S_epic_list_skills = {"properties":{}} as const;
const S_epic_update_skill = {"properties":{"skillPath":{"type":"string"},"description":{"type":"string"},"details":{"type":"object"}},"required":["skillPath","description","details"]} as const;

/** 6 wrapped engine tools routed to the `epic` category. */
export const actions: Record<string, ActionSpec> = {
  epic_create_skill: bp(
    "mutate",
    "[Epic ToolsetRegistry.AgentSkillToolset] Creates a new AgentSkill. This should ONLY be called after getting explicit direction or permission from the user. Params: folderPath, assetName, description, details",
    "epic_call_tool",
    (p) => epicToolCall("ToolsetRegistry.AgentSkillToolset", "ToolsetRegistry.AgentSkillToolset.CreateSkill", S_epic_create_skill, p),
  ),
  epic_execute_tool_script: bp(
    "mutate",
    "[Epic editor_toolset.toolsets.programmatic.ProgrammaticToolset] Execute a Python script against the toolset APIs. Use this to batch multiple tool calls into a single script execution, reducing round-trips and context usage. IMPORTANT: Available modules and usage instructions are described by the value returned by `get_execution_environment`. You MUST call `get_execution_environment` once in the conversation before using this tool. Read the value in the `instructions` field in the returned environment info prior to calling this function, so that you understand what APIs are available and how to use them. Before writing a script that calls multiple tools, look up the output schemas (if available) for any tools you plan to use. This returns the JSON schema describing each tool's return value, so you know how to parse results and pass data between calls. Params: script",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "editor_toolset.toolsets.programmatic.ProgrammaticToolset.execute_tool_script", S_epic_execute_tool_script, p),
  ),
  epic_get_execution_environment: bp(
    "read",
    "[Epic editor_toolset.toolsets.programmatic.ProgrammaticToolset] Get details about execution environment. This includes instructions on how to write scripts, and constraints, such as what modules may be imported and the script entrypoint and function signature. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "editor_toolset.toolsets.programmatic.ProgrammaticToolset.get_execution_environment", S_epic_get_execution_environment, p),
  ),
  epic_get_skills: bp(
    "read",
    "[Epic ToolsetRegistry.AgentSkillToolset] Returns detailed information about a specific set of AgentSkills. Params: skillPaths",
    "epic_call_tool",
    (p) => epicToolCall("ToolsetRegistry.AgentSkillToolset", "ToolsetRegistry.AgentSkillToolset.GetSkills", S_epic_get_skills, p),
  ),
  epic_list_skills: bp(
    "read",
    "[Epic ToolsetRegistry.AgentSkillToolset] Gets a summary of all AgentSkills in the project. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("ToolsetRegistry.AgentSkillToolset", "ToolsetRegistry.AgentSkillToolset.ListSkills", S_epic_list_skills, p),
  ),
  epic_update_skill: bp(
    "mutate",
    "[Epic ToolsetRegistry.AgentSkillToolset] Updates an existing AgentSkill. This should ONLY be called after getting explicit direction or permission from the user. Params: skillPath, description, details",
    "epic_call_tool",
    (p) => epicToolCall("ToolsetRegistry.AgentSkillToolset", "ToolsetRegistry.AgentSkillToolset.UpdateSkill", S_epic_update_skill, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  assetName: z.string().optional().describe("The name of the skill to create i.e. MySkill."),
  description: z.string().optional().describe("The brief description of the skill."),
  details: z.record(z.unknown()).optional().describe("Detailed information about how to use the skill."),
  folderPath: z.string().optional().describe("The folder in which to create the skill. i.e. /Game/Skills/."),
  script: z.string().optional(),
  skillPath: z.string().optional().describe("The full path to the skill to modify i.e. /Game/Skills/MySkill.MySkill_C."),
  skillPaths: z.array(z.unknown()).optional().describe("A list of paths to the AgentSkills to retrieve."),
};
