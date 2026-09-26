import { describe, it, expect } from "vitest";
import { widgetTool } from "../../src/tools/widget.js";
import type { IBridge } from "../../src/bridge.js";
import type { ToolContext } from "../../src/core/types.js";

/**
 * #799: add_widget and remove_widget compile and save the Widget Blueprint
 * before they answer. The default 30s cap expired on calls the editor went on
 * to finish, which made a completed mutation look like a failed one.
 */
function recordingBridge(): IBridge & { calls: Array<{ method: string; timeoutMs?: number }> } {
  const calls: Array<{ method: string; timeoutMs?: number }> = [];
  return {
    calls,
    isConnected: true,
    connect: async () => {},
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    call: async (method, _params, timeoutMs) => {
      calls.push({ method, timeoutMs });
      return { success: true };
    },
  };
}

describe("widget tree mutations", () => {
  for (const action of ["add_widget", "remove_widget"] as const) {
    it(`${action} allows longer than the 30s default`, async () => {
      const spec = widgetTool.actions[action];
      expect(spec.timeoutMs).toBe(120_000);
    });

    it(`${action} forwards its timeout to the bridge`, async () => {
      const bridge = recordingBridge();
      await widgetTool.handler({ bridge } as unknown as ToolContext, {
        action,
        assetPath: "/Game/UI/WBP_Test",
        widgetName: "StartButton",
        widgetClass: "Button",
      });

      expect(bridge.calls).toEqual([{ method: action, timeoutMs: 120_000 }]);
    });
  }

  it("tells callers the mutations are idempotent, so a retry is safe", () => {
    for (const action of ["add_widget", "remove_widget"] as const) {
      expect(widgetTool.actions[action].description).toMatch(/idempotent/i);
    }
  });
});
