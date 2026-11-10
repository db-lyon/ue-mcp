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
  "epic_ai_assistant_get_conversation": {
    "category": "epic",
    "params": [
      {
        "name": "conversationId",
        "type": "string",
        "required": false,
        "description": "Conversation to use; empty or omitted starts a new one (send_message) or reads the active one (get_conversation)"
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Return only once the result is complete (default true); false returns the requestId to poll with get_request"
      },
      {
        "name": "timeoutSeconds",
        "type": "number",
        "required": false,
        "description": "Longest wait in seconds (send_message default 300, get_conversation default 60)"
      }
    ],
    "contractExempt": "opens the AI Assistant panel"
  },
  "epic_ai_assistant_get_events": {
    "category": "epic",
    "params": [
      {
        "name": "sinceSeq",
        "type": "integer",
        "required": false,
        "description": "Return only updates after this seq (default 0, every recorded update)"
      },
      {
        "name": "conversationId",
        "type": "string",
        "required": false,
        "description": "Return only this conversation's updates"
      }
    ]
  },
  "epic_ai_assistant_get_request": {
    "category": "epic",
    "params": [
      {
        "name": "requestId",
        "type": "integer",
        "required": true,
        "description": "requestId from send_message or get_conversation"
      }
    ]
  },
  "epic_ai_assistant_send_message": {
    "category": "epic",
    "params": [
      {
        "name": "message",
        "type": "string",
        "required": true,
        "description": "The user-visible prompt text"
      },
      {
        "name": "conversationId",
        "type": "string",
        "required": false,
        "description": "Conversation to use; empty or omitted starts a new one (send_message) or reads the active one (get_conversation)"
      },
      {
        "name": "hiddenContext",
        "type": "string",
        "required": false,
        "description": "Context appended to the message but not shown in the panel"
      },
      {
        "name": "reply",
        "type": "boolean",
        "required": false,
        "description": "Resolve with the assistant's reply text (default true); false resolves as soon as the message is bound to a conversation, with its id"
      },
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Return only once the result is complete (default true); false returns the requestId to poll with get_request"
      },
      {
        "name": "timeoutSeconds",
        "type": "number",
        "required": false,
        "description": "Longest wait in seconds (send_message default 300, get_conversation default 60)"
      }
    ],
    "contractExempt": "opens the AI Assistant panel and sends a message to its backend"
  },
  "epic_ai_assistant_status": {
    "category": "epic",
    "params": []
  },
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
  "epic_get_toolset_states": {
    "category": "epic",
    "params": [
      {
        "name": "nameFilter",
        "type": "string",
        "required": false,
        "description": "Case-sensitive substring of the qualified toolset name"
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
  "epic_set_toolset_enabled": {
    "category": "epic",
    "params": [
      {
        "name": "toolsets",
        "type": "array",
        "required": true,
        "description": "Exact qualified toolset names from epic_get_toolset_states, e.g. GASToolsets.AttributeSetToolset",
        "items": "string"
      },
      {
        "name": "enabled",
        "type": "boolean",
        "required": true,
        "description": "true turns the toolsets on, false turns them off"
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
  epic_ai_assistant_get_conversation: "Params: conversationId?, wait?, timeoutSeconds?",
  epic_ai_assistant_get_events: "Params: sinceSeq?, conversationId?",
  epic_ai_assistant_get_request: "Params: requestId",
  epic_ai_assistant_send_message: "Params: message, conversationId?, hiddenContext?, reply?, wait?, timeoutSeconds?",
  epic_ai_assistant_status: "Params: none",
  epic_call_mcp_tool: "Params: tool, input?, inputJson?",
  epic_call_tool: "Params: toolset, tool, input?, inputJson?",
  epic_describe_toolset: "Params: toolset",
  epic_get_toolset_states: "Params: nameFilter?",
  epic_list_mcp_resources: "Params: none",
  epic_list_mcp_tools: "Params: nameFilter?, includeSchemas?, includeRegistryAdapters?",
  epic_list_toolsets: "Params: nameFilter?, includeSchemas?",
  epic_read_mcp_resource: "Params: uri",
  epic_set_toolset_enabled: "Params: toolsets, enabled",
  epic_status: "Params: none",
};

/** Every key the spec'd epic handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  conversationId: z.string().optional().describe("Conversation to use; empty or omitted starts a new one (send_message) or reads the active one (get_conversation) (epic_ai_assistant_get_conversation, epic_ai_assistant_send_message). Return only this conversation's updates (epic_ai_assistant_get_events)"),
  enabled: z.boolean().optional().describe("true turns the toolsets on, false turns them off"),
  hiddenContext: z.string().optional().describe("Context appended to the message but not shown in the panel"),
  includeRegistryAdapters: z.boolean().optional().describe("Also list the tools that only adapt ToolsetRegistry tools, which epic(list_toolsets) already covers (default false)"),
  includeSchemas: z.boolean().optional().describe("Return each tool's input and output schemas (default false) (epic_list_mcp_tools). Return each tool with its input and output schemas instead of its name (default false) (epic_list_toolsets)"),
  input: z.record(z.string(), z.unknown()).optional().describe("Tool arguments as a JSON object"),
  inputJson: z.string().optional().describe("Tool arguments as a raw JSON string; wins over input"),
  message: z.string().optional().describe("The user-visible prompt text"),
  nameFilter: z.string().optional().describe("Case-sensitive substring of the qualified toolset name (epic_get_toolset_states, epic_list_toolsets). Case-sensitive substring of the tool name (epic_list_mcp_tools)"),
  reply: z.boolean().optional().describe("Resolve with the assistant's reply text (default true); false resolves as soon as the message is bound to a conversation, with its id"),
  requestId: z.number().int().optional().describe("requestId from send_message or get_conversation"),
  sinceSeq: z.number().int().optional().describe("Return only updates after this seq (default 0, every recorded update)"),
  timeoutSeconds: z.number().optional().describe("Longest wait in seconds (send_message default 300, get_conversation default 60)"),
  tool: z.string().optional().describe("Tool name from epic_list_mcp_tools (epic_call_mcp_tool). Tool name from describe_toolset, bare or qualified with its toolset (epic_call_tool)"),
  toolset: z.string().optional().describe("Qualified toolset name from epic_list_toolsets, e.g. GASToolsets.AttributeSetToolset"),
  toolsets: z.array(z.string()).optional().describe("Exact qualified toolset names from epic_get_toolset_states, e.g. GASToolsets.AttributeSetToolset"),
  uri: z.string().optional().describe("Resource uri from epic_list_mcp_resources"),
  wait: z.boolean().optional().describe("Return only once the result is complete (default true); false returns the requestId to poll with get_request"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
