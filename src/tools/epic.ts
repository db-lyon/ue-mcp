import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { actions as epicActions, schema as epicSchema } from "./epic/epic.generated.js";

// Wraps Epic's native UE 5.8 AI Toolset Registry (the plugin behind Unreal's
// experimental MCP server) as first-class ue-mcp actions. The bridge reaches the
// registry in-process by reflection, so we ride Epic's maintained engine<>tool
// boundary instead of competing with it: every toolset they ship is callable
// here, and ue-mcp's own value (flows, plugin ecosystem, in-editor suite)
// composes on top. Mirrors Epic's own discover-then-call pattern so the surface
// stays tiny no matter how many toolsets ship (5.8 registers 50+).
export const epicTool: ToolDef = categoryTool(
  "epic",
  "The Unreal 5.8 AI Toolset Registry itself: discovery (status/list_toolsets/describe_toolset), direct dispatch (call_tool), and the registry's own meta-tooling (agent skills, programmatic tool batching). Toolsets that map to a real editor domain are NOT here - they are injected as epic_* actions on that domain's category (GAS in gas, Sequencer in animation, Dataflow in dataflow, and so on), so reach for the domain tool first and use this one to introspect or call the registry directly. Requires UE 5.8+ with the ToolsetRegistry plugin enabled - call epic(status) first to check availability.",
  {
    status:           bp("read", "Report whether Epic's ToolsetRegistry is available and how many toolsets are registered. Never errors (reports available=false with a reason when the plugin is absent). Params: none", "epic_status"),
    list_toolsets:    bp("read", "List registered toolsets: name, version, description, tool names + count. Strips the verbose per-tool input/output schemas to stay small - use describe_toolset for those (or includeSchemas). Params: nameFilter? (case-sensitive substring on the qualified name), includeSchemas? (return full tool objects with input/output schemas)", "epic_list_toolsets", (p) => ({ nameFilter: p.nameFilter, includeSchemas: p.includeSchemas })),
    describe_toolset: bp("read", "Full schema for one toolset: every tool with its input/output JSON schema. Params: toolset (qualified name from list_toolsets, e.g. 'GASToolsets.AttributeSetToolset')", "epic_describe_toolset", (p) => ({ toolset: p.toolset })),
    call_tool:        bp("unknown", "Execute a registered Epic tool exactly as its MCP server would. Params: toolset (qualified), tool (qualified name from describe_toolset, e.g. 'GASToolsets.AttributeSetToolset.ListAttributeSets'), input? (object) or inputJson? (raw JSON string). Returns the tool's JSON result.", "epic_call_tool", (p) => ({ toolset: p.toolset, tool: p.tool, input: p.input, inputJson: p.inputJson })),
    ...epicActions,
  },
  undefined,
  {
    ...epicSchema,
    nameFilter: z.string().optional().describe("list_toolsets: case-sensitive substring filter on the qualified toolset name"),
    includeSchemas: z.boolean().optional().describe("list_toolsets: include full per-tool input/output schemas instead of just tool names"),
    toolset: z.string().optional().describe("Qualified toolset name, e.g. 'GASToolsets.AttributeSetToolset'"),
    tool: z.string().optional().describe("Qualified tool name, e.g. 'GASToolsets.AttributeSetToolset.ListAttributeSets'"),
    input: z.record(z.unknown()).optional().describe("call_tool: tool arguments as a JSON object"),
    inputJson: z.string().optional().describe("call_tool: tool arguments as a raw JSON string (alternative to input)"),
  },
);
