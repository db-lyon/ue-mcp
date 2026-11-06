/**
 * The mutation journal through a spawned daemon (#1481): a named group
 * journals each mutating call with its rollback descriptor or its checkpoint
 * snapshot, /v1/journal lists it, revert replays the inverses and restores the
 * files byte for byte, and the journal survives a daemon restart. The editor
 * is a fake bridge that answers like the plugin, rollback descriptors and the
 * checkpoint save recorder included.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

type Json = Record<string, any>;

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;
let dir: string;
let armed: string | null = null;
const actors = new Set<string>();
const variables = new Set<string>();

const file = (rel: string) => path.join(dir, rel);
const write = (rel: string, content: string) => {
  fs.mkdirSync(path.dirname(file(rel)), { recursive: true });
  fs.writeFileSync(file(rel), content);
};
const read = (rel: string) => fs.readFileSync(file(rel), "utf8");

/** What the bridge's pre-save hook records when armed, then the save itself. */
function editorSave(rel: string, content: string): void {
  if (!armed) return write(rel, content);
  const manifest = path.join(armed, "manifest.jsonl");
  const n = fs.existsSync(manifest) ? fs.readFileSync(manifest, "utf8").split("\n").filter(Boolean).length : 0;
  const existed = fs.existsSync(file(rel));
  if (existed) fs.copyFileSync(file(rel), path.join(armed, `${n}.bin`));
  fs.appendFileSync(manifest, `${JSON.stringify({ path: file(rel), existed, ...(existed ? { copy: `${n}.bin` } : {}) })}\n`);
  write(rel, content);
}

async function mcp(category: string, method: string, args: Json): Promise<{ isError: boolean; text: string }> {
  const server = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
  try {
    return await server.call("tools", { action: "call", category, method, args });
  } finally {
    await server.client.close();
  }
}

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Journal");
  dir = path.dirname(project);
  write("Content/BP_Door.uasset", "door v1");
  bridge = await FakeBridge.start({
    projectDir: dir,
    handlers: {
      checkpoint_arm: (p) => {
        armed = p.enabled === true ? String(p.pendingDir) : null;
        return { success: true, armed: p.enabled === true, dirtyPackages: [] };
      },
      checkpoint_prepare_restore: () => ({ success: true, closedEditors: [] }),
      checkpoint_finish_restore: (p) => ({ success: true, reloaded: ((p.files as Json[]) ?? []).map((f) => path.basename(String(f.file))), unloaded: [], configsReloaded: [], reopened: [] }),
      place_actor: (p) => {
        const label = String(p.label ?? "Actor");
        actors.add(label);
        return { success: true, actorLabel: label, created: true, rollback: { method: "delete_actor", payload: { actorLabel: label } } };
      },
      delete_actor: (p) => ({ success: actors.delete(String(p.actorLabel)) }),
      add_variable: (p) => {
        variables.add(String(p.name));
        return { success: true, created: true, rollback: { method: "remove_variable", payload: { assetPath: p.assetPath, name: p.name } } };
      },
      remove_variable: (p) => ({ success: variables.delete(String(p.name)) }),
      save_asset: () => {
        editorSave("Content/BP_Door.uasset", "door v2");
        return { success: true, saved: ["/Game/BP_Door"] };
      },
    },
  });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

describe("the mutation journal through the daemon", () => {
  it("journals a named group's mutating calls with a rollback or a snapshot", async () => {
    covers("daemon.journal");
    expect((await call(d, "GET", "/v1/health")).body.features).toContain("journal");
    expect((await call(d, "PUT", "/v1/checkpoints/enabled", { enabled: true })).status).toBe(200);
    const named = await call(d, "PUT", "/v1/journal/group", { group: "turn-1" });
    expect(named.status, JSON.stringify(named.body)).toBe(200);
    expect(named.body).toMatchObject({ group: "turn-1", checkpointsEnabled: true });

    for (const [category, method, args] of [
      ["level", "place_actor", { actorClass: "StaticMeshActor", label: "Crate" }],
      ["blueprint", "add_variable", { assetPath: "/Game/BP_Door", name: "Health", varType: "float" }],
      ["asset", "save", { assetPath: "/Game/BP_Door" }],
      ["project", "write_cpp_file", { path: "Journal/Door.h", content: "#pragma once\n" }],
      ["level", "get_actors", {}],
    ] as const) {
      const r = await mcp(category, method, args as Json);
      if (method !== "get_actors") expect(r.isError, `${method}: ${r.text}`).toBe(false);
    }
    expect(actors).toEqual(new Set(["Crate"]));
    expect(variables).toEqual(new Set(["Health"]));
    expect(read("Content/BP_Door.uasset")).toBe("door v2");
    expect(read("Source/Journal/Door.h")).toBe("#pragma once\n");

    const state = await call(d, "GET", "/v1/journal?group=turn-1");
    expect(state.status).toBe(200);
    const entries = state.body.entries as Json[];
    expect(entries.map((e) => [e.task, e.revertible])).toEqual([
      ["level.place_actor", true],
      ["blueprint.add_variable", true],
      ["asset.save", true],
      ["project.write_cpp_file", true],
    ]);
    expect(entries[0].rollback).toEqual({ method: "delete_actor", payload: { actorLabel: "Crate" } });
    expect(entries[2].snapshot.paths).toEqual(["Content/BP_Door.uasset"]);
    expect(entries[3].snapshot.paths).toEqual(["Source/Journal/Door.h"]);
    expect(fs.existsSync(file("Saved/UE_MCP_Bridge/journal.jsonl"))).toBe(true);
  }, 120_000);

  it("reverts the group after a daemon restart: inverses replayed, files byte-identical", async () => {
    covers("daemon.journal");
    await stopSandboxDaemons(sb);
    d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
    await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the restarted daemon to reach the fake editor");
    expect((await call(d, "PUT", "/v1/checkpoints/enabled", { enabled: true })).status).toBe(200);
    expect((await call(d, "GET", "/v1/journal")).body).toMatchObject({ group: null, groups: [{ group: "turn-1", entries: 4, reverted: 0 }] });

    const r = await call(d, "POST", "/v1/journal/revert", { group: "turn-1" });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    expect((r.body.results as Json[]).map((x) => [x.task, x.status, x.via])).toEqual([
      ["project.write_cpp_file", "reverted", "snapshot"],
      ["asset.save", "reverted", "snapshot"],
      ["blueprint.add_variable", "reverted", "rollback"],
      ["level.place_actor", "reverted", "rollback"],
    ]);
    expect(actors.size).toBe(0);
    expect(variables.size).toBe(0);
    expect(read("Content/BP_Door.uasset")).toBe("door v1");
    expect(fs.existsSync(file("Source/Journal/Door.h"))).toBe(false);
    expect((await call(d, "GET", "/v1/journal")).body.groups[0]).toMatchObject({ reverted: 4 });
  }, 120_000);

  it("reports a call with no rollback and no snapshot as not revertible, and reverts later groups", async () => {
    covers("daemon.journal");
    expect((await call(d, "PUT", "/v1/checkpoints/enabled", { enabled: false })).status).toBe(200);
    expect((await call(d, "PUT", "/v1/journal/group", { group: "turn-2" })).status).toBe(200);
    const saved = await mcp("asset", "save", { assetPath: "/Game/BP_Door" });
    expect(saved.isError, saved.text).toBe(false);
    expect((await call(d, "PUT", "/v1/journal/group", { group: "turn-3" })).status).toBe(200);
    const placed = await mcp("level", "place_actor", { actorClass: "StaticMeshActor", label: "Barrel" });
    expect(placed.isError, placed.text).toBe(false);
    expect((await call(d, "PUT", "/v1/journal/group", { group: null })).body.group).toBeNull();

    const r = await call(d, "POST", "/v1/journal/revert-after", { group: "turn-1" });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    expect(r.body.groups).toEqual(["turn-2", "turn-3"]);
    expect((r.body.results as Json[]).map((x) => [x.group, x.status])).toEqual([["turn-3", "reverted"], ["turn-2", "not-revertible"]]);
    expect(r.body.results[1].reason).toMatch(/checkpoints are off/);
    expect(actors.size).toBe(0);

    expect((await call(d, "POST", "/v1/journal/revert", { group: "nope" })).status).toBe(404);
    expect((await call(d, "PUT", "/v1/journal/group", { group: 3 })).status).toBe(400);
  }, 120_000);
});
