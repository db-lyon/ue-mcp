/**
 * The parts project(get_status) is read from, each a task the registry holds
 * as `ue-mcp.status/<part>`. The action runs them as its children and reduces
 * what they answer into its response.
 */
import { checkBridgeParity } from "../../bridge/bridge-parity.js";
import { connectedEditorOf } from "../../editor/editor-control.js";
import { detectProjectHolders } from "../../editor/project-holders.js";
import { toolGraphOf } from "../../surface/target-params.js";
import type { ToolContext } from "../../core/types.js";

export const STATUS_PART_PREFIX = "ue-mcp.status/";

export const STATUS_PARTS: Readonly<Record<string, (ctx: ToolContext) => unknown>> = {
  // Pre-built sequences for this project.
  flows: (ctx) => ctx.getFlows?.() ?? [],
  // What the connected plugin said it was at handshake (#821).
  capabilities: (ctx) => ctx.bridge.capabilities,
  // The advertised surface against the methods the running binary registered (#1021).
  parity: (ctx) => checkBridgeParity(toolGraphOf(ctx), ctx.bridge.capabilities),
  // Several editor processes holding the project, naming the one answering (#1150).
  holders: (ctx) => detectProjectHolders(ctx.project.projectDir, ctx.project.projectPath, connectedEditorOf(ctx.bridge)?.pid ?? null),
};
