/**
 * One MCP server over a runtime: the initialize instructions, every category
 * tool and the flow tool, each dispatched through the runtime's sessions.
 *
 * Built once per MCP client. A stdio process builds one; the daemon builds one
 * per connection, all sharing the runtime's bridges.
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { z } from "zod";
import { composeServerInstructions } from "../surface/context/instructions.js";
import { fullSurfaceDescription } from "../surface/context/lean-context.js";
import { envelopeInputSchema, envelopeShape, flatInputSchema, strippingInputSchema, usesArgsEnvelope } from "../surface/context/call-envelope.js";
import type { ToolDef } from "../core/types.js";
import type { ContextStrategy } from "../surface/context/lean-context.js";
import { withholdFromTool } from "../surface/registered-surface.js";
import { packageVersion } from "../core/package-root.js";
import { upgradeNoticeForClient } from "../core/version-check.js";
import {
  buildElicit,
  dispatchCategoryCall,
  dispatchFlowCall,
  type CallExtra,
  type DispatchDeps,
  type ToolResult,
} from "../dispatch/server-dispatch.js";
import type { Runtime } from "./runtime.js";
import type { SessionAccess } from "../flow/session-access.js";
import { announceResourceChange, registerEditorResources } from "./editor-resources.js";

export interface RuntimeServer {
  server: McpServer;
  /** Stop following the runtime's surface changes. */
  dispose(): void;
}

/**
 * How one tool is shown in tools/list: its description and input schema once
 * the actions no running plugin has are withheld (6.4), or null when nothing
 * of it is left. `envelope` says it is advertised as action + args (#1172).
 * The server re-advertises through this, and a context projection reads it.
 */
export function advertisedPresentation(
  tool: ToolDef,
  envelope: boolean,
  strategy: ContextStrategy,
  have: ReadonlySet<string> | null,
): { description: string; inputSchema: z.ZodTypeAny } | null {
  const view = withholdFromTool(tool, have);
  if (!view) return null;
  if (envelope) {
    return {
      description: strategy === "full" ? fullSurfaceDescription(view) : view.description,
      inputSchema: envelopeInputSchema(view),
    };
  }
  return { description: view.description, inputSchema: flatInputSchema(view, view.schema) };
}

/** Whether tools/list carries the flow tool. Micro reaches flows through its gateway. */
export function advertisesFlowTool(strategy: ContextStrategy): boolean {
  return strategy !== "micro";
}

export interface McpServerOptions {
  /**
   * This client's access mode, read on every call. The daemon holds one per
   * connection so it can switch a session to read-only (#1484). Absent is
   * read-write.
   */
  access?: SessionAccess;
  /**
   * The id this client's asset leases are held under. The daemon passes the
   * connection's session id; a stdio server is one session, this process.
   */
  sessionId?: string;
}

export function createMcpServer(rt: Runtime, opts: McpServerOptions = {}): RuntimeServer {
  const { sessions, loads, contextStrategy } = rt;

  const server = new McpServer({
    name: "ue-mcp",
    // Read from package.json, never written here.
    version: packageVersion(),
  }, {
    instructions: composeServerInstructions(
      contextStrategy,
      loads.surfaces,
      sessions.list().map((s) => s.name),
      sessions.active.name,
    ),
  });

  const elicit = buildElicit(server);
  // The dialog guard and the flow runner ask whoever called last.
  const claimElicit = (): void => {
    rt.baseCtx.elicit = elicit;
  };
  claimElicit();

  const deps: DispatchDeps = {
    sessions,
    loads,
    lockingCfg: rt.lockingCfg,
    dialogGuardFor: rt.dialogGuardFor,
    elicit: () => elicit,
    client: () => server.server.getClientVersion(),
    // An MCP session's playtest runs follow `ue-mcp playtest mode`.
    gatePlaytest: true,
    flow: { tool: rt.flowTool, ctx: rt.baseCtx },
    ...(opts.access ? { access: () => opts.access!.mode } : {}),
    ...(opts.sessionId
      ? { leaseHolder: () => ({ id: opts.sessionId!, label: server.server.getClientVersion()?.name ?? "unknown client" }) }
      : {}),
  };

  // The upgrade notice leads this client's first tool response.
  const takeNotice = upgradeNoticeForClient();
  const withNotice = async (pending: Promise<ToolResult>): Promise<ToolResult> => {
    const result = await pending;
    const notice = takeNotice();
    return notice ? { ...result, content: [{ type: "text", text: notice }, ...result.content] } : result;
  };

  const registeredTools = new Map<string, ReturnType<typeof server.tool>>();
  // Tools registered as action + args. The flow tool is registered flat and stays flat.
  const envelopeTools = new Set<string>();

  const descriptionFor = (tool: ToolDef): string =>
    envelopeTools.has(tool.name) && contextStrategy === "full" ? fullSurfaceDescription(tool) : tool.description;

  // A bridge action no running plugin has is withheld from tools/list (6.4).
  function advertise(tool: ToolDef, have = rt.advertisedHave()): void {
    const registration = registeredTools.get(tool.name);
    if (!registration) return;
    const envelope = envelopeTools.has(tool.name);
    const shown = advertisedPresentation(tool, envelope, contextStrategy, have);
    if (!shown) {
      if (registration.enabled) registration.disable();
      return;
    }
    if (!registration.enabled) registration.enable();
    const view = withholdFromTool(tool, have)!;
    // The SDK rebuilds a stripping object from the raw shape; put the
    // advertised one back so a flat call still reaches validation.
    registration.update({ description: shown.description, paramsSchema: envelope ? envelopeShape(view) : view.schema });
    registration.inputSchema = shown.inputSchema;
  }

  // ── Category tools, dispatched through each session's task registry ──
  // A call reaches the category as it is advertised NOW: turning a category
  // on or off at runtime can replace the object (the micro gateway, a lean
  // view), and the registration has to follow it by name.
  const currentTool = (name: string, fallback: ToolDef): ToolDef =>
    rt.advertisedTools.find((t) => t.name === name) ?? fallback;
  function registerCategory(tool: ToolDef): void {
    const shape: Record<string, z.ZodType> = { ...tool.schema };
    // Advertised as `action` + `args` (#1172); the flat shape stays the
    // validation contract, applied in dispatch instead of by the SDK.
    const envelope = usesArgsEnvelope(tool);
    if (envelope) envelopeTools.add(tool.name);
    const description = descriptionFor(tool);
    const callback = (callArgs: Record<string, unknown>, extra: CallExtra) => {
      claimElicit();
      return withNotice(dispatchCategoryCall(deps, currentTool(tool.name, tool), envelope, callArgs, extra));
    };
    const registration = envelope
      ? server.registerTool(tool.name, { description, inputSchema: envelopeInputSchema(tool) }, callback as never)
      : server.registerTool(tool.name, { description, inputSchema: flatInputSchema(tool, shape) }, callback as never);
    registeredTools.set(tool.name, registration);
  }
  for (const tool of rt.advertisedTools) registerCategory(tool);

  // ── Flow tool ────────────────────────────────────────────────────
  // In micro mode flow is a gateway category (deps.flow), not a second tool.
  if (advertisesFlowTool(contextStrategy)) {
    const flowTool = rt.flowTool;
    const flowShape: Record<string, z.ZodType> = { ...flowTool.schema };
    const flowRegistration = server.registerTool(flowTool.name, { description: flowTool.description, inputSchema: strippingInputSchema(flowShape) }, ((rawParams: Record<string, unknown>) => {
      claimElicit();
      return withNotice(dispatchFlowCall(deps, flowTool, rt.baseCtx, rawParams)) as never;
    }) as never);
    registeredTools.set(flowTool.name, flowRegistration);
  }

  // Resources registered with Epic's Unreal MCP plugin in the active editor.
  registerEditorResources(server, () => sessions.active.guarded);

  // Re-advertise whenever targeting or what the plugins registered changes.
  let parityKey: string | null = null;
  const sync = (force: boolean): void => {
    const have = rt.advertisedHave();
    const key = have ? [...have].sort().join(",") : "";
    if (!force && key === parityKey) return;
    parityKey = key;
    // A category turned on at runtime that was off at startup was never
    // registered; one turned off is still registered and is hidden.
    for (const tool of rt.advertisedTools) if (!registeredTools.has(tool.name)) registerCategory(tool);
    const live = new Set(rt.targetable.map((t) => t.name));
    for (const [name, registration] of registeredTools) {
      if (!live.has(name) && registration.enabled) registration.disable();
    }
    for (const tool of rt.targetable) advertise(tool, have);
  };
  sync(true);
  const unsubscribe = rt.onSurfaceChanged(() => {
    sync(true);
    // An editor that connected or changed may serve different resources.
    announceResourceChange(server);
  });

  return { server, dispose: unsubscribe };
}
