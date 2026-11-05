/**
 * What a client would be handed under one context strategy and a chosen set
 * of categories, and a way to run a call against exactly that surface.
 *
 * Built from the pieces the MCP server itself uses: the session surfaces, the
 * strategy transform (strategySurface: applyLeanContext, buildMicroGateway),
 * the instructions composer, the server's per-tool presentation and the SDK's
 * schema conversion. A call goes through Runtime.callTool, the dispatch
 * api.callAction takes, against a registry built from the projected tools, so
 * lean's describe and catalog and micro's gateway resolve as the server
 * resolves them, and dialogs, elicitation, effect gates and checkpoints apply
 * the same way.
 */
import { normalizeObjectSchema } from "@modelcontextprotocol/sdk/server/zod-compat.js";
import { toJsonSchemaCompat } from "@modelcontextprotocol/sdk/server/zod-json-schema-compat.js";
import type { InProcessCallOptions, ToolDef } from "../core/types.js";
import { composeServerInstructions } from "../surface/context/instructions.js";
import type { ContextStrategy } from "../surface/context/lean-context.js";
import { MICRO_GATEWAY_CALL, MICRO_GATEWAY_TOOL } from "../surface/context/micro-context.js";
import { usesArgsEnvelope } from "../surface/context/call-envelope.js";
import { injectEditorTarget, injectMigrateTarget } from "../surface/target-params.js";
import { unionSurface } from "../sessions/session-surface.js";
import { buildLoadRegistry, strategySurface, type SessionLoad } from "../sessions/session-load.js";
import { errorResult, type ToolResult } from "../dispatch/server-dispatch.js";
import { advertisedPresentation } from "./mcp-server.js";
import type { Runtime } from "./runtime.js";

export const CONTEXT_STRATEGIES: readonly ContextStrategy[] = ["micro", "lean", "full"];

export interface ContextProjectionOptions {
  strategy: ContextStrategy;
  /** Category names. Omitted means every registered, enabled category. */
  categories?: string[];
}

export interface ProjectedTool {
  name: string;
  description: string;
  /** JSON Schema, as tools/list sends it. */
  inputSchema: Record<string, unknown>;
  /** The category this tool is, or null for one that is not a category (micro's gateway, lean's catalog, flow). */
  category: string | null;
}

export interface ContextProjection {
  strategy: ContextStrategy;
  /** The categories in scope after resolving. */
  categories: string[];
  /** The initialize instructions this strategy sends, narrowed to the scope when one was given. */
  instructions: string;
  /** What tools/list sends, in order. */
  tools: ProjectedTool[];
  /** Characters of JSON.stringify({ instructions, tools }) as the server sends them, / 4, rounded. */
  seedTokens: number;
  estimator: "chars/4";
}

/** A request this projection cannot serve: an unknown strategy or category. */
export class ContextProjectionError extends Error {}

interface Built {
  strategy: ContextStrategy;
  categories: string[];
  scoped: boolean;
  /** The advertised category-shaped tools (gateway, catalog and categories), before the flow tool. */
  tools: ToolDef[];
  /** The scoped enabled graph the strategy was applied to. */
  base: ToolDef[];
}

/** The enabled graph the server would advertise under `full`: one session's, or the union. */
export function enabledGraph(rt: Runtime): ToolDef[] {
  const enabled = (load: SessionLoad): ToolDef[] => load.surface.tools.filter((t) => !load.surface.disabled.has(t.name));
  const loads = rt.loads.all();
  if (loads.length <= 1) return enabled(rt.primaryLoad);
  return unionSurface(loads.map((l) => ({ ...l.surface, tools: enabled(l) }))).tools;
}

/** A copy of a tool whose schema can take editor targeting without touching the graph it came from. */
function detached(tool: ToolDef): ToolDef {
  return { ...tool, schema: { ...tool.schema } };
}

export class ContextProjector {
  private cache = new Map<string, Built>();
  private views = new WeakMap<SessionLoad, Map<string, SessionLoad>>();

  constructor(private readonly rt: Runtime) {
    // A plugin registering, an editor joining or leaving: build again.
    rt.onSurfaceChanged(() => this.cache.clear());
  }

  private resolve(opts: ContextProjectionOptions): { strategy: ContextStrategy; scope: string[] | null } {
    if (!CONTEXT_STRATEGIES.includes(opts.strategy)) {
      throw new ContextProjectionError(`strategy is one of ${CONTEXT_STRATEGIES.join(", ")}, not '${String(opts.strategy)}'`);
    }
    if (opts.categories === undefined) return { strategy: opts.strategy, scope: null };
    const known = new Set(enabledGraph(this.rt).map((t) => t.name));
    const unknown = opts.categories.filter((c) => !known.has(c));
    if (unknown.length > 0) {
      throw new ContextProjectionError(`no such category: ${unknown.join(", ")}. Registered: ${[...known].join(", ")}`);
    }
    return { strategy: opts.strategy, scope: [...new Set(opts.categories)] };
  }

  private build(opts: ContextProjectionOptions): Built {
    const { strategy, scope } = this.resolve(opts);
    const key = `${strategy}|${scope ? [...scope].sort().join(",") : "*"}`;
    const hit = this.cache.get(key);
    if (hit) return hit;

    const all = enabledGraph(this.rt);
    const inScope = scope ? new Set(scope) : null;
    const base = all.filter((t) => !inScope || inScope.has(t.name)).map(detached);
    const surface = { ...this.rt.primaryLoad.surface, tools: base, disabled: new Set<string>() };
    // Lean and micro build fresh tools; full advertises the detached copies themselves.
    const tools = strategySurface(surface, strategy).advertisedTools;
    // Per-call targeting exists only while more than one editor is registered, as on the server.
    const names = this.rt.sessions.list().map((s) => s.name);
    if (names.length > 1) {
      for (const tool of tools) {
        injectEditorTarget(tool, names);
        injectMigrateTarget(tool, names);
      }
    }
    const built: Built = { strategy, categories: base.map((t) => t.name), scoped: scope !== null, tools, base };
    this.cache.set(key, built);
    return built;
  }

  /** What the server would send under this strategy and scope. */
  project(opts: ContextProjectionOptions): ContextProjection {
    const built = this.build(opts);
    const rt = this.rt;
    const have = rt.advertisedHave();
    const categoryNames = new Set(built.categories);
    const wire: Array<{ name: string; description: string; inputSchema: Record<string, unknown>; execution: { taskSupport: "forbidden" } }> = [];
    const tools: ProjectedTool[] = [];
    for (const tool of [...built.tools, rt.flowTool]) {
      const envelope = tool !== rt.flowTool && usesArgsEnvelope(tool);
      const shown = advertisedPresentation(tool, envelope, built.strategy, have);
      if (!shown) continue;
      const obj = normalizeObjectSchema(shown.inputSchema);
      const inputSchema = (obj
        ? toJsonSchemaCompat(obj, { strictUnions: true, pipeStrategy: "input" })
        : { type: "object", properties: {} }) as Record<string, unknown>;
      // registerTool gives every tool this execution field, and tools/list sends it.
      wire.push({ name: tool.name, description: shown.description, inputSchema, execution: { taskSupport: "forbidden" } });
      tools.push({ name: tool.name, description: shown.description, inputSchema, category: categoryNames.has(tool.name) ? tool.name : null });
    }
    const instructions = composeServerInstructions(
      built.strategy,
      rt.loads.surfaces,
      rt.sessions.list().map((s) => s.name),
      rt.sessions.active.name,
      built.scoped ? built.base : undefined,
    );
    return {
      strategy: built.strategy,
      categories: built.categories,
      instructions,
      tools,
      // The estimate scripts/context-tax.mjs makes without an API key, over the same payload.
      seedTokens: Math.round(JSON.stringify({ instructions, tools: wire }).length / 4),
      estimator: "chars/4",
    };
  }

  /** The load one session dispatches a projected call against: same session, the projection's registry. */
  private viewOf(built: Built, scopeKey: string): (load: SessionLoad) => SessionLoad {
    return (load) => {
      let perLoad = this.views.get(load);
      if (!perLoad) this.views.set(load, (perLoad = new Map()));
      const key = `${built.strategy}|${scopeKey}`;
      let view = perLoad.get(key);
      if (!view) {
        const scope = built.scoped ? new Set(built.categories) : undefined;
        const { advertisedTools, registryTools } = strategySurface(load.surface, built.strategy, scope);
        view = { ...load, advertisedTools, registryTools, registry: buildLoadRegistry(registryTools, load.pluginLoad, load.configDir) };
        perLoad.set(key, view);
      }
      return view;
    };
  }

  /**
   * Run one call to a projected tool, as the MCP server would run it. A tool or
   * gateway call outside the projection's categories is refused, not run.
   */
  async call(
    opts: ContextProjectionOptions,
    toolName: string,
    input: Record<string, unknown>,
    callOpts: InProcessCallOptions = {},
  ): Promise<ToolResult> {
    const built = this.build(opts);
    const rt = this.rt;
    if (toolName === rt.flowTool.name) return rt.callTool(rt.flowTool, input, callOpts);
    const tool = built.tools.find((t) => t.name === toolName);
    if (!tool) {
      const registered = enabledGraph(rt).some((t) => t.name === toolName);
      return errorResult(
        "NOT_FOUND",
        registered
          ? `'${toolName}' is outside this context's categories (${built.categories.join(", ")}), so it was not run.`
          : `No tool '${toolName}' in the ${built.strategy} context. Tools: ${[...built.tools.map((t) => t.name), rt.flowTool.name].join(", ")}.`,
      );
    }
    if (tool.name === MICRO_GATEWAY_TOOL && input.action === MICRO_GATEWAY_CALL) {
      const category = typeof input.category === "string" ? input.category : "";
      if (category && !built.categories.includes(category) && enabledGraph(rt).some((t) => t.name === category)) {
        return errorResult(
          "NOT_FOUND",
          `'${category}' is outside this context's categories (${built.categories.join(", ")}), so it was not run.`,
        );
      }
    }
    const scopeKey = built.scoped ? [...built.categories].sort().join(",") : "*";
    try {
      return await rt.callTool(tool, input, {
        ...callOpts,
        envelope: usesArgsEnvelope(tool),
        loadView: this.viewOf(built, scopeKey),
      });
    } catch (e) {
      // The server hands a validation throw to the SDK, which answers it as an error result.
      return { content: [{ type: "text", text: e instanceof Error ? e.message : String(e) }], isError: true };
    }
  }
}
