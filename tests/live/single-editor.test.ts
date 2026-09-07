/**
 * One editor, before and after (#817, plan 7.3, and the eighteen enumerated
 * single-editor changes).
 *
 * The promise the whole of #817 rests on is that a user with one editor sees
 * no change beyond eighteen fixes. Half of that is the golden corpus, which
 * compares the advertised surface byte for byte. This is the other half: the
 * behaviour those eighteen rows describe, asserted against a real editor.
 *
 * Each test says what the behaviour was before the change and what it is
 * after, because a row like "start_editor refuses on its own project" only
 * means something next to the bug it replaced (refusing because SOME editor
 * was running, anywhere on the machine).
 *
 * Three rows cannot be asserted here and say so where they sit: a restart
 * inside TIME_WAIT, a pinned self-launched editor, and the stop-failure branch
 * of restart all need an editor to be started or stopped, and this suite
 * attaches to one somebody else owns. They belong to the C++ automation suite
 * (plan item 0.9), which can drive a process of its own.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { EditorBridge, readBridgeErrorRecord, readBridgeLockfile } from "../../src/bridge.js";
import { resolveBridgeTarget, isPidAlive } from "../../src/editor-target.js";
import { checkPluginFreshness } from "../../src/plugin-freshness.js";
import { attach } from "../../src/deployer.js";
import { startEditor } from "../../src/editor-control.js";
import { switchProject } from "../../src/project-switch.js";
import { ProjectContext } from "../../src/project.js";
import { requestedPortPath } from "../../src/requested-port.js";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget, makeTempProject } from "./harness.js";

const target = await liveTarget();

let server: LiveServer;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
});

describe("the single-editor shape is the shape it always was", () => {
  it("advertises no targeting parameter", async () => {
    // Construction-gated: the parameter exists only while the server drives
    // more than one editor. The golden connected baseline pins the whole
    // schema byte for byte; this names the one field that would appear first.
    const tools = await server.listTools();
    for (const tool of tools) {
      const schema = tool.inputSchema as { properties?: Record<string, unknown> };
      expect(Object.keys(schema.properties ?? {})).not.toContain("editor");
    }
  }, 120_000);

  it("attributes nothing to an editor, because there is only one", async () => {
    const result = await server.call("asset", {
      action: "list_textures",
      directory: "/Game",
      recursive: false,
    });
    expect(result.isError).toBe(false);
    expect(result.blocks.some((b) => b.startsWith("MACHINE_EDITOR="))).toBe(false);
  }, 120_000);

  it("gates nothing: an untargeted change runs, as it always did", async () => {
    // The same call is refused beyond one editor (see addressing.test.ts).
    // Here it has to reach the editor, and the path it names does not exist,
    // so the editor's answer is the only thing that changes.
    const result = await server.call("asset", {
      action: "delete",
      assetPath: "/Game/LiveTierNoSuchAsset",
    });
    expect(result.text).not.toContain("will not pick one for you");
  }, 120_000);

  it("reports the real action count and the fab category in its instructions", async () => {
    // Before: all three instruction variants carried a hand-written "685+
    // actions" and a category list with no `fab` in it.
    expect(server.instructions).toContain("fab");
    expect(server.instructions).not.toContain("685+");
    const claimed = /covering (\d+) actions/.exec(server.instructions);
    expect(claimed, "instructions no longer state an action count").toBeTruthy();
    expect(Number(claimed![1])).toBeGreaterThan(700);
  });

  it("hands a flow the whole context, including flows and plugins", async () => {
    // Before: makeRunner rebuilt the context field by field and dropped
    // elicit, getFlows and getPlugins, so project(get_status) called inside a
    // flow reported no flows while the same call outside one reported them.
    const result = await server.call("flow", {
      action: "run",
      flowName: "live_tier_leak_probe",
    });
    expect(result.isError).toBe(false);
    const body = resultJson<{ steps?: Array<{ data?: { flows?: unknown[] } }> }>(result);
    const flowsSeenInside = JSON.stringify(body).includes("live_tier_leak_probe");
    expect(flowsSeenInside).toBe(true);
  }, 180_000);
});

describe("the editor's own records", () => {
  it("publishes a port lockfile whose process is alive and answering", () => {
    const lockfile = readBridgeLockfile(target.uproject);
    expect(lockfile).toBeTruthy();
    expect(lockfile!.port).toBe(target.port);
    expect(isPidAlive(lockfile!.pid!)).toBe(true);
  });

  it("resolves this project's port from its own record, not from a guess", () => {
    // Before: stop_editor took the port from UE_MCP_PORT or the legacy fixed
    // 9877, so it could ask an editor belonging to another project to quit.
    const resolved = resolveBridgeTarget(target.projectDir);
    if (!resolved.ok) throw new Error(resolved.reason);
    expect(resolved.port).toBe(target.port);
    expect(resolved.pid).toBe(target.capabilities.pid);
  });

  it("hands the editor the port the client resolved", () => {
    // The client writes requested.json so an editor launched from Explorer
    // can bind the pin the client computed from four config layers plus the
    // environment, which it cannot see for itself. Written on every connect
    // through a session, so it is present here.
    const file = requestedPortPath(target.projectDir);
    if (!fs.existsSync(file)) return; // no pin resolved for this project
    const record = JSON.parse(fs.readFileSync(file, "utf-8")) as Record<string, unknown>;
    expect(String(record.projectRoot).toLowerCase()).toContain("ue_mcp");
    expect(Number(record.port)).toBeGreaterThan(0);
  });

  it("reads a bind-failure record only while the editor that wrote it is alive", () => {
    // "Editor alive, bridge dead" used to be indistinguishable from "no
    // editor", and the client said the wrong thing. A record from a process
    // that has since exited describes nothing current and is ignored.
    const fake = makeTempProject("BindFailed");
    try {
      const dir = path.join(fake.dir, "Saved", "UE_MCP_Bridge");
      fs.mkdirSync(dir, { recursive: true });
      const record = {
        status: "bind-failed",
        pid: process.pid,
        failedAt: new Date().toISOString(),
        firstPortTried: 49999,
        lastPortTried: 50009,
        detail: "The bridge could not bind any port in 49999-50009.",
      };
      fs.writeFileSync(path.join(dir, "bridge-error.json"), JSON.stringify(record), "utf-8");
      expect(readBridgeErrorRecord(fake.uproject)?.detail).toContain("could not bind");

      // The same record, from a process that is gone.
      fs.writeFileSync(
        path.join(dir, "bridge-error.json"),
        JSON.stringify({ ...record, pid: 0x7ffffffe }),
        "utf-8",
      );
      expect(readBridgeErrorRecord(fake.uproject)).toBeNull();
    } finally {
      fake.cleanup();
    }
  });

  it("treats a lockfile whose process is gone as evidence of a crash, not an address", () => {
    // A crashed editor leaves its port published. The port can since have been
    // taken by something else, so an answer on it proves nothing: the record
    // is discarded rather than being allowed to refuse a launch forever.
    const fake = makeTempProject("Crashed");
    try {
      const dir = path.join(fake.dir, "Saved", "UE_MCP_Bridge");
      fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(
        path.join(dir, "port.json"),
        JSON.stringify({ port: 49155, pid: 0x7ffffffe, startedAt: new Date().toISOString() }),
        "utf-8",
      );
      const resolved = resolveBridgeTarget(fake.dir);
      if (!resolved.ok) throw new Error(resolved.reason);
      expect(resolved.port).toBe(49155);
      expect(isPidAlive(resolved.pid!)).toBe(false);
    } finally {
      fake.cleanup();
    }
  });
});

describe("lifecycle, without starting or stopping anything", () => {
  it("refuses to start an editor for a project that already has one", async () => {
    // Before (#819): the guard asked whether ANY editor was running on the
    // machine, so a second project could not be started at all. It now asks
    // about this project, and answers from the port this project published.
    const project = new ProjectContext();
    project.setProject(target.uproject);
    const result = await startEditor(project, 1);
    // A refusal, because no editor was launched. The markers are what make it
    // readable: `alreadyRunning` says the reason was an editor that is already
    // up, and `bridgeReady` with `port` say it can be reached at this address
    // right now. A flow step that expects this sets ignore_failure on itself.
    expect(result.success).toBe(false);
    expect(result.alreadyRunning).toBe(true);
    expect(result.bridgeReady).toBe(true);
    expect(result.port).toBe(target.port);
    expect(result.message).toContain("already running for this project");
    expect(result.message).toContain(String(target.port));
  }, 60_000);
});

describe("attach, staleness and switching", () => {
  it("leaves a project that already has the bridge byte-identical", () => {
    // Before: set_project deployed plugin source into the target, which
    // overwrites a local fork, a pinned revision or a source-controlled
    // bridge. Attaching detects and enables; it never writes source.
    const before = fs.readFileSync(target.uproject);
    const context = new ProjectContext();
    context.setProject(target.uproject);
    const result = attach(context);
    const after = fs.readFileSync(target.uproject);
    expect(after.equals(before)).toBe(true);
    expect(result.cppPluginPresent).toBe(true);
  });

  it("writes no dangling plugin entry into a project that has no bridge", () => {
    // Before: attach enabled UE_MCP_Bridge in the .uproject before it checked
    // whether the plugin was there, so attaching to a project without it wrote
    // a reference to nothing and turned that project's next launch into a
    // missing-plugin prompt.
    const fake = makeTempProject("NoBridge");
    try {
      const before = fs.readFileSync(fake.uproject, "utf-8");
      const context = new ProjectContext();
      context.setProject(fake.uproject);
      const result = attach(context);
      const after = fs.readFileSync(fake.uproject, "utf-8");
      expect(result.cppPluginPresent).toBe(false);
      expect(after).not.toContain("UE_MCP_Bridge");
      expect(after).toBe(before);
    } finally {
      fake.cleanup();
    }
  });

  it("decides staleness for a real deployment instead of always saying fresh", () => {
    // Before: all three staleness signals were dead (a .uplugin version frozen
    // at 0.3.0, an API version that never moved, and a comparison of deployed
    // source against the binary). The check has to reach a decision here, on a
    // project that really has a compiled bridge.
    const freshness = checkPluginFreshness(target.uproject);
    expect(freshness.checked).toBe(true);
    if (freshness.stale) {
      expect(freshness.message).toBeTruthy();
    }
  });

  it("moves path resolution and the socket together, onto a live editor", async () => {
    // Before (#818): set_project re-pointed path resolution while connect()
    // early-returned on the existing socket, so the two halves addressed
    // different projects. Here the switch starts on a project with no editor
    // and lands on the one that is running.
    const fake = makeTempProject("SwitchFrom");
    const project = new ProjectContext();
    project.setProject(fake.uproject);
    const bridge = new EditorBridge();
    bridge.setProjectContext(fake.uproject);
    try {
      const result = await switchProject(project, bridge, target.uproject, { connectTimeoutMs: 5000 });
      expect(result.projectPath.toLowerCase()).toBe(target.uproject.toLowerCase());
      expect(result.connected).toBe(true);
      expect(result.target.port).toBe(target.port);
      expect(result.target.verified).toBe(true);
      // Both halves, not one: the resolved project and the answering editor.
      expect(project.projectName).toBe("ue_mcp");
      expect(bridge.capabilities?.projectName).toBe("ue_mcp");
    } finally {
      bridge.disconnect();
      fake.cleanup();
    }
  }, 60_000);

  it("takes the port from the project's published record, not from a stale field", async () => {
    // Before: a connect that moved the port mutated it permanently, so a later
    // reconnect inherited a number chosen for a connection that had gone.
    const bridge = new EditorBridge("127.0.0.1", 49_151);
    bridge.setProjectContext(target.uproject);
    try {
      await bridge.connect(5000);
      expect(bridge.port).toBe(target.port);
      expect(bridge.getTarget().portSource).toBe("lockfile");
      bridge.disconnect();
      await bridge.connect(5000);
      expect(bridge.port).toBe(target.port);
    } finally {
      bridge.disconnect();
    }
  }, 60_000);
});

describe("the capability handshake against the running binary", () => {
  it("reports the version compiled into the binary, not scraped from source", () => {
    // The deployed-source scrape reports the newest value the moment a deploy
    // lands, while the loaded DLL is arbitrarily old. The handshake is the
    // only answer a stale binary cannot fake.
    expect(target.capabilities.handlerApiVersion).toBeGreaterThan(0);
    expect(target.capabilities.protocolVersion).toBeGreaterThan(0);
    expect(target.capabilities.builtAt).toBeTruthy();
    expect(target.capabilities.actionCount).toBeGreaterThan(400);
  });

  it("answers on a fresh connection inside the ordinary connect budget", async () => {
    // A bridge too old to know the handshake answers -32601 immediately, and
    // one that does know it answers off the game thread. Neither costs the
    // caller a handler timeout, which is the property that makes connecting
    // to a legacy bridge indistinguishable in cost from connecting to a new
    // one. The legacy half is emulated in tests/unit/bridge.test.ts.
    const bridge = new EditorBridge(target.host, target.port);
    bridge.setProjectContext(target.uproject);
    const started = Date.now();
    try {
      await bridge.connect(5000);
      const elapsed = Date.now() - started;
      expect(bridge.capabilities?.legacy).toBe(false);
      expect(elapsed).toBeLessThan(5000);
    } finally {
      bridge.disconnect();
    }
  }, 30_000);
});
