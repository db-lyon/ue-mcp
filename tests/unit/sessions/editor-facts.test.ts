/**
 * EditorFacts: fetched when first asked for, kept until an event moves them,
 * and readable by conditions through the `editor` namespace.
 */
import { describe, expect, it, vi } from "vitest";
import type { ConditionContext } from "@db-lyon/flowkit";
import { EditorFacts } from "../../../src/sessions/editor-facts.js";
import { factsScope, hostNamespaces, makeConditionEvaluator } from "../../../src/flow/condition.js";
import type { ProjectContext } from "../../../src/config/project.js";
import type { ToolContext } from "../../../src/core/types.js";

// The effects table installs itself into editor-facts.ts when it loads.
await import("../../../src/surface/action-effects.js");

function fakeEditor() {
  const bridge = {
    isConnected: true,
    capabilities: { protocolVersion: 2, engineVersion: "5.7.1", actions: ["a", "b"], legacy: false } as Record<string, unknown> | null,
  };
  let pieCount = 0;
  const call = vi.fn(async (method: string) => {
    if (method === "list_pie_instances") return { count: pieCount, instances: [] };
    if (method === "get_current_level") return { levelName: "Main", levelPath: "/Game/Maps/Main.Main" };
    if (method === "list_dirty_packages") return { content: [{ package: "/Game/A" }], maps: [{ package: "/Game/Maps/Main" }] };
    throw new Error(`unexpected ${method}`);
  });
  const project = { projectPath: null } as unknown as ProjectContext;
  const facts = new EditorFacts({ bridge: bridge as never, call, project });
  return { bridge, call, facts, setPie: (n: number) => { pieCount = n; } };
}

describe("EditorFacts", () => {
  it("fetches once and serves the cache until invalidated", async () => {
    const { facts, call } = fakeEditor();
    expect(await facts.get("map")).toBe("/Game/Maps/Main.Main");
    expect(await facts.get("map")).toBe("/Game/Maps/Main.Main");
    expect(call).toHaveBeenCalledTimes(1);
    expect(await facts.get("dirtyPackages")).toEqual(["/Game/A", "/Game/Maps/Main"]);
  });

  it("drops the PIE state when PIE starts or stops", async () => {
    const { facts, setPie } = fakeEditor();
    expect(await facts.get("pie")).toBe(false);
    setPie(1);
    expect(await facts.get("pie")).toBe(false);
    facts.observe("pie_control", { action: "start" });
    expect(await facts.get("pie")).toBe(true);
  });

  it("drops the dirty list on a change and keeps it on a read", async () => {
    const { facts, call } = fakeEditor();
    await facts.get("dirtyPackages");
    facts.observe("list_dirty_packages", {});
    await facts.get("dirtyPackages");
    expect(call).toHaveBeenCalledTimes(1);
    facts.observe("save_asset", { assetPath: "/Game/A" });
    await facts.get("dirtyPackages");
    expect(call).toHaveBeenCalledTimes(2);
  });

  it("drops everything on a reconnect, which brings new capabilities", async () => {
    const { facts, bridge, call } = fakeEditor();
    await facts.get("map");
    expect(facts.peek("engineVersion")).toBe("5.7.1");
    bridge.capabilities = { protocolVersion: 2, engineVersion: "5.8.0", legacy: false };
    expect(facts.peek("engineVersion")).toBe("5.8.0");
    await facts.get("map");
    expect(call).toHaveBeenCalledTimes(2);
  });

  it("is read by a condition through the editor namespace", async () => {
    const { facts, bridge, setPie } = fakeEditor();
    const ctx = { bridge, project: { projectPath: null }, session: { name: "alpha", facts } } as unknown as ToolContext;
    const evaluate = makeConditionEvaluator(hostNamespaces(ctx), factsScope(ctx));
    const at = { steps: [] } as unknown as ConditionContext;
    expect(await evaluate("editor.pie", at)).toBe(false);
    setPie(2);
    facts.observe("pie_control", {});
    expect(await evaluate("editor.pie && editor.connected", at)).toBe(true);
    expect(await evaluate("editor.engineVersion == '5.7.1'", at)).toBe(true);
    expect(await evaluate("editor.map == '/Game/Maps/Main.Main'", at)).toBe(true);
  });

  it("reads a project setting through project.config", async () => {
    const { bridge } = fakeEditor();
    const project = { projectPath: null, projectConfig: { block: { bridge: { port: 9100 } } } };
    const ctx = { bridge, project } as unknown as ToolContext;
    const evaluate = makeConditionEvaluator(hostNamespaces(ctx));
    expect(await evaluate("project.config.bridge.port == 9100", { steps: [] } as unknown as ConditionContext)).toBe(true);
  });

  it("fails a condition whose fact cannot be read, rather than reading it as false", async () => {
    const { facts, bridge, call } = fakeEditor();
    call.mockRejectedValueOnce(new Error("editor refused"));
    const ctx = { bridge, project: { projectPath: null }, session: { name: "alpha", facts } } as unknown as ToolContext;
    const evaluate = makeConditionEvaluator(hostNamespaces(ctx), factsScope(ctx));
    await expect(evaluate("!editor.pie", { steps: [] } as unknown as ConditionContext)).rejects.toThrow("editor refused");
  });
});
