// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd epic handler. */
export const handlerSpecs: HandlerSpecs = {
  "epic_call_mcp_tool": {
    "category": "epic",
    "params": [
      {
        "name": "tool",
        "type": "string",
        "required": true,
        "description": "Tool name from epic_list_mcp_tools"
      },
      {
        "name": "input",
        "type": "object",
        "required": false,
        "description": "Tool arguments as a JSON object"
      },
      {
        "name": "inputJson",
        "type": "string",
        "required": false,
        "description": "Tool arguments as a raw JSON string; wins over input"
      }
    ],
    "contractExempt": "runs a registered engine tool"
  },
  "epic_call_tool": {
    "category": "epic",
    "params": [
      {
        "name": "toolset",
        "type": "string",
        "required": true,
        "description": "Qualified toolset name from epic_list_toolsets, e.g. GASToolsets.AttributeSetToolset"
      },
      {
        "name": "tool",
        "type": "string",
        "required": true,
        "description": "Tool name from describe_toolset, bare or qualified with its toolset"
      },
      {
        "name": "input",
        "type": "object",
        "required": false,
        "description": "Tool arguments as a JSON object"
      },
      {
        "name": "inputJson",
        "type": "string",
        "required": false,
        "description": "Tool arguments as a raw JSON string; wins over input"
      }
    ],
    "contractExempt": "runs a registered engine tool"
  },
  "epic_describe_toolset": {
    "category": "epic",
    "params": [
      {
        "name": "toolset",
        "type": "string",
        "required": true,
        "description": "Qualified toolset name from epic_list_toolsets, e.g. GASToolsets.AttributeSetToolset"
      }
    ]
  },
  "epic_list_mcp_resources": {
    "category": "epic",
    "params": []
  },
  "epic_list_mcp_tools": {
    "category": "epic",
    "params": [
      {
        "name": "nameFilter",
        "type": "string",
        "required": false,
        "description": "Case-sensitive substring of the tool name"
      },
      {
        "name": "includeSchemas",
        "type": "boolean",
        "required": false,
        "description": "Return each tool's input and output schemas (default false)"
      },
      {
        "name": "includeRegistryAdapters",
        "type": "boolean",
        "required": false,
        "description": "Also list the tools that only adapt ToolsetRegistry tools, which epic(list_toolsets) already covers (default false)"
      }
    ]
  },
  "epic_list_toolsets": {
    "category": "epic",
    "params": [
      {
        "name": "nameFilter",
        "type": "string",
        "required": false,
        "description": "Case-sensitive substring of the qualified toolset name"
      },
      {
        "name": "includeSchemas",
        "type": "boolean",
        "required": false,
        "description": "Return each tool with its input and output schemas instead of its name (default false)"
      }
    ]
  },
  "epic_read_mcp_resource": {
    "category": "epic",
    "params": [
      {
        "name": "uri",
        "type": "string",
        "required": true,
        "description": "Resource uri from epic_list_mcp_resources"
      }
    ]
  },
  "epic_status": {
    "category": "epic",
    "params": []
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  epic_call_mcp_tool: "Params: tool, input?, inputJson?",
  epic_call_tool: "Params: toolset, tool, input?, inputJson?",
  epic_describe_toolset: "Params: toolset",
  epic_list_mcp_resources: "Params: none",
  epic_list_mcp_tools: "Params: nameFilter?, includeSchemas?, includeRegistryAdapters?",
  epic_list_toolsets: "Params: nameFilter?, includeSchemas?",
  epic_read_mcp_resource: "Params: uri",
  epic_status: "Params: none",
};

/** Every key the spec'd epic handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  includeRegistryAdapters: z.boolean().optional().describe("Also list the tools that only adapt ToolsetRegistry tools, which epic(list_toolsets) already covers (default false)"),
  includeSchemas: z.boolean().optional().describe("Return each tool's input and output schemas (default false) (epic_list_mcp_tools). Return each tool with its input and output schemas instead of its name (default false) (epic_list_toolsets)"),
  input: z.record(z.string(), z.unknown()).optional().describe("Tool arguments as a JSON object"),
  inputJson: z.string().optional().describe("Tool arguments as a raw JSON string; wins over input"),
  nameFilter: z.string().optional().describe("Case-sensitive substring of the tool name (epic_list_mcp_tools). Case-sensitive substring of the qualified toolset name (epic_list_toolsets)"),
  tool: z.string().optional().describe("Tool name from epic_list_mcp_tools (epic_call_mcp_tool). Tool name from describe_toolset, bare or qualified with its toolset (epic_call_tool)"),
  toolset: z.string().optional().describe("Qualified toolset name from epic_list_toolsets, e.g. GASToolsets.AttributeSetToolset"),
  uri: z.string().optional().describe("Resource uri from epic_list_mcp_resources"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
