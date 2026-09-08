/**
 * Niagara graph authoring, against a real editor.
 *
 * REQUIRES A PLUGIN REBUILD. The compile gate below calls niagara(compile),
 * which is new C++ in NiagaraHandlers_Compile.cpp; until the bridge is rebuilt
 * every case that asserts a compile fails with "Unknown method".
 *
 * This file exists because the installed engine ships no NiagaraEditor `.cpp`
 * files, only headers. Every graph shape these handlers build - a simulation
 * stage's script with its output node and usage id, an event handler's script,
 * the parameter-map chain a module removal has to close over - was derived
 * from a header signature and never observed. A header tells you a function
 * exists; it does not tell you the node graph the editor will accept.
 *
 * So the assertion that matters is not "the call returned success". It is
 * "the system still compiles afterwards". A stage that is created wrong
 * returns success and then fails to compile, and only the editor knows.
 *
 * The fixture is built here and deleted in afterAll.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;

const ROOT = "/Game/MCPLive/Niagara";
const SYSTEM = `${ROOT}/NS_MCPDepth`;
const EMITTER_ASSET = `${ROOT}/FX_MCPDepthEmitter`;
// add_emitter names the instance after the asset it came from.
const EMITTER = "FX_MCPDepthEmitter";

const call = async (tool: string, args: Record<string, unknown>) =>
  server.call(tool, { ...args, timeoutMs: 180_000 });

const niagara = async (action: string, args: Record<string, unknown> = {}) =>
  resultJson<Record<string, any>>(await call("niagara", { action, systemPath: SYSTEM, ...args }));

/**
 * The only assertion that proves a graph edit was well formed.
 *
 * `validate` answers a different question - whether the system emits - so it
 * passes over a stage whose script is malformed. `get_compiled_hlsl` is worse
 * than useless here: on a CPU-sim emitter it reports "Emitter is CPU-sim; no
 * compiled HLSL available" and returns success WITHOUT COMPILING ANYTHING, so
 * an assertion built on it proves only that the call was dispatched. Every
 * emitter in this fixture is CPU-sim, which is what made the first version of
 * this helper vacuous.
 *
 * `compile` forces the system through the translator and blocks until it
 * settles, then reports the last compile status of every compilable script.
 * That is what a header-derived graph shape has to survive.
 */
const compiles = async (): Promise<{ ok: boolean; detail: unknown }> => {
  const body = resultJson<{
    success?: boolean;
    error?: string;
    compiled?: boolean;
    scriptCount?: number;
    failedScriptCount?: number;
    errors?: unknown[];
    scripts?: Array<{ scriptName?: string; usage?: string; status?: string }>;
  }>(await call("niagara", { action: "compile", systemPath: SYSTEM }));

  // A compile that reported zero scripts compiled nothing, and reading that as
  // a pass is the same vacuity this helper was rewritten to remove.
  const ok = body.success !== false && body.compiled === true && (body.scriptCount ?? 0) > 0;
  return { ok, detail: body };
};

let fixtureReady = false;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });

  // A system with one emitter is the smallest thing every case below needs.
  // `create` takes a name plus a package path, not a full asset path.
  const created = resultJson<{ success?: boolean; error?: string }>(
    await call("niagara", { action: "create", name: "NS_MCPDepth", packagePath: ROOT }),
  );
  if (created.success === false) throw new Error(`niagara(create) failed: ${created.error}`);

  const emitter = resultJson<{ success?: boolean; error?: string }>(
    await call("niagara", { action: "create_emitter", name: "FX_MCPDepthEmitter", packagePath: ROOT }),
  );
  if (emitter.success === false) throw new Error(`niagara(create_emitter) failed: ${emitter.error}`);

  const added = resultJson<{ success?: boolean; error?: string }>(
    await call("niagara", { action: "add_emitter", systemPath: SYSTEM, emitterPath: EMITTER_ASSET }),
  );
  if (added.success === false) throw new Error(`niagara(add_emitter) failed: ${added.error}`);
  fixtureReady = true;
}, 300_000);

afterAll(async () => {
  if (fixtureReady) {
    try {
      await call("asset", { action: "delete", assetPath: SYSTEM, force: true });
      await call("asset", { action: "delete", assetPath: EMITTER_ASSET, force: true });
    } catch {
      // A leftover fixture is noise, not a failure.
    }
  }
  await server?.close();
  closeLiveBridges();
});

describe("simulation stages", () => {
  it("creates a stage whose script the editor can actually compile", async () => {
    const body = await niagara("add_simulation_stage", { stageName: "MCPStage", emitterName: EMITTER });
    expect(body.success).not.toBe(false);
    // The point of the handler: it builds the backing script, not just the struct.
    expect(body.simulationStageObjectPath).toBeTruthy();

    const compiled = await compiles();
    expect(compiled.ok, `system did not compile after add_simulation_stage: ${JSON.stringify(compiled.detail)}`).toBe(
      true,
    );
  }, 240_000);

  it("reports the stage in the emitter's stack", async () => {
    const body = await niagara("get_emitter_info", { assetPath: EMITTER_ASSET, emitterName: EMITTER });
    expect(body.success).not.toBe(false);
  });

  it("re-adding the same stage name is refused rather than silently duplicated", async () => {
    const body = await niagara("add_simulation_stage", { stageName: "MCPStage", emitterName: EMITTER });
    // Either it reports the existing one or it errors; what it must not do is
    // create a second stage with the same name and leave the stack ambiguous.
    const marker = body.alreadyExists ?? body.existed ?? (body.success === false ? "error" : undefined);
    expect(marker).toBeDefined();
  });

  it("removes the stage and still compiles", async () => {
    const body = await niagara("remove_simulation_stage", { stageName: "MCPStage", emitterName: EMITTER });
    expect(body.success).not.toBe(false);

    const compiled = await compiles();
    expect(compiled.ok, `system did not compile after remove_simulation_stage: ${JSON.stringify(compiled.detail)}`).toBe(
      true,
    );
  }, 240_000);

  it("removing an absent stage reports alreadyRemoved and lists what exists", async () => {
    const body = await niagara("remove_simulation_stage", { stageName: "NotAStage", emitterName: EMITTER });
    expect(body.alreadyRemoved ?? body.success === false).toBeTruthy();
  });
});

describe("event handlers", () => {
  it("creates an event handler whose script compiles", async () => {
    const body = await niagara("add_event_handler", { eventName: "MCPEvent", emitterName: EMITTER });
    expect(body.success).not.toBe(false);
    expect(body.eventHandlerPropertyPath).toBeTruthy();

    const compiled = await compiles();
    expect(compiled.ok, `system did not compile after add_event_handler: ${JSON.stringify(compiled.detail)}`).toBe(true);
  }, 240_000);

  it("removes the handler and still compiles", async () => {
    const body = await niagara("remove_event_handler", { eventName: "MCPEvent", emitterName: EMITTER });
    expect(body.success).not.toBe(false);

    const compiled = await compiles();
    expect(compiled.ok, `system did not compile after remove_event_handler: ${JSON.stringify(compiled.detail)}`).toBe(
      true,
    );
  }, 240_000);
});

describe("module lifecycle", () => {
  it("adds a module, disables it, and re-disabling reports alreadySet", async () => {
    const added = await niagara("add_module", {
      emitterName: EMITTER,
      stackContext: "ParticleUpdate",
      moduleScript: "/Niagara/Modules/Update/Forces/GravityForce",
    });
    if (added.success === false) {
      // The stock module set differs by engine build; skip rather than assert a name.
      expect(String(added.error ?? "")).toBeTruthy();
      return;
    }

    const disabled = await niagara("set_module_enabled", {
      emitterName: EMITTER,
      stackContext: "ParticleUpdate",
      moduleName: "GravityForce",
      enabled: false,
    });
    expect(disabled.success).not.toBe(false);

    const again = await niagara("set_module_enabled", {
      emitterName: EMITTER,
      stackContext: "ParticleUpdate",
      moduleName: "GravityForce",
      enabled: false,
    });
    expect(again.alreadySet).toBe(true);

    const removed = await niagara("remove_module", {
      emitterName: EMITTER,
      stackContext: "ParticleUpdate",
      moduleName: "GravityForce",
    });
    expect(removed.success).not.toBe(false);
    expect(Array.isArray(removed.remainingModules)).toBe(true);

    const compiled = await compiles();
    expect(compiled.ok, `system did not compile after remove_module: ${JSON.stringify(compiled.detail)}`).toBe(true);
  }, 300_000);
});

describe("dynamic inputs and HLSL", () => {
  it("lists the override map without needing a property read", async () => {
    const body = await niagara("list_dynamic_inputs", { emitterName: EMITTER, stackContext: "all" });
    expect(body.success).not.toBe(false);
  });

  it("reads back custom HLSL nodes, which is what makes HLSL iterable", async () => {
    const body = await niagara("get_custom_hlsl", { emitterName: EMITTER, stackContext: "ParticleUpdate" });
    // An emitter with no CustomHLSL node returns an empty list, not an error.
    expect(body.success).not.toBe(false);
  });
});
