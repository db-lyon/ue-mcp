/**
 * The daemon's pieces without a process: how a disconnect is classified, the
 * resumable event log, version ordering for handoff, and the discovery file.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { classifyDisconnect, ExpectedDisconnect } from "../../src/daemon/disconnect.js";
import { EventLog } from "../../src/daemon/events.js";
import { isOlder } from "../../src/daemon/daemon.js";
import { discoveryPath, readDiscovery, readLiveDiscovery, removeDiscovery, writeDiscovery } from "../../src/daemon/discovery.js";
import { normalizeProjectRoot } from "../../src/bridge/port.js";
import type { BridgeInstanceRecord } from "../../src/bridge/editor-target.js";

const record: BridgeInstanceRecord = {
  port: 50000, pid: 4242, instanceId: "x", state: "listening", startedAt: null,
  recordPath: "/p/Saved/UE_MCP_Bridge/instances/4242.json", writtenAtMs: 0,
  pluginDir: null, installKind: null, pluginVersion: null,
};

describe("classifyDisconnect", () => {
  it("names what the daemon announced first", () => {
    expect(classifyDisconnect({ record, expected: "restarting", isAlive: () => false, recordExists: () => true })).toBe("restarting");
    expect(classifyDisconnect({ record, expected: "rebuild", isAlive: () => true })).toBe("rebuild");
  });

  it("says unknown while the editor process is alive", () => {
    expect(classifyDisconnect({ record, expected: null, isAlive: () => true })).toBe("unknown");
    expect(classifyDisconnect({ record: null, expected: null })).toBe("unknown");
  });

  it("tells a crash from a clean exit by the record the bridge leaves behind", () => {
    expect(classifyDisconnect({ record, expected: null, isAlive: () => false, recordExists: () => true })).toBe("crashed");
    expect(classifyDisconnect({ record, expected: null, isAlive: () => false, recordExists: () => false })).toBe("closed");
  });

  it("forgets an announcement after its window", () => {
    const e = new ExpectedDisconnect();
    e.expect("restarting", -1);
    expect(e.get()).toBeNull();
    e.expect("rebuild");
    expect(e.get()).toBe("rebuild");
    e.clear();
    expect(e.get()).toBeNull();
  });
});

describe("EventLog", () => {
  it("numbers events and resumes after an id", () => {
    const log = new EventLog();
    log.publish("a");
    const b = log.publish("b", { x: 1 });
    log.publish("c");
    expect(log.since(b.id - 1).events.map((e) => e.type)).toEqual(["b", "c"]);
    expect(log.since(log.lastId).events).toEqual([]);
  });

  it("reports a gap when a reader fell behind the kept window", () => {
    const log = new EventLog(2);
    for (let i = 0; i < 5; i++) log.publish(`e${i}`);
    const r = log.since(1);
    expect(r.gap).toBe(true);
    expect(r.events.map((e) => e.type)).toEqual(["e3", "e4"]);
  });
});

describe("isOlder", () => {
  it("orders releases and prereleases", () => {
    expect(isOlder("1.3.9", "1.3.10")).toBe(true);
    expect(isOlder("1.3.10-beta.2", "1.3.10-beta.16")).toBe(true);
    expect(isOlder("1.3.10-beta.16", "1.3.10")).toBe(true);
    expect(isOlder("1.3.10", "1.3.10")).toBe(false);
    expect(isOlder("1.4.0", "1.3.10")).toBe(false);
  });
});

describe("discovery", () => {
  let dir: string;
  let saved: string | undefined;
  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-discovery-"));
    saved = process.env.UE_MCP_DAEMON_DIR;
    process.env.UE_MCP_DAEMON_DIR = dir;
  });
  afterEach(() => {
    if (saved === undefined) delete process.env.UE_MCP_DAEMON_DIR;
    else process.env.UE_MCP_DAEMON_DIR = saved;
    fs.rmSync(dir, { recursive: true, force: true });
  });

  const project = path.join(os.tmpdir(), "Some", "Project");
  const d = () => ({
    pid: process.pid, port: 1234, token: "t", version: "1.0.0", apiVersion: 1,
    projectRoot: normalizeProjectRoot(project), startedAt: "now",
  });

  it("round-trips, and is only live while its process is", () => {
    writeDiscovery(project, d());
    expect(readDiscovery(project)).toEqual(d());
    expect(readLiveDiscovery(project, () => true)?.port).toBe(1234);
    expect(readLiveDiscovery(project, () => false)).toBeNull();
  });

  it("ignores a record for another project root", () => {
    writeDiscovery(project, { ...d(), projectRoot: "c:/elsewhere" });
    expect(readDiscovery(project)).toBeNull();
  });

  it("is removed only by the process that owns it", () => {
    writeDiscovery(project, d());
    removeDiscovery(project, process.pid + 1);
    expect(fs.existsSync(discoveryPath(project))).toBe(true);
    removeDiscovery(project);
    expect(fs.existsSync(discoveryPath(project))).toBe(false);
  });
});
