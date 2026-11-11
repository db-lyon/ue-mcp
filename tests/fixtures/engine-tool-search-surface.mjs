// The startup surface of the Unreal Engine 5.9 ModelContextProtocol plugin
// with tool search on (its default): what `initialize` and `tools/list` hand a
// client. Reconstructed from engine source at ue5-main 2fad1ef4a1de:
//   Engine/Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocolEditor/Private/ModelContextProtocolToolSearch.{h,cpp}
//     the three tools, their descriptions and input schemas, in registration order
//   Engine/Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocol/Private/ModelContextProtocolServer.cpp
//     tools/list writes name, description, inputSchema; initialize never sets `instructions`
// The default strategy's seed must stay at or below this (scripts/context-tax.mjs).

export const ENGINE_TOOL_SEARCH_SOURCE = "UE 5.9 ModelContextProtocol tool search";

export const ENGINE_TOOL_SEARCH_INSTRUCTIONS = "";

export const ENGINE_TOOL_SEARCH_TOOLS = [
  {
    name: "list_toolsets",
    description: "List all available toolsets with names and descriptions.",
    inputSchema: { type: "object", properties: {} },
  },
  {
    name: "describe_toolset",
    description: "Get detailed information about a toolset including all tool names, descriptions, and input schemas.",
    inputSchema: {
      type: "object",
      properties: {
        toolset_name: { type: "string", description: "Name of the toolset to describe. Use list_toolsets to see available names." },
      },
      required: ["toolset_name"],
    },
  },
  {
    name: "call_tool",
    description:
      "Call a tool by name. Provide toolset_name to call a toolset tool, or omit it to call a top-level MCP tool. "
      + "Use list_toolsets and describe_toolset to discover available tools and their input schemas.",
    inputSchema: {
      type: "object",
      properties: {
        toolset_name: {
          type: "string",
          description: "Optional. Name of the toolset containing the tool. Omit to call a top-level MCP tool. Use list_toolsets to discover toolset names.",
        },
        tool_name: {
          type: "string",
          description: "Name of the tool to call, without toolset prefix. Use describe_toolset to discover tool names and input schemas.",
        },
        arguments: {
          type: "object",
          description: "Arguments to pass to the tool. Must match the tool's input schema. Defaults to an empty object.",
        },
      },
      required: ["tool_name"],
    },
  },
];
