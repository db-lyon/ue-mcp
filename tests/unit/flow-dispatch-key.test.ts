// A flow step must not send the dispatch key to the bridge either.
//
// The task name selects the action on this route, so a params key called
// `action` is the dispatcher's and never an argument. The MCP route strips it;
// this one did not, so the two disagreed and a wrapped tool with an argument of
// that name would have received the step's own value (#1078).
import { describe, expect, it } from "vitest";

import { bridgeTaskClass } from "../../src/flow/task-factory.js";

function taskWith(mapParams?: (p: Record<string, unknown>) => Record<string, unknown>) {
  const seen: Array<Record<string, unknown>> = [];
  const Task = bridgeTaskClass("probe_action", "probe_method", mapParams);
  const ctx = {
    bridge: {
      isConnected: true,
      call: async (_m: string, params: Record<string, unknown>) => { seen.push(params); return { ok: true }; },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
      connect: async () => {},
      retargetProject: () => ({}),
    },
    project: {},
  };
  return { Task, ctx, seen };
}

describe("the dispatch key on a flow step", () => {
  it("never reaches the bridge, even when the step's params carry it", async () => {
    const { Task, ctx, seen } = taskWith();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    await new (Task as any)(ctx, { action: "probe_action", assetPath: "/Game/X" }).execute();

    expect(seen).toHaveLength(1);
    expect(
      Object.prototype.hasOwnProperty.call(seen[0], "action"),
      `the bridge was handed ${JSON.stringify(seen[0])}`,
    ).toBe(false);
    expect(seen[0].assetPath).toBe("/Game/X");
  });

  it("keeps it off a mapParams that forwards its whole bag", async () => {
    const { Task, ctx, seen } = taskWith((p) => ({ ...p }));
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    await new (Task as any)(ctx, { action: "probe_action", assetPath: "/Game/X" }).execute();

    expect(seen).toHaveLength(1);
    expect(
      JSON.stringify(seen[0]).includes("probe_action"),
      `the bridge was handed ${JSON.stringify(seen[0])}`,
    ).toBe(false);
  });
});
