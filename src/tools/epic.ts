import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { actions as epicActions, schema as epicSchema } from "./epic/epic.generated.js";
import { specBp, schema as specSchema } from "./specs/epic.generated.js";
import { waitForAssistant } from "./ai-assistant-wait.js";

// Wraps Epic's native AI Toolset Registry (UE 5.8+) (the plugin behind Unreal's
// experimental MCP server) as first-class ue-mcp actions. The bridge reaches the
// registry in-process by reflection, so we ride Epic's maintained engine<>tool
// boundary instead of competing with it: every toolset they ship is callable
// here, and ue-mcp's own value (flows, plugin ecosystem, in-editor suite)
// composes on top. Mirrors Epic's own discover-then-call pattern so the surface
// stays tiny no matter how many toolsets ship (50+ since 5.8).
export const epicTool: ToolDef = categoryTool(
  "epic",
  "Unreal's AI Toolset Registry itself: discovery (status/list_toolsets/describe_toolset), turning toolsets off and on (get_toolset_states/set_toolset_enabled), direct dispatch (call_tool), and the registry's own meta-tooling (agent skills, programmatic tool batching). Also Epic's Unreal MCP plugin's own tools and resources (list_mcp_tools/call_mcp_tool, list_mcp_resources/read_mcp_resource). Toolsets that map to a real editor domain are NOT here - unless nativeTools withholds them, they appear as epic_* actions on that domain's category (GAS in gas, Sequencer in animation, Dataflow in dataflow, and so on), so reach for the domain tool first and use this one to introspect or call the registry directly. Requires UE 5.8+ with the ToolsetRegistry plugin enabled - call epic(status) first to check availability. The ai_assistant_* actions drive Epic's in-editor AI Assistant (UE 5.9+ with the AI Assistant plugin): send a message and get the reply, read a conversation, and read its update events; check ai_assistant_status first.",
  {
    status:           specBp("read", "Report whether Epic's ToolsetRegistry is available and how many toolsets are registered. Never errors (reports available=false with a reason when the plugin is absent).", "epic_status"),
    list_toolsets:    specBp("read", "List registered toolsets: name, version, description, tool names + count. Strips the verbose per-tool input/output schemas to stay small - use describe_toolset for those (or includeSchemas).", "epic_list_toolsets"),
    describe_toolset: specBp("read", "Full schema for one toolset: every tool with its input/output JSON schema.", "epic_describe_toolset"),
    get_toolset_states: specBp("read", "Every registered toolset with its enabled state, including the disabled ones list_toolsets leaves out: for each, the block and allow patterns that match its name (blockedBy, allowedBy), outsideAllowList, disabledBy (blockList, allowList or noVisibleTools) and toggledHere when set_toolset_enabled wrote its pattern. Also the registry's live blockedNames and allowedNames, the settings' copies, and the config file they are saved to. UE 5.8+ with the ToolsetRegistry plugin, Win64 editor.", "epic_get_toolset_states"),
    set_toolset_enabled: specBp("mutate", "Turn registered toolsets off or on by exact name, effective at once: a disabled toolset leaves list_toolsets and its epic_* actions, and call_tool refuses its tools. Off adds the pattern /^\\Qname\\E$/ to the registry's block list; on removes it, and adds the same pattern to a non-empty allow list that does not match. Applied to the live registry and saved to UToolsetRegistrySettings (Config/DefaultGame.ini on 5.9, DefaultEditorPerProjectUserSettings.ini on 5.8), so the next session starts the same way. Unknown names are refused with validNames; a toolset blocked by a pattern this action did not write is refused rather than edited. A toolset already in the asked state reports unchanged. Rollback restores the previous state.", "epic_set_toolset_enabled"),
    call_tool:        specBp("unknown", "Execute a registered Epic tool exactly as its MCP server would. tool takes the qualified name from describe_toolset, e.g. GASToolsets.AttributeSetToolset.ListAttributeSets; pass the arguments as input, or as inputJson to send raw JSON. Returns the tool's JSON result; images its output schema marks (FToolsetImage) come back as MCP image content. A tool that answers later reports progress while it runs.", "epic_call_tool"),
    list_mcp_tools:   specBp("read", "List the tools Epic's Unreal MCP plugin serves beside the registry: legacy MCP Tool Library Blueprints (source toolLibrary) and tools other plugins register (source plugin). Its adapters onto the registry are counted, not listed, since list_toolsets covers them; includeRegistryAdapters lists them too. available=false and no tools when the plugin is not enabled.", "epic_list_mcp_tools"),
    call_mcp_tool:    specBp("unknown", "Run a tool from list_mcp_tools exactly as Epic's MCP server would: input as an object, or inputJson. Text and structuredContent come back as JSON, images and audio as MCP content. A long tool reports progress, and cancelling the call reaches the tool's CancelAsync.", "epic_call_mcp_tool"),
    list_mcp_resources: specBp("read", "List the resources registered with Epic's Unreal MCP plugin: uri, name, title, description, mimeType. Empty when no provider is registered or the plugin is off. This server also serves them as MCP resources.", "epic_list_mcp_resources"),
    read_mcp_resource:  specBp("read", "Read one resource from list_mcp_resources by uri: contents with its text, or its blob as base64.", "epic_read_mcp_resource"),
    ai_assistant_status: specBp("read", "Report whether Epic's AI Assistant API (UAIAssistantInstance, UE 5.9+ with the experimental AI Assistant plugin) is loaded: engineVersion, available, and when it is not, errorCode unsupported_engine_version or PLUGIN_NOT_AVAILABLE with the reason. When available, lists its UFUNCTIONs, whether the singleton exists, whether conversation updates are being recorded, and pendingRequests. Never errors.", "epic_ai_assistant_status"),
    ai_assistant_send_message: {
      ...specBp("mutate", "Send a user message to Epic's AI Assistant, as UAIAssistantInstance::SendTextMessageAndGetReply (reply=true, default) or SendTextMessage (reply=false). Empty conversationId starts a new conversation. Opens the assistant panel, which needs the editor's Epic sign-in to answer. Waits (default 300s) and returns state complete with reply (and conversationId: the one the reply names, or on builds whose reply is bare text the first update event's, when none was given) or, with reply=false, the conversationId the message was bound to; wait=false returns state pending and a requestId for get_request. A timeout fails with state timed_out and keeps the requestId. Engines before 5.9 return unsupported_engine_version.", "epic_ai_assistant_send_message"),
      followUp: waitForAssistant(300),
    },
    ai_assistant_get_conversation: {
      ...specBp("read", "Read an AI Assistant conversation as Epic serializes it (UAIAssistantInstance::GetConversationJson): conversation is the parsed JSON (conversationJson when it does not parse). Empty conversationId reads the active conversation. Does not open the assistant panel, and fails while it is closed (send_message opens it). Waits like send_message (default 60s).", "epic_ai_assistant_get_conversation"),
      followUp: waitForAssistant(60),
    },
    ai_assistant_get_request: specBp("read", "One poll of an AI Assistant request that send_message or get_conversation opened: state pending, complete (with its reply, conversationId or conversation) or failed (errorCode ai_assistant_failed, assistantError). A complete or failed answer releases the request; an unknown or released requestId returns ai_assistant_request_not_found.", "epic_ai_assistant_get_request"),
    ai_assistant_get_events: specBp("read", "Conversation updates UAIAssistantInstance::OnConversationUpdated reported (Started, MessagesUpdated, Complete, Stopped), each with seq, conversationId and utc, oldest first; pass the last nextSeq as sinceSeq to read only newer ones. Recording starts once the assistant instance exists (after a send_message or get_conversation, or the panel's own use); listening=false says it has not started. Does not open the panel.", "epic_ai_assistant_get_events"),
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
