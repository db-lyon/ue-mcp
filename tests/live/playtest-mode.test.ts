// @live-editor headless
/**
 * Playtest mode manual against a real editor: the shipped server refuses an
 * MCP client's pie(arrangement_play) and pie(clip_record_start) before the
 * editor is called, and the rest of pie still answers from the editor.
 *
 * The server's user state file is this test's own, holding playtest mode
 * manual for the target project.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { normalizeProjectRoot } from "../../src/bridge/wire-contract.js";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

let server: LiveServer;
let stateDir: string;

beforeAll(async () => {
  const target = await liveTarget();
  stateDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-live-playtest-"));
  const statePath = path.join(stateDir, "state.json");
  fs.writeFileSync(statePath, JSON.stringify({
    projects: { [normalizeProjectRoot(path.resolve(target.projectDir))]: { playtest: { mode: "manual" } } },
  }));
  server = await LiveServer.start({ projects: [target.uproject], env: { UE_MCP_USER_STATE: statePath } });
}, 120_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
  fs.rmSync(stateDir, { recursive: true, force: true });
});

describe("playtest mode manual", () => {
  it("refuses arrangement_play and clip_record_start without starting a run", async () => {
    const play = { runId: "live-refused", arrangement: { id: "noop", rate: 60, tracks: [], checks: [], regions: [] }, clips: {} };
    const run = await server.call("pie", { action: "arrangement_play", args: play });
    expect(run.isError).toBe(true);
    expect(resultJson(run)).toMatchObject({ success: false, refusedMethod: "pie.arrangement_play", playtestMode: "manual" });
    const record = await server.call("pie", { action: "clip_record_start", args: { performer: { kind: "player", client: 0 }, kind: "player" } });
    expect(resultJson(record)).toMatchObject({ success: false, refusedMethod: "pie.clip_record_start" });
  });

  it("leaves the rest of pie answering from the editor", async () => {
    const listed = await server.call("pie", { action: "profile_list", args: {} });
    expect(listed.text).not.toContain("playtestMode");
  });
});
