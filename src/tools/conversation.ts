import { z } from "zod";
import { categoryTool, type ToolDef } from "../types.js";
import { actions as epicActions, schema as epicSchema } from "./epic/conversation.generated.js";

/**
 * Conversation graphs, contributed entirely by Unreal's own toolsets.
 *
 * Same shape as `dataflow`: no native handlers behind it, every action a
 * generated declaration, and the category declared rather than conjured at
 * startup so it is in ALL_TOOLS, the golden baseline and the parameter audits
 * like anything else.
 */
export const conversationTool: ToolDef = categoryTool(
  "conversation",
  "Conversation graphs (UConversationDatabase): dialogue nodes, node connections, sub-nodes, speakers, and entry points. Requires UE 5.8+ with the Conversation toolsets available.",
  { ...epicActions },
  undefined,
  { ...epicSchema },
);
