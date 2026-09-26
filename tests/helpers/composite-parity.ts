/**
 * Old against new for an action moved from a handler to a composite: the
 * handler body as it stood before the move runs next to the composite against
 * the same scripted editor, live and through the category tool, and the two
 * must answer the same bytes and send the same calls.
 */
import { expect } from "vitest";
import type { TaskResult } from "@db-lyon/flowkit";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { createLiveTask } from "../../src/flow/live-task.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";

export type LegacyHandler = (ctx: ToolContext, p: Record<string, unknown>) => Promise<unknown>;
export type Answer = (method: string, params: Record<string, unknown>) => unknown;

function scriptedContext(answer: Answer, contentRoots?: string[]) {
  const calls: Array<{ method: string; params: Record<string, unknown>; timeoutMs?: number }> = [];
  const bridge = {
    isConnected: true,
    call: async (method: string, params: Record<string, unknown>, timeoutMs?: number) => {
      calls.push({ method, params, timeoutMs });
      return answer(method, params);
    },
  } as unknown as IBridge;
  const ctx = { project: { config: { contentRoots } }, bridge } as unknown as FlowContext;
  return { ctx, calls };
}

/** The same tool with one action put back as the handler it used to be. */
function legacyTool(tool: ToolDef, action: string, handler: LegacyHandler): ToolDef {
  const current = tool.actions[action];
  return tool.rebuild!({
    ...tool.actions,
    [action]: { kind: "handler", effect: current.effect, description: current.description, handler },
  });
}

function settled(result: TaskResult): Record<string, unknown> {
  return { success: result.success, data: result.data, error: result.error?.message };
}

async function live(tool: ToolDef, action: string, answer: Answer, params: Record<string, unknown>, roots?: string[]) {
  const { ctx, calls } = scriptedContext(answer, roots);
  const graphCtx = { ...ctx, getToolGraph: () => [tool] } as FlowContext;
  const task = await createLiveTask({ registry: buildFlowRegistry([tool]) }, graphCtx, `${tool.name}.${action}`, params);
  return { result: settled(await task.run()), calls };
}

async function direct(tool: ToolDef, action: string, answer: Answer, params: Record<string, unknown>, roots?: string[]) {
  const { ctx, calls } = scriptedContext(answer, roots);
  const graphCtx = { ...ctx, getToolGraph: () => [tool] } as ToolContext;
  try {
    return { out: await tool.handler(graphCtx, { action, ...params }), calls };
  } catch (e) {
    return { thrown: (e as Error).message, calls };
  }
}

/** Old and new, on both routes, against the same editor. Returns the new live result. */
export async function expectParity(
  tool: ToolDef,
  action: string,
  legacy: LegacyHandler,
  answer: Answer,
  params: Record<string, unknown>,
  roots?: string[],
): Promise<Record<string, unknown>> {
  const old = legacyTool(tool, action, legacy);
  expect([tool.actions[action].kind, old.actions[action].kind]).toEqual(["flow", "handler"]);
  const oldLive = await live(old, action, answer, params, roots);
  const newLive = await live(tool, action, answer, params, roots);
  expect(newLive.result).toEqual(oldLive.result);
  expect(JSON.stringify(newLive.result.data)).toBe(JSON.stringify(oldLive.result.data));
  expect(newLive.calls).toEqual(oldLive.calls);
  const oldDirect = await direct(old, action, answer, params, roots);
  const newDirect = await direct(tool, action, answer, params, roots);
  expect(JSON.stringify(newDirect)).toBe(JSON.stringify(oldDirect));
  return newLive.result;
}
