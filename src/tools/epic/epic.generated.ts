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

const S_epic_create_markdown_knowledge_base = {"properties":{"alias":{"type":"string"},"description":{"type":"string"},"relativePath":{"type":"string"}},"required":["alias","description","relativePath"]} as const;
const S_epic_create_skill = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"description":{"type":"string"},"details":{"type":"object","properties":{"instructions":{"type":"string"}}}},"required":["folderPath","assetName","description","details"]} as const;
const S_epic_execute_tool_script = {"properties":{"script":{"type":"string"}},"required":["script"]} as const;
const S_epic_get_docked_context = {"properties":{}} as const;
const S_epic_get_execution_environment = {"properties":{}} as const;
const S_epic_get_knowledge_entry = {"properties":{"alias":{"type":"string"},"entryId":{"type":"string"}},"required":["alias","entryId"]} as const;
const S_epic_get_project_context = {"properties":{}} as const;
const S_epic_get_skills = {"properties":{"skillPaths":{"type":"array","items":{"type":"string"}}},"required":["skillPaths"]} as const;
const S_epic_list_knowledge_base_tags = {"properties":{"alias":{"type":"string"},"prefix":{"type":"string"},"limit":{"type":"integer"}},"required":["alias","prefix"]} as const;
const S_epic_list_knowledge_bases = {"properties":{}} as const;
const S_epic_list_skills = {"properties":{}} as const;
const S_epic_query_knowledge_base = {"properties":{"alias":{"type":"string"},"text":{"type":"string"},"tags":{"type":"array","items":{"type":"string"}},"limit":{"type":"integer"}},"required":["alias","text","tags"]} as const;
const S_epic_update_skill = {"properties":{"skillPath":{"type":"string"},"description":{"type":"string"},"details":{"type":"object","properties":{"instructions":{"type":"string"}}}},"required":["skillPath","description","details"]} as const;
const S_epic_write_knowledge = {"properties":{"alias":{"type":"string"},"fileId":{"type":"string"},"summary":{"type":"string"},"tags":{"type":"array","items":{"type":"string"}},"bodyJson":{"type":"string"},"entryId":{"type":"string"}},"required":["alias","fileId","summary","tags","bodyJson","entryId"]} as const;

/** 14 wrapped engine tools routed to the `epic` category. */
export const actions: Record<string, ActionSpec> = {
  epic_create_markdown_knowledge_base: {
    epicSchema: S_epic_create_markdown_knowledge_base,
    epicTool: { toolset: "KnowledgeBaseToolset.KnowledgeBaseTools", name: "KnowledgeBaseToolset.KnowledgeBaseTools.CreateMarkdownKnowledgeBase", engines: ["5.9"] },
    ...bp("mutate", "[Epic KnowledgeBaseToolset.KnowledgeBaseTools] Creates an empty markdown base under the project's Content/KnowledgeBases folder, writes the marker file that lets it be rediscovered on restart, and registers it. An empty base is valid; the first WriteKnowledge creates its first file. Raises an error if the alias is already registered or not a valid identifier, if RelativePath is not one or two plain segments, if the directory already belongs to another base, or if it cannot be written. Available on UE 5.9; absent from UE 5.8. Params: alias, description, relativePath", "epic_call_tool"),
  },
  epic_create_skill: {
    epicSchema: S_epic_create_skill,
    epicTool: { toolset: "ToolsetRegistry.AgentSkillToolset", name: "ToolsetRegistry.AgentSkillToolset.CreateSkill", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic ToolsetRegistry.AgentSkillToolset] Creates a new AgentSkill. This should ONLY be called after getting explicit direction or permission from the user. Params: folderPath, assetName, description, details", "epic_call_tool"),
  },
  epic_execute_tool_script: {
    epicSchema: S_epic_execute_tool_script,
    epicTool: { toolset: "editor_toolset.toolsets.programmatic.ProgrammaticToolset", name: "editor_toolset.toolsets.programmatic.ProgrammaticToolset.execute_tool_script", engines: ["5.8"] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.programmatic.ProgrammaticToolset] Execute a Python script against the toolset APIs. Use this to batch multiple tool calls into a single script execution, reducing round-trips and context usage. IMPORTANT: Available modules and usage instructions are described by the value returned by `get_execution_environment`. You MUST call `get_execution_environment` once in the conversation before using this tool. Read the value in the `instructions` field in the returned environment info prior to calling this function, so that you understand what APIs are available and how to use them. Before writing a script that calls multiple tools, look up the output schemas (if available) for any tools you plan to use. This returns the JSON schema describing each tool's return value, so you know how to parse results and pass data between calls. Available on UE 5.8; absent from UE 5.9. Params: script", "epic_call_tool"),
  },
  epic_get_docked_context: {
    epicSchema: S_epic_get_docked_context,
    epicTool: { toolset: "AIAssistantEditor.AIAssistantToolset", name: "AIAssistantEditor.AIAssistantToolset.GetDockedContext", engines: ["5.9"] },
    ...bp("read", "[Epic AIAssistantEditor.AIAssistantToolset] Returns the UE tab layout and editor context in the window hosting the assistant. Tab information is available for tool windows with no asset editor. The result is empty when the assistant is closed or detached from a window. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_execution_environment: {
    epicSchema: S_epic_get_execution_environment,
    epicTool: { toolset: "editor_toolset.toolsets.programmatic.ProgrammaticToolset", name: "editor_toolset.toolsets.programmatic.ProgrammaticToolset.get_execution_environment", engines: ["5.8"] },
    ...bp("read", "[Epic editor_toolset.toolsets.programmatic.ProgrammaticToolset] Get details about execution environment. This includes instructions on how to write scripts, and constraints, such as what modules may be imported and the script entrypoint and function signature. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_knowledge_entry: {
    epicSchema: S_epic_get_knowledge_entry,
    epicTool: { toolset: "KnowledgeBaseToolset.KnowledgeBaseTools", name: "KnowledgeBaseToolset.KnowledgeBaseTools.GetKnowledgeEntry", engines: ["5.9"] },
    ...bp("read", "[Epic KnowledgeBaseToolset.KnowledgeBaseTools] Returns one entry by id with its full body. Ids are exact, so take them from QueryKnowledgeBase hits rather than composing them; a miss names the nearest ids the base holds. Raises an error if the alias is not registered or no entry has that id. Available on UE 5.9; absent from UE 5.8. Params: alias, entryId", "epic_call_tool"),
  },
  epic_get_project_context: {
    epicSchema: S_epic_get_project_context,
    epicTool: { toolset: "AIAssistantEditor.AIAssistantToolset", name: "AIAssistantEditor.AIAssistantToolset.GetProjectContext", engines: ["5.9"] },
    ...bp("read", "[Epic AIAssistantEditor.AIAssistantToolset] Returns important information about the current project and/or current user. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_skills: {
    epicSchema: S_epic_get_skills,
    epicTool: { toolset: "ToolsetRegistry.AgentSkillToolset", name: "ToolsetRegistry.AgentSkillToolset.GetSkills", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ToolsetRegistry.AgentSkillToolset] Returns detailed information about a specific set of AgentSkills. Params: skillPaths", "epic_call_tool"),
  },
  epic_list_knowledge_base_tags: {
    epicSchema: S_epic_list_knowledge_base_tags,
    epicTool: { toolset: "KnowledgeBaseToolset.KnowledgeBaseTools", name: "KnowledgeBaseToolset.KnowledgeBaseTools.ListKnowledgeBaseTags", engines: ["5.9"] },
    ...bp("read", "[Epic KnowledgeBaseToolset.KnowledgeBaseTools] Lists the tags a base's entries carry, most common first, so a Tags filter is spelled from what exists rather than guessed. Tags are exact strings: \"simulation\" does not match \"Simulation\". Raises an error if the alias is not registered or the base fails to build. Available on UE 5.9; absent from UE 5.8. Params: alias, prefix, limit?", "epic_call_tool"),
  },
  epic_list_knowledge_bases: {
    epicSchema: S_epic_list_knowledge_bases,
    epicTool: { toolset: "KnowledgeBaseToolset.KnowledgeBaseTools", name: "KnowledgeBaseToolset.KnowledgeBaseTools.ListKnowledgeBases", engines: ["5.9"] },
    ...bp("read", "[Epic KnowledgeBaseToolset.KnowledgeBaseTools] Lists every registered knowledge base. Call this first: aliases are the handle every other tool takes and they vary by project. Choose a base by its Description. A base with a \"vocabulary\" consumer is the catalogue that tool's input is spelled from, so query it before calling that tool. Builds nothing. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_skills: {
    epicSchema: S_epic_list_skills,
    epicTool: { toolset: "ToolsetRegistry.AgentSkillToolset", name: "ToolsetRegistry.AgentSkillToolset.ListSkills", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic ToolsetRegistry.AgentSkillToolset] Gets a summary of all AgentSkills in the project. Params: none", "epic_call_tool"),
  },
  epic_query_knowledge_base: {
    epicSchema: S_epic_query_knowledge_base,
    epicTool: { toolset: "KnowledgeBaseToolset.KnowledgeBaseTools", name: "KnowledgeBaseToolset.KnowledgeBaseTools.QueryKnowledgeBase", engines: ["5.9"] },
    ...bp("read", "[Epic KnowledgeBaseToolset.KnowledgeBaseTools] Searches one base. Text is matched as plain words and ranked; Tags keep only entries carrying all of them; both empty lists the whole base. Bodies are truncated; GetKnowledgeEntry returns a hit's full payload. Raises an error if the alias is not registered or the base fails to build. Available on UE 5.9; absent from UE 5.8. Params: alias, text, tags, limit?", "epic_call_tool"),
  },
  epic_update_skill: {
    epicSchema: S_epic_update_skill,
    epicTool: { toolset: "ToolsetRegistry.AgentSkillToolset", name: "ToolsetRegistry.AgentSkillToolset.UpdateSkill", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic ToolsetRegistry.AgentSkillToolset] Updates an existing AgentSkill. This should ONLY be called after getting explicit direction or permission from the user. Params: skillPath, description, details", "epic_call_tool"),
  },
  epic_write_knowledge: {
    epicSchema: S_epic_write_knowledge,
    epicTool: { toolset: "KnowledgeBaseToolset.KnowledgeBaseTools", name: "KnowledgeBaseToolset.KnowledgeBaseTools.WriteKnowledge", engines: ["5.9"] },
    ...bp("mutate", "[Epic KnowledgeBaseToolset.KnowledgeBaseTools] Writes a structured entry into a markdown base as a JSON block, preserving the prose and other blocks in the file. An existing EntryId is replaced in the file that holds it, wherever that is; an empty EntryId appends a new entry to the file FileId names, creating the file if absent. Reuse the stem of a hit's Source as FileId so a topic accumulates rather than sprawls. Only markdown bases are writable; check bWritable in ListKnowledgeBases first. Raises an error if the alias is not registered, the base is read-only, BodyJson is not a JSON object, or the file cannot be written. Available on UE 5.9; absent from UE 5.8. Params: alias, fileId, summary, tags, bodyJson, entryId", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  alias: z.string().optional().describe("A short identifier for the base, e.g. \"rigging\". Lowercase, no spaces."),
  assetName: z.string().optional().describe("The name of the skill to create i.e. MySkill."),
  bodyJson: z.string().optional().describe("The entry payload as a JSON object. Stored verbatim when it carries no \"_parent\" member; one at the end is cut out, one anywhere else is removed by reserialisi…"),
  description: z.string().optional().describe("The brief description of the skill."),
  details: z.record(z.string(), z.unknown()).optional().describe("Detailed information about how to use the skill."),
  entryId: z.string().optional().describe("The entry id, exactly as a hit reported it."),
  fileId: z.string().optional().describe("The file to add a new entry to, without extension, e.g. \"ik-setup\" or \"limbs/ik-setup\". Ignored when EntryId already exists."),
  folderPath: z.string().optional().describe("The folder in which to create the skill. i.e. /Game/Skills/."),
  limit: z.number().optional().describe("Maximum tags to return, or zero for all of them."),
  prefix: z.string().optional().describe("Only tags starting with this, e.g. \"layer:\" for one family; empty for every tag."),
  relativePath: z.string().optional().describe("Where under Content/KnowledgeBases the base lives, e.g. \"Rigging\" or \"Rigging/Conventions\". No absolute paths, no \"..\"."),
  script: z.string().optional(),
  skillPath: z.string().optional().describe("The full path to the skill to modify i.e. /Game/Skills/MySkill.MySkill_C."),
  skillPaths: z.array(z.unknown()).optional().describe("A list of paths to the AgentSkills to retrieve."),
  summary: z.string().optional().describe("One line describing the entry. This is what search ranks and returns."),
  tags: z.array(z.unknown()).optional().describe("Tags an entry must all carry, spelled as ListKnowledgeBaseTags reports them; empty for no filter."),
  text: z.string().optional().describe("Words to match, or empty to filter by tags alone."),
};
