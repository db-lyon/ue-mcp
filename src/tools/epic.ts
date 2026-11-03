import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { actions as epicActions, schema as epicSchema } from "./epic/epic.generated.js";
import { specBp, schema as specSchema } from "./specs/epic.generated.js";

// Wraps Epic's native UE 5.8 AI Toolset Registry (the plugin behind Unreal's
// experimental MCP server) as first-class ue-mcp actions. The bridge reaches the
// registry in-process by reflection, so we ride Epic's maintained engine<>tool
// boundary instead of competing with it: every toolset they ship is callable
// here, and ue-mcp's own value (flows, plugin ecosystem, in-editor suite)
// composes on top. Mirrors Epic's own discover-then-call pattern so the surface
// stays tiny no matter how many toolsets ship (5.8 registers 50+).
export const epicTool: ToolDef = categoryTool(
  "epic",
  "The Unreal 5.8+ AI Toolset Registry itself: discovery (status/list_toolsets/describe_toolset), direct dispatch (call_tool), and the registry's own meta-tooling (agent skills, programmatic tool batching). Also Epic's Unreal MCP plugin's own tools and resources (list_mcp_tools/call_mcp_tool, list_mcp_resources/read_mcp_resource). Toolsets that map to a real editor domain are NOT here - unless nativeTools withholds them, they appear as epic_* actions on that domain's category (GAS in gas, Sequencer in animation, Dataflow in dataflow, and so on), so reach for the domain tool first and use this one to introspect or call the registry directly. Requires UE 5.8+ with the ToolsetRegistry plugin enabled - call epic(status) first to check availability.",
  {
    status:           specBp("read", "Report whether Epic's ToolsetRegistry is available and how many toolsets are registered. Never errors (reports available=false with a reason when the plugin is absent).", "epic_status"),
    list_toolsets:    specBp("read", "List registered toolsets: name, version, description, tool names + count. Strips the verbose per-tool input/output schemas to stay small - use describe_toolset for those (or includeSchemas).", "epic_list_toolsets"),
    describe_toolset: specBp("read", "Full schema for one toolset: every tool with its input/output JSON schema.", "epic_describe_toolset"),
    call_tool:        specBp("unknown", "Execute a registered Epic tool exactly as its MCP server would. tool takes the qualified name from describe_toolset, e.g. GASToolsets.AttributeSetToolset.ListAttributeSets; pass the arguments as input, or as inputJson to send raw JSON. Returns the tool's JSON result; images its output schema marks (FToolsetImage) come back as MCP image content. A tool that answers later reports progress while it runs.", "epic_call_tool"),
    list_mcp_tools:   specBp("read", "List the tools Epic's Unreal MCP plugin serves beside the registry: legacy MCP Tool Library Blueprints (source toolLibrary) and tools other plugins register (source plugin). Its adapters onto the registry are counted, not listed, since list_toolsets covers them; includeRegistryAdapters lists them too. available=false and no tools when the plugin is not enabled.", "epic_list_mcp_tools"),
    call_mcp_tool:    specBp("unknown", "Run a tool from list_mcp_tools exactly as Epic's MCP server would: input as an object, or inputJson. Text and structuredContent come back as JSON, images and audio as MCP content. A long tool reports progress, and cancelling the call reaches the tool's CancelAsync.", "epic_call_mcp_tool"),
    list_mcp_resources: specBp("read", "List the resources registered with Epic's Unreal MCP plugin: uri, name, title, description, mimeType. Empty when no provider is registered or the plugin is off. This server also serves them as MCP resources.", "epic_list_mcp_resources"),
    read_mcp_resource:  specBp("read", "Read one resource from list_mcp_resources by uri: contents with its text, or its blob as base64.", "epic_read_mcp_resource"),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key the discovery handlers and call_tool declare, generated
    // from their C++ registrations. The epic_* actions in other categories
    // dispatch to call_tool too; each builds its bag from its wrapped tool's
    // recorded input schema.
    ...specSchema,
  },
);
