import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import yaml from "js-yaml";
import { BaseTask, TaskRegistry, type TaskConstructor, type TaskResult } from "@db-lyon/flowkit";
import { createLiveGuardSource } from "../../src/flow/guard-config.js";
import { buildGuards, type GuardSource } from "../../src/flow/guards.js";
import { GuardsSchema, type GuardDeclarations } from "../../src/flow/guard-schema.js";
import { GuardRegistry } from "../../src/flow/guard.js";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { loadFlowConfig } from "../../src/flow/loader.js";
import { GUARD_CONFIG_KEY } from "../../src/guard-task.js";
import type { IBridge } from "../../src/bridge.js";
import type { ToolContext } from "../../src/types.js";

// Keep real filesystem operations, while allowing deterministic read failures
// and file changes during a read on both Windows and Unix.
vi.mock("node:fs", async (importOriginal) => ({ ...await importOriginal<typeof import("node:fs")>() }));

let dir: string;
let globalFile: string;
let projectFile: string;
let clock: number;
let seen: Record<string, unknown>[];
let warnings: string[];

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-guard-config-"));
  globalFile = path.join(dir, "global.yml");
  projectFile = path.join(dir, "ue-mcp.yml");
  vi.stubEnv("UE_MCP_GLOBAL_CONFIG", globalFile);
  vi.stubEnv("UE_MCP_ENV", "");
  clock = Date.now();
  vi.spyOn(Date, "now").mockImplementation(() => clock);
  seen = [];
  warnings = [];
});

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllEnvs();
  fs.rmSync(dir, { recursive: true, force: true });
});

// Explicit timestamps avoid depending on the filesystem's timestamp resolution.
function write(file: string, document: unknown): void {
  fs.writeFileSync(file, typeof document === "string" ? document : yaml.dump(document));
  const modified = new Date(clock += 1000);
  fs.utimesSync(file, modified, modified);
}

function config(options: Record<string, unknown>, after = false) {
  return {
    guards: {
      policy: {
        before: { class_path: "check", options },
        ...(after ? { after: { class_path: "check", options: { owner: "after" } } } : {}),
      },
    },
  };
}

const liveSource = () => createLiveGuardSource([], dir, undefined, (message) => warnings.push(message));

/** Real task construction and guard dispatch, with only the editor replaced. */
async function pipeline(sources: Array<GuardSource & { guards: GuardDeclarations }>) {
  const registry = new TaskRegistry();
  registry.register("check", class extends BaseTask {
    get taskName() { return "check"; }
    async execute(): Promise<TaskResult> {
      const options = this.options[GUARD_CONFIG_KEY] as Record<string, unknown>;
      seen.push(options);
      return options.deny ? { success: false, error: new Error("policy denied") } : { success: true };
    }
  } as unknown as TaskConstructor);
  const call = vi.fn(async () => ({ ok: true }));
  const target = { projectPath: null, port: 0, portSource: "default" as const, verified: true };
  const raw: IBridge = {
    call, isConnected: true, connect: async () => {},
    getTarget: () => target, retargetProject: () => target,
  };
  const deps = { registry, rawBridge: raw, ctx: { bridge: raw, project: {} } as ToolContext };
  const guards = new GuardRegistry();
  for (const source of sources) {
    for (const guard of await buildGuards(source.guards, deps, source)) guards.register(guard);
  }
  const bridge = new GuardedBridge(raw, guards, () => null);
  return { invoke: () => { clock += 300; return bridge.call("spawn_actor", {}); }, call };
}

describe("guard config from files", () => {
  it("reloads YAML options without overriding a same-name plugin guard", async () => {
    write(projectFile, config({ owner: "yaml", version: 1 }));
    const plugin = {
      label: "plugin",
      guards: GuardsSchema.parse(config({ owner: "plugin", version: 0 }).guards),
    };
    const { invoke } = await pipeline([plugin, liveSource()]);
    await invoke();
    write(projectFile, config({ owner: "yaml", version: 2 }));
    await invoke();
    write(projectFile, {});
    await invoke();
    expect(seen).toEqual([
      { owner: "plugin", version: 0 }, { owner: "yaml", version: 1 },
      { owner: "plugin", version: 0 }, { owner: "yaml", version: 2 },
      { owner: "plugin", version: 0 }, { owner: "yaml", version: 2 },
    ]);
  });

  it("keeps removed hooks and guards until restart, and accepts restored options", async () => {
    write(projectFile, config({ owner: "before" }, true));
    const { invoke } = await pipeline([liveSource()]);
    await invoke();
    write(projectFile, { guards: { policy: { after: { class_path: "check", options: { owner: "after" } } } } });
    await invoke();
    write(projectFile, {});
    await invoke();
    write(projectFile, config({ owner: "restored" }, true));
    await invoke();
    expect(seen.map((options) => options.owner)).toEqual(["before", "after", "before", "after", "before", "after", "restored", "after"]);
    expect(warnings[0]).toContain("restart ue-mcp");
  });

  it("keeps the last valid options while project or global layers disappear", async () => {
    write(globalFile, config({ owner: "global" }));
    write(projectFile, config({ owner: "project" }));
    const { invoke } = await pipeline([liveSource()]);
    await invoke();
    fs.unlinkSync(projectFile);
    await invoke();
    write(globalFile, config({ owner: "global-edited" }));
    await invoke();
    fs.unlinkSync(globalFile);
    await invoke();
    write(globalFile, config({ owner: "global-restored" }));
    await invoke();
    write(projectFile, config({ owner: "project-restored" }));
    await invoke();
    expect(seen.map((options) => options.owner)).toEqual([
      "project", "project", "project", "project", "project", "project-restored",
    ]);
  });

  it("uses flow config merging and defaults, including changing environment and local overlays", async () => {
    vi.stubEnv("UE_MCP_ENV", "test");
    const envFile = path.join(dir, "ue-mcp.test.yml");
    const localFile = path.join(dir, "ue-mcp.local.yml");
    const contributions = { tasks: { custom: { class_path: "check" } } };
    write(globalFile, config({ enabled: true, nested: { global: 1, winner: "global" }, paths: ["global"] }));
    write(projectFile, {
      tasks: { custom: { options: { enabled: true } } }, // Inherits the plugin's class_path.
      guards: { policy: { before: { options: { nested: { project: 2 }, paths: ["project"] } } } },
    });
    write(envFile, { guards: { policy: { before: { options: { nested: { winner: "env" } } } } } });
    write(localFile, { guards: { policy: { before: { options: { enabled: false } } } } });
    const source = createLiveGuardSource([], dir, contributions, (message) => warnings.push(message));
    const expected = () => loadFlowConfig([], dir, contributions).config.guards;
    expect(source.guards).toEqual(expected());
    const { invoke } = await pipeline([source]);
    await invoke();
    expect(seen.at(-1)).toEqual({ enabled: false, nested: { global: 1, project: 2, winner: "env" }, paths: ["project"] });
    write(globalFile, config({ enabled: true, nested: { global: 3, winner: "global" }, paths: ["global"] }));
    await invoke();
    expect(seen.at(-1)).toEqual(expected().policy.before!.options);
    expect(seen.at(-1)).toMatchObject({ nested: { global: 3 } });
    write(envFile, { guards: { policy: { before: { options: { nested: { winner: "env-edited" } } } } } });
    fs.unlinkSync(localFile);
    await invoke();
    expect(seen.at(-1)).toMatchObject({ enabled: false, nested: { winner: "env" } });
    write(localFile, { guards: { policy: { before: { options: { enabled: false } } } } });
    await invoke();
    expect(seen.at(-1)?.enabled).toBe(false);
    expect(seen.at(-1)).toEqual(expected().policy.before!.options);
    fs.unlinkSync(projectFile);
    await invoke();
    expect(seen.at(-1)).toMatchObject({ enabled: false, nested: { winner: "env-edited" } });
  });

  it.each(["global", "project"])("keeps a denying guard on invalid %s YAML and removal", async (layer) => {
    const file = layer === "global" ? globalFile : projectFile;
    write(file, config({ deny: true }));
    const { invoke, call } = await pipeline([liveSource()]);
    // Break the file before the first call: the startup snapshot is last good.
    write(file, "guards: [broken");
    await expect(invoke()).rejects.toThrow("policy denied");
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(warnings).toHaveLength(1);
    write(file, { guards: { policy: { before: { options: { deny: false } } } } });
    await expect(invoke()).rejects.toThrow("policy denied");
    write(file, { guards: {}, tasks: null });
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(call).not.toHaveBeenCalled();
    write(file, {});
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(call).not.toHaveBeenCalled();
    write(file, config({ deny: false }));
    await expect(invoke()).resolves.toEqual({ ok: true });
  });

  it("keeps a denying guard on filesystem errors and retries the same changed file after recovery", async () => {
    write(globalFile, config({ deny: true }));
    const { invoke, call } = await pipeline([liveSource()]);
    fs.unlinkSync(globalFile);
    fs.mkdirSync(globalFile);
    await expect(invoke()).rejects.toThrow("policy denied");
    fs.rmdirSync(globalFile);
    write(globalFile, config({ deny: false }));
    const read = fs.readFileSync;
    const blockedRead = vi.spyOn(fs, "readFileSync").mockImplementation((...args: Parameters<typeof read>) => {
      if (args[0] === globalFile) throw Object.assign(new Error("access denied"), { code: "EACCES" });
      return read(...args);
    });
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(call).not.toHaveBeenCalled();
    blockedRead.mockRestore();
    clock += 5000;
    await expect(invoke()).resolves.toEqual({ ok: true });
    expect(warnings).toHaveLength(2);
  });

  it("rejects invalid project startup config and names its file", () => {
    write(projectFile, "guards: [broken");
    expect(liveSource).toThrow(projectFile);
    write(projectFile, { guards: { policy: { scope: "writes" } } });
    expect(liveSource).toThrow();
    expect(liveSource).toThrow(/a guard needs/);
  });

  it.each(["created", "replaced"])("retries startup if a config file is %s during the initial read", async (change) => {
    write(globalFile, {});
    const read = fs.readFileSync;
    let changed = false;
    const changingRead = vi.spyOn(fs, "readFileSync").mockImplementation((...args: Parameters<typeof read>) => {
      const contents = read(...args);
      if (args[0] === globalFile && !changed) {
        changed = true;
        write(change === "created" ? projectFile : globalFile, config({ deny: true }));
      }
      return contents;
    });
    // The first read observes no guards. Publishing it would leave no hooks
    // registered to notice the guard that appeared while that read was in flight.
    const source = liveSource();
    changingRead.mockRestore();
    const { invoke, call } = await pipeline([source]);
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(call).not.toHaveBeenCalled();
  });

  it.each(["malformed", "unreadable", "directory"])("warns and ignores a %s global config at startup", async (kind) => {
    write(projectFile, config({ deny: true }));
    if (kind === "directory") fs.mkdirSync(globalFile);
    else write(globalFile, kind === "malformed" ? "guards: [broken" : config({ deny: false }));
    if (kind === "unreadable") {
      const read = fs.readFileSync;
      vi.spyOn(fs, "readFileSync").mockImplementation((...args: Parameters<typeof read>) => {
        if (args[0] === globalFile) throw Object.assign(new Error("access denied"), { code: "EACCES" });
        return read(...args);
      });
    }
    const source = liveSource();
    expect(source.guards).toEqual(loadFlowConfig([], dir).config.guards);
    expect(warnings).toHaveLength(1);
    expect(warnings[0]).toContain(globalFile);
    const { invoke, call } = await pipeline([source]);
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(call).not.toHaveBeenCalled();
  });

  it("retains options when a broken global layer would reveal a weaker project guard", async () => {
    write(globalFile, config({ deny: true }));
    write(projectFile, { guards: { policy: { before: { class_path: "check" } } } });
    const { invoke, call } = await pipeline([liveSource()]);
    write(globalFile, "guards: [broken");
    await expect(invoke()).rejects.toThrow("policy denied");
    expect(call).not.toHaveBeenCalled();
  });

  it("uses the last successful startup snapshot if saves keep racing its reads", async () => {
    write(globalFile, config({ deny: true }));
    const read = fs.readFileSync;
    const changingRead = vi.spyOn(fs, "readFileSync").mockImplementation((...args: Parameters<typeof read>) => {
      const contents = read(...args);
      if (args[0] === globalFile) write(globalFile, config({ deny: true }));
      return contents;
    });
    const source = liveSource();
    changingRead.mockRestore();
    expect(warnings).toHaveLength(1);
    expect(warnings[0]).toContain("last valid config");
    const { invoke } = await pipeline([source]);
    await expect(invoke()).rejects.toThrow("policy denied");
  });

  it("keeps a startup snapshot when its layer disappears during the read", async () => {
    write(globalFile, config({ deny: true }));
    const read = fs.readFileSync;
    const disappearingRead = vi.spyOn(fs, "readFileSync").mockImplementation((...args: Parameters<typeof read>) => {
      const contents = read(...args);
      if (args[0] === globalFile) fs.unlinkSync(globalFile);
      return contents;
    });
    const source = liveSource();
    disappearingRead.mockRestore();
    expect(warnings[0]).toContain("disappeared during startup");
    const { invoke } = await pipeline([source]);
    await expect(invoke()).rejects.toThrow("policy denied");
  });

  it.each(["YAML", "guard declaration"])("keeps a startup snapshot when a concurrent edit becomes invalid %s", async (kind) => {
    write(globalFile, config({ deny: true }));
    const read = fs.readFileSync;
    let changed = false;
    const changingRead = vi.spyOn(fs, "readFileSync").mockImplementation((...args: Parameters<typeof read>) => {
      const contents = read(...args);
      if (args[0] === globalFile && !changed) {
        changed = true;
        write(globalFile, kind === "YAML" ? "guards: [broken" : { guards: { policy: { scope: "all" } } });
      }
      return contents;
    });
    const source = liveSource();
    changingRead.mockRestore();
    expect(warnings[0]).toContain("last valid config");
    const { invoke } = await pipeline([source]);
    await expect(invoke()).rejects.toThrow("policy denied");
  });

  it("shares metadata polling across hooks and caches unchanged malformed input", () => {
    write(globalFile, config({ deny: true }, true));
    const source = liveSource();
    const stat = vi.spyOn(fs, "statSync");
    const read = vi.spyOn(fs, "readFileSync");
    for (let i = 0; i < 20; i++) {
      source.liveOptions!("policy", "before");
      source.liveOptions!("policy", "after");
    }
    expect(stat).toHaveBeenCalledTimes(2);
    expect(read).not.toHaveBeenCalled();
    write(globalFile, "guards: [broken");
    expect(source.liveOptions!("policy", "before")).toEqual({ deny: true });
    const reads = read.mock.calls.length;
    expect(reads).toBeGreaterThan(0);
    for (let i = 0; i < 20; i++) {
      clock += 1000;
      expect(source.liveOptions!("policy", "before")).toEqual({ deny: true });
    }
    expect(read).toHaveBeenCalledTimes(reads);
    expect(warnings).toHaveLength(1);
    write(globalFile, config({ deny: false }, true));
    expect(source.liveOptions!("policy", "before")).toEqual({ deny: false });
  });

  it.each(["class_path", "scope", "order", "hook", "guard"])("requires restart for a new %s while retaining the last valid options", async (change) => {
    write(projectFile, config({ version: 1 }));
    const { invoke } = await pipeline([liveSource()]);
    write(projectFile, config({ version: 2 }));
    await invoke();
    const changed: any = config({ version: 3 });
    if (change === "class_path") changed.guards.policy.before.class_path = "different";
    if (change === "scope") changed.guards.policy.scope = "writes";
    if (change === "order") changed.guards.policy.order = 1;
    if (change === "hook") changed.guards.policy.after = { class_path: "check" };
    if (change === "guard") changed.guards.extra = { before: { class_path: "check" } };
    write(projectFile, changed);
    await invoke();
    expect(seen.map((options) => options.version)).toEqual([2, 2]);
    expect(warnings).toHaveLength(1);
    expect(warnings[0]).toContain("restart ue-mcp");
  });
});
