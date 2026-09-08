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

const S_epic_get_all_nodes = {"properties":{"conversation":{"type":"object","properties":{"refPath":{}}}},"required":["conversation"]} as const;
const S_epic_get_node_by_guid = {"properties":{"conversation":{"type":"object","properties":{"refPath":{}}},"guid":{"type":"string"}},"required":["conversation","guid"]} as const;
const S_epic_get_node_connections = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_get_node_guids = {"properties":{"conversation":{"type":"object","properties":{"refPath":{}}}},"required":["conversation"]} as const;
const S_epic_get_sub_nodes = {"properties":{"node":{"type":"object","properties":{"refPath":{}}}},"required":["node"]} as const;
const S_epic_list_entry_points = {"properties":{"conversation":{"type":"object","properties":{"refPath":{}}}},"required":["conversation"]} as const;
const S_epic_list_speakers = {"properties":{"conversation":{"type":"object","properties":{"refPath":{}}}},"required":["conversation"]} as const;

/** 7 wrapped engine tools routed to the `conversation` category. */
export const actions: Record<string, ActionSpec> = {
  epic_get_all_nodes: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns all reachable nodes in the conversation. Use ObjectTools.get_class and get_properties on each node to inspect type and properties. Each node's GUID is available via its compiled_node_guid attribute. Params: conversation",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.get_all_nodes", S_epic_get_all_nodes, p),
  ),
  epic_get_node_by_guid: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns a conversation node by its GUID. Params: conversation, guid",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.get_node_by_guid", S_epic_get_node_by_guid, p),
  ),
  epic_get_node_connections: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns output connection GUIDs for a conversation node. Params: node",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.get_node_connections", S_epic_get_node_connections, p),
  ),
  epic_get_node_guids: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns GUIDs of all reachable nodes, in map iteration order. Use with get_node_by_guid to look up specific nodes. Params: conversation",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.get_node_guids", S_epic_get_node_guids, p),
  ),
  epic_get_sub_nodes: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns sub-nodes (requirements, choices) attached to a task node. Params: node",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.get_sub_nodes", S_epic_get_sub_nodes, p),
  ),
  epic_list_entry_points: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns entry points (FConversationEntryList structs). Params: conversation",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.list_entry_points", S_epic_list_entry_points, p),
  ),
  epic_list_speakers: bp(
    "read",
    "[Epic conversation_toolset.toolsets.conversation.ConversationTools] Returns speaker/participant information. Params: conversation",
    "epic_call_tool",
    (p) => epicToolCall("conversation_toolset.toolsets.conversation.ConversationTools", "conversation_toolset.toolsets.conversation.ConversationTools.list_speakers", S_epic_list_speakers, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  conversation: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  guid: z.string().optional().describe("Globally unique identifier in 8-4-4-4-12 hyphenated form, e.g. \"E05FCC13-4D37-9D7A-E238-83859F29AD74\"."),
  node: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
};
