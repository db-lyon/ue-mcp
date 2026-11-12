/**
 * `ue-mcp projects`, spawned: the machine report read from Unreal's recent
 * lists and the user's own list, with tooling copies left out, and add and
 * remove changing what the next report lists.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { PACKAGE_VERSION, plain, runCli, Sandbox } from "./harness.js";

let sb: Sandbox;
let env: Record<string, string>;
let found: string;
let added: string;
let shard: string;
let temp: string;

function recent(version: string, entries: Array<[string, string]>): void {
  const dir = sb.path("unreal", version, "Saved", "Config", "WindowsEditor");
  fs.mkdirSync(dir, { recursive: true });
  const lines = entries.map(([p, at]) => `RecentlyOpenedProjectFiles=(ProjectName="${p.replace(/\\/g, "/")}",LastOpenTime=${at})`);
  fs.writeFileSync(path.join(dir, "EditorSettings.ini"), `[/Script/UnrealEd.EditorSettings]\n${lines.join("\n")}\n`);
}

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-projects-");
  found = sb.makeProject("Found", { config: "ue-mcp:\n  version: 1\n" });
  added = sb.makeProject("Added", { engine: "5.7" });
  fs.mkdirSync(sb.path("repo", "tests", ".shards", "s1", "Shard"), { recursive: true });
  shard = sb.path("repo", "tests", ".shards", "s1", "Shard", "Shard.uproject");
  fs.writeFileSync(shard, JSON.stringify({ EngineAssociation: "5.6" }));
  fs.mkdirSync(sb.path("tmp", "Scratch"), { recursive: true });
  temp = sb.path("tmp", "Scratch", "Scratch.uproject");
  fs.writeFileSync(temp, JSON.stringify({ EngineAssociation: "5.6" }));
  recent("5.6", [[found, "2026.11.01-10.00.00"], [shard, "2026.11.02-10.00.00"], [sb.path("Gone", "Gone.uproject"), "2026.11.03-10.00.00"]]);
  recent("5.7", [[temp, "2026.11.04-10.00.00"], [found, "2026.10.01-10.00.00"]]);
  env = { UE_MCP_RECENT_PROJECTS_DIR: sb.path("unreal"), TMP: sb.path("tmp"), TEMP: sb.path("tmp") };
  const deployed = await runCli(sb, ["deploy", found]);
  expect(deployed.code, deployed.stdout + deployed.stderr).toBe(0);
  // A live daemon record: this test's own process stands in for the daemon's.
  const daemons = path.join(sb.home, ".ue-mcp", "daemons");
  fs.mkdirSync(daemons, { recursive: true });
  const live = await runCli(sb, ["projects", "--json"], { env });
  const foundKey = (JSON.parse(live.stdout).projects as Array<{ key: string; name: string }>).find((p) => p.name === "Found")!.key;
  fs.writeFileSync(path.join(daemons, `${foundKey}.json`), JSON.stringify({
    pid: process.pid, port: 1, token: "f".repeat(64), version: PACKAGE_VERSION, apiVersion: 1,
    projectRoot: path.dirname(found).replace(/\\/g, "/").toLowerCase(), startedAt: "2026-11-08T00:00:00.000Z",
  }));
});
afterAll(() => sb.cleanup());

describe("projects", () => {
  it("--json lists recent projects once each, leaves tooling copies out, and reports each one's state and daemon", async () => {
    covers("cli.projects.json");
    const r = await runCli(sb, ["projects", "--json"], { env });
    expect(r.code, r.stderr).toBe(0);
    const report = JSON.parse(r.stdout);
    expect(report).toMatchObject({ version: 1, ueMcp: PACKAGE_VERSION, daemonDir: path.join(sb.home, ".ue-mcp", "daemons") });
    expect(report.projects.map((p: { name: string }) => p.name)).toEqual(["Found"]);
    const p = report.projects[0];
    expect(p).toMatchObject({
      uproject: found,
      source: "recent",
      lastOpened: "2026.11.01-10.00.00",
      engine: { association: "5.6", version: "5.6" },
      editor: { running: false, pids: [] },
      ueMcp: { setUp: true, bridge: { version: PACKAGE_VERSION, kind: "source" }, config: true },
      extensions: {},
    });
    expect(p.plugins).toContainEqual({ name: "UE_MCP_Bridge", version: expect.any(String) });
    expect(p.daemon.running).toMatchObject({ pid: process.pid, port: 1, uproject: found, version: PACKAGE_VERSION });
    expect(report.daemons).toHaveLength(1);
    expect(report.daemons[0].log).toBe(path.join(sb.home, ".ue-mcp", "daemons", `${p.key}.log`));
    // The daemon's token is never part of the report.
    expect(r.stdout).not.toContain("f".repeat(64));
  });

  it("add puts a project on the list and remove takes it off, hiding a found one", async () => {
    covers("cli.projects.add");
    covers("cli.projects.remove");
    const a = await runCli(sb, ["projects", "add", added, "--json"], { env });
    expect(JSON.parse(a.stdout)).toEqual({ ok: true, added });
    let names = JSON.parse((await runCli(sb, ["projects", "--json"], { env })).stdout).projects.map((p: { name: string; source: string }) => `${p.name}:${p.source}`);
    expect(names).toEqual(["Added:added", "Found:recent"]);

    const rm = await runCli(sb, ["projects", "remove", found, "--json"], { env });
    expect(JSON.parse(rm.stdout)).toEqual({ ok: true, removed: found });
    await runCli(sb, ["projects", "remove", added], { env });
    names = JSON.parse((await runCli(sb, ["projects", "--json"], { env })).stdout).projects.map((p: { name: string }) => p.name);
    expect(names).toEqual([]);

    // Adding a hidden project shows it again; a path that is no .uproject is refused.
    await runCli(sb, ["projects", "add", found], { env });
    names = JSON.parse((await runCli(sb, ["projects", "--json"], { env })).stdout).projects.map((p: { name: string }) => p.name);
    expect(names).toEqual(["Found"]);
    const bad = await runCli(sb, ["projects", "add", sb.path("nothing.uproject"), "--json"], { env });
    expect(bad.code).toBe(1);
    expect(JSON.parse(bad.stdout)).toEqual({ ok: false, error: expect.stringContaining("is not a .uproject file") });
  });

  it("prints projects for people, and usage for a wrong command line", async () => {
    covers("cli.projects");
    const r = await runCli(sb, ["projects"], { env });
    expect(r.code).toBe(0);
    expect(plain(r.stdout)).toContain(`Found  UE 5.6  ue-mcp ${PACKAGE_VERSION}  daemon pid ${process.pid}`);
    const wrong = await runCli(sb, ["projects", "frobnicate"], { env });
    expect(wrong.code).toBe(2);
    expect(plain(wrong.stderr + wrong.stdout)).toContain("usage: ue-mcp projects");
  });
});
