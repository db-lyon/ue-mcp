import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS } from "../../src/bridge/bridge-timeouts.js";
import { auditHandlers, readRegistrations } from "../../scripts/audit-handler-conventions.mjs";
import { handlerParamReads, readAllSources } from "../../scripts/lib/handler-param-reads.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SNAPSHOT = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  handlers: Record<string, { params: Array<{ name: string; required: boolean }>; contractExempt?: string }>;
};

function fakeCtx(seen: Array<{ method: string; params: Record<string, unknown> }>) {
  return {
    bridge: {
      isConnected: true,
      call: async (method: string, params: Record<string, unknown>) => { seen.push({ method, params }); return { success: true }; },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
      connect: async () => {},
      retargetProject: () => ({}),
    },
    project: {},
  } as never;
}

const ACTIONS = ["list_loaded_regions", "unload_loaded_regions", "load_region"] as const;

describe("editor loaded region actions: the advertised spec", () => {
  it("exposes the three actions under the bridge names the handlers register", () => {
    for (const name of ACTIONS) {
      expect(levelTool.schema.action.safeParse(name).success, name).toBe(true);
      expect(levelTool.actions[name].bridge, name).toBe(name);
    }
  });

  it("declares list as a read and unload and load as mutations", () => {
    expect(levelTool.actions.list_loaded_regions.effect).toBe("read");
    expect(levelTool.actions.unload_loaded_regions.effect).toBe("mutate");
    expect(levelTool.actions.load_region.effect).toBe("mutate");
  });

  it("takes each action's parameters from its recorded spec, with unload's selector as a choice", () => {
    const spec = (name: (typeof ACTIONS)[number]) => {
      const action = levelTool.actions[name];
      return action.kind === "bridge" ? action : undefined;
    };
    expect(spec("list_loaded_regions")?.paramSpec?.map((p) => p.name)).toEqual(["includeUnloaded", "bounds", "cursor", "limit"]);
    expect(spec("unload_loaded_regions")?.paramSpec?.map((p) => p.name)).toEqual(["ids", "bounds", "maxRegions", "force", "dryRun"]);
    expect(spec("unload_loaded_regions")?.paramChoices).toEqual([
      { mode: "atLeastOne", branches: [["ids"], ["bounds"]] },
    ]);
    expect(spec("load_region")?.paramSpec?.map((p) => p.name)).toEqual(["bounds", "label", "maxActors", "dryRun"]);
    expect(levelTool.actions.unload_loaded_regions.description).toContain(
      "Params: at least one of ids/bounds, maxRegions?, force?, dryRun?",
    );
  });

  it("refuses an unload that names no region before anything is sent", async () => {
    const seen: Array<{ method: string; params: Record<string, unknown> }> = [];
    await expect(levelTool.handler(fakeCtx(seen), { action: "unload_loaded_regions", force: true }))
      .rejects.toThrow(/needs at least one of ids\/bounds/);
    expect(seen).toEqual([]);
  });

  it("sends ids, bounds and the guards through unchanged", async () => {
    const seen: Array<{ method: string; params: Record<string, unknown> }> = [];
    const bounds = { min: { x: -10, y: -10, z: -10 }, max: { x: 10, y: 10, z: 10 } };
    await levelTool.handler(fakeCtx(seen), {
      action: "unload_loaded_regions", ids: ["/Game/M.M:PersistentLevel.R_0"], bounds, maxRegions: 2, force: false, dryRun: true,
    });
    expect(seen).toEqual([{
      method: "unload_loaded_regions",
      params: { ids: ["/Game/M.M:PersistentLevel.R_0"], bounds, maxRegions: 2, force: false, dryRun: true },
    }]);
  });

  it("declares bounds as the one required parameter of load_region", () => {
    const action = levelTool.actions.load_region;
    const required = action.kind === "bridge" ? action.paramSpec?.filter((p) => p.required).map((p) => p.name) : undefined;
    expect(required).toEqual(["bounds"]);
  });

  it("gives the two actions that stream actors the long call budget the plugin registers", () => {
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.unload_loaded_regions).toBe(300);
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.load_region).toBe(300);
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.list_loaded_regions).toBeUndefined();
  });
});

describe("editor loaded region actions: the C++ handlers", () => {
  const registrations = readRegistrations();
  const sources = readAllSources();

  it("registers each action", () => {
    for (const name of ACTIONS) expect(registrations.has(name), name).toBe(true);
  });

  it("reads nothing its recorded spec does not declare, and all of it for the exempt load_region", () => {
    for (const name of ACTIONS) {
      const reads = handlerParamReads(name, { registrations, sources });
      expect(reads, `${name}: handler body not found`).not.toBeNull();
      const declared = SNAPSHOT.handlers[name].params.map((p) => p.name).sort();
      expect([...reads!.keys].filter((k) => !declared.includes(k)), `${name}: reads an undeclared parameter`).toEqual([]);
      if (name === "load_region") {
        expect(reads!.opaque, name).toEqual([]);
        expect([...reads!.keys].sort(), name).toEqual(declared);
      }
    }
  });

  it("exempts only load_region from the contract call, since it is the one that creates", () => {
    expect(SNAPSHOT.handlers.load_region.contractExempt).toBeTruthy();
    expect(SNAPSHOT.handlers.unload_loaded_regions.contractExempt).toBeUndefined();
    expect(SNAPSHOT.handlers.list_loaded_regions.contractExempt).toBeUndefined();
  });

  it("answers the convention questions in the result body", () => {
    const byBridge = new Map<string, string>();
    for (const tool of ALL_TOOLS) for (const spec of Object.values(tool.actions)) if (spec.bridge) byBridge.set(spec.bridge, spec.effect);
    const rows = auditHandlers((bridge: string) => byBridge.get(bridge) ?? "orphan") as Array<{
      action: string; bodyFound: boolean; idempotent: boolean; rollback: boolean; declaresNoRollback: boolean;
    }>;
    const row = (name: string) => rows.find((r) => r.action === name)!;

    for (const name of ACTIONS) expect(row(name).bodyFound, `${name} body`).toBe(true);
    // Load emits the inverse of what it created; unload says why it has none.
    expect(row("load_region").rollback).toBe(true);
    expect(row("unload_loaded_regions").declaresNoRollback).toBe(true);
    expect(row("load_region").idempotent).toBe(true);
    expect(row("unload_loaded_regions").idempotent).toBe(true);
  });
});
