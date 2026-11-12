/**
 * select and omit through the real server, on every kind of tool a client
 * calls: a category action answered in this process, a bridge action through
 * the micro gateway, and the flow tool, advertised (full) and through the
 * gateway's flow category (micro). The editor never receives either key.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox } from "./harness.js";

let sb: Sandbox;
let bridge: FakeBridge;

beforeAll(async () => {
  sb = new Sandbox();
  bridge = await FakeBridge.start({
    projectDir: sb.path("editor"),
    handlers: {
      get_world_outliner: () => ({
        success: true,
        world: "editor",
        actors: [
          { label: "Floor", class: "StaticMeshActor", location: { x: 0, y: 0, z: 0 } },
          { label: "Sun", class: "DirectionalLight", location: { x: 1, y: 2, z: 3 } },
        ],
      }),
    },
  });
}, 60_000);

afterAll(async () => {
  await bridge?.stop();
  sb.cleanup();
});

describe("select and omit", () => {
  it("narrow a bridge action's answer through the gateway, and never reach the editor", async () => {
    covers("server.field-select");
    const server = await McpServer.start(sb, { projects: [], port: bridge.port });
    try {
      const r = await server.call("tools", { action: "call", category: "level", method: "get_outliner", args: { select: "actors.label" } });
      const sent = bridge.calls.at(-1)!;
      expect(r.isError, r.text).toBe(false);
      expect(sent.params).not.toHaveProperty("select");
      expect(json(r)).toEqual({ actors: [{ label: "Floor" }, { label: "Sun" }] });

      const omitted = json(await server.call("tools", { action: "call", category: "level", method: "get_outliner", args: { omit: ["actors.location", "success"] } }));
      expect(omitted).toEqual({ world: "editor", actors: [{ label: "Floor", class: "StaticMeshActor" }, { label: "Sun", class: "DirectionalLight" }] });
    } finally {
      await server.close();
    }
  });

  it("narrow the flow tool's answer, advertised and through the gateway", async () => {
    covers("server.field-select");
    const uproject = sb.makeProject("FlowGame");
    for (const strategy of ["full", "micro"] as const) {
      const server = await McpServer.start(sb, { projects: [uproject], strategy });
      try {
        const all = json<{ flowCount: number; flows: Array<{ name: string }> }>(await server.callFlow({ action: "list" }));
        expect(all.flowCount, strategy).toBeGreaterThan(0);
        const names = json<Record<string, unknown>>(await server.callFlow({ action: "list", select: "flows.name" }));
        expect(Object.keys(names), strategy).toEqual(["flows"]);
        expect((names.flows as Array<Record<string, unknown>>).every((f) => Object.keys(f).join() === "name"), strategy).toBe(true);
        const dropped = json<Record<string, unknown>>(await server.callFlow({ action: "list", omit: "flows" }));
        expect(dropped, strategy).toEqual({ flowCount: all.flowCount });
      } finally {
        await server.close();
      }
    }
  }, 120_000);
});
