import { z } from "zod";
import type { EditorBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";

export interface ToolContext {
  bridge: EditorBridge;
  project: ProjectContext;
}

export interface ToolDef {
  name: string;
  description: string;
  schema: Record<string, z.ZodType>;
  handler: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
}

export function bridgeTool(
  name: string,
  description: string,
  schema: Record<string, z.ZodType>,
  bridgeMethod?: string,
  mapParams?: (params: Record<string, unknown>) => Record<string, unknown>,
): ToolDef {
  return {
    name,
    description,
    schema,
    handler: async (ctx, params) => {
      const mapped = mapParams ? mapParams(params) : params;
      return ctx.bridge.call(bridgeMethod ?? name, mapped);
    },
  };
}

export function bt(
  name: string,
  description: string,
  schema: Record<string, z.ZodType>,
  bridgeMethod?: string,
  mapParams?: (params: Record<string, unknown>) => Record<string, unknown>,
): ToolDef {
  return bridgeTool(name, description, schema, bridgeMethod, mapParams);
}
