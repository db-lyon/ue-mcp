/**
 * Commands that read or set one project's state, spawned: status, context and
 * the Claude Code hook handler.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { PACKAGE_VERSION, plain, runCli, Sandbox, snapshotTree, type CliResult } from "./harness.js";

let sb: Sandbox;
let installed: string;
let bare: string;
let empty: string;

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-project-");
  installed = sb.makeProject("Installed", { config: "ue-mcp:\n  version: 1\n" });
  bare = sb.makeProject("Bare");
  empty = sb.path("empty");
  fs.mkdirSync(empty, { recursive: true });
  const deployed = await runCli(sb, ["deploy", installed]);
  expect(deployed.code, deployed.stdout + deployed.stderr).toBe(0);
  // A Claude Code project entry pinned to this version.
  fs.writeFileSync(
    path.join(path.dirname(installed), ".mcp.json"),
    JSON.stringify({ mcpServers: { "ue-mcp": { command: "npx", args: ["-y", `ue-mcp@${PACKAGE_VERSION}`, installed.replace(/\\/g, "/")] } } }),
  );
});
afterAll(() => sb.cleanup());

describe("status", () => {
  let results: CliResult[];
  let before: Record<string, string>;
  beforeAll(async () => {
    before = snapshotTree(sb.root);
    results = await Promise.all([
      runCli(sb, ["status", installed, "--json"]),
      runCli(sb, ["status", "--json"], { cwd: path.dirname(bare) }),
      runCli(sb, ["status", "--json"], { cwd: empty }),
      runCli(sb, ["status", installed]),
      runCli(sb, ["status"], { cwd: path.dirname(bare) }),
      runCli(sb, ["status"], { cwd: empty }),
    ]);
  });

  it("--json prints one report with the bridge, clients and editor state", () => {
    covers("cli.status.json");
    const [json, bareJson, none] = results;
    expect(json.code, json.stderr).toBe(0);
    const report = JSON.parse(json.stdout);
    expect(report.version).toBe(PACKAGE_VERSION);
    expect(report.project).toMatchObject({ name: "Installed", engine: "5.6", engineAssociation: "5.6" });
    expect(report.plugin).toMatchObject({ deployed: true, installKind: "source", installedVersion: PACKAGE_VERSION });
    expect(report.plugin.marker).toMatchObject({ kind: "source", version: PACKAGE_VERSION });
    expect(report.editor).toEqual({ running: false, instances: [] });
    expect(report.config).toEqual({ path: path.join(path.dirname(installed), "ue-mcp.yml"), exists: true });
    expect(report.clients.find((c: { id: string }) => c.id === "claude-code")).toMatchObject({ configured: true, pinned: PACKAGE_VERSION });
    expect(report.clients.find((c: { id: string }) => c.id === "cursor")).toMatchObject({ configured: false, pinned: null });

    expect(bareJson.code).toBe(0);
    const bareReport = JSON.parse(bareJson.stdout);
    expect(bareReport.project.name).toBe("Bare");
    expect(bareReport.plugin.deployed).toBe(false);
    expect(bareReport.config.exists).toBe(false);

    expect(none.code).toBe(2);
    expect(JSON.parse(none.stdout)).toEqual({ ok: false, error: expect.stringContaining("No .uproject found") });
  });

  it("prints the install state for people, and writes nothing", () => {
    covers("cli.status");
    const [, , , text, bareText, none] = results;
    expect(text.code).toBe(0);
    const out = plain(text.stdout);
    expect(out).toContain(`Installed  UE 5.6  ue-mcp ${PACKAGE_VERSION}`);
    expect(out).toContain(`bridge ${PACKAGE_VERSION} (source)`);
    expect(out).toContain("editor not running");
    expect(out).toContain(`Claude Code (project): ue-mcp@${PACKAGE_VERSION}`);

    expect(bareText.code).toBe(0);
    expect(plain(bareText.stdout)).toContain("bridge not installed");
    expect(none.code).toBe(2);
    expect(plain(none.stdout + none.stderr)).toContain("No .uproject found");

    expect(snapshotTree(sb.root)).toEqual(before);
  });
});

describe("context", () => {
  let project: string;
  let yml: string;
  beforeAll(() => {
    project = sb.makeProject("Ctx", { config: "ue-mcp:\n  version: 1\n  disable:\n    - niagara\ntasks: {}\nflows: {}\n" });
    yml = path.join(path.dirname(project), "ue-mcp.yml");
  });

  it("prints the strategy of the project in cwd or named, micro by default", async () => {
    covers("cli.context.status");
    const [inCwd, named, explicit] = await Promise.all([
      runCli(sb, ["context"], { cwd: path.dirname(project) }),
      runCli(sb, ["context", project]),
      runCli(sb, ["context", "status", path.dirname(project)]),
    ]);
    for (const r of [inCwd, named, explicit]) {
      expect(r.code, r.stderr).toBe(0);
      expect(plain(r.stdout)).toContain("Context strategy: micro");
      expect(plain(r.stdout)).toContain(yml);
    }
  });

  it("writes lean or full into ue-mcp.yml, keeps the rest, and micro removes the key", async () => {
    covers("cli.context.set");
    const full = await runCli(sb, ["context", "full", project]);
    expect(full.code, full.stderr).toBe(0);
    expect(plain(full.stdout)).toContain("Context strategy: micro -> full");
    let text = fs.readFileSync(yml, "utf8");
    expect(text).toMatch(/context:\s+strategy: full/);
    expect(text).toMatch(/disable:\s+- niagara/);

    const status = await runCli(sb, ["context"], { cwd: path.dirname(project) });
    expect(plain(status.stdout)).toContain("Context strategy: full");

    const lean = await runCli(sb, ["context", "lean"], { cwd: path.dirname(project) });
    expect(plain(lean.stdout)).toContain("Context strategy: full -> lean");
    expect(fs.readFileSync(yml, "utf8")).toMatch(/strategy: lean/);

    const micro = await runCli(sb, ["context", "micro", project]);
    expect(micro.code).toBe(0);
    text = fs.readFileSync(yml, "utf8");
    expect(text).not.toContain("context:");
    expect(text).toMatch(/disable:\s+- niagara/);
  });

  it("refuses a word that is neither a strategy nor a project, and a project that does not exist", async () => {
    covers("cli.context.set", "cli.context.status");
    const before = fs.readFileSync(yml, "utf8");
    const [typo, missing, extra] = await Promise.all([
      runCli(sb, ["context", "leen"], { cwd: path.dirname(project) }),
      runCli(sb, ["context", "full", sb.path("Nowhere", "Nowhere.uproject")], { cwd: path.dirname(project) }),
      runCli(sb, ["context", "full", project, "lean"]),
    ]);
    expect(typo.code).toBe(2);
    expect(plain(typo.stdout + typo.stderr)).toContain("'leen'");
    expect(missing.code).toBe(2);
    expect(plain(missing.stdout + missing.stderr)).toContain("Nowhere.uproject");
    expect(extra.code).toBe(2);
    expect(plain(extra.stdout + extra.stderr)).toContain("'lean'");
    expect(fs.readFileSync(yml, "utf8")).toBe(before);
  });
});

describe("hook", () => {
  let on: string;
  let off: string;
  let outside: string;
  const payload = (dir: string, action: string, extra: Record<string, unknown> = {}) =>
    JSON.stringify({ tool_name: "mcp__ue-mcp__editor", tool_input: { action, ...extra }, cwd: dir });

  beforeAll(() => {
    on = path.dirname(sb.makeProject("HookOn", { config: "ue-mcp:\n  version: 1\n" }));
    off = path.dirname(sb.makeProject("HookOff", { config: "ue-mcp:\n  version: 1\n  disable:\n    - feedback\n" }));
    outside = sb.path("not-a-project");
    fs.mkdirSync(outside, { recursive: true });
  });

  it("post-tool-use answers execute_python in a ue-mcp project with a feedback reminder", async () => {
    covers("cli.hook.post-tool-use");
    const tmp = { TMP: sb.path("tmp"), TEMP: sb.path("tmp"), TMPDIR: sb.path("tmp") };
    fs.mkdirSync(tmp.TMP, { recursive: true });
    const run = (input: string, cwd = outside) => runCli(sb, ["hook", "post-tool-use"], { input, cwd, env: tmp });
    const [fires, byProjectPath, otherAction, disabled, noProject, garbage, empty] = await Promise.all([
      run(payload(on, "execute_python")),
      // The project the call named wins over the client's cwd.
      run(payload(outside, "execute_python", { projectPath: path.join(on, "HookOn.uproject") })),
      run(payload(on, "get_status")),
      run(payload(off, "execute_python")),
      run(payload(outside, "execute_python")),
      run("{not json"),
      run(""),
    ]);

    for (const r of [fires, byProjectPath]) {
      expect(r.code, r.stderr).toBe(0);
      expect(r.stderr).toBe("");
      const message = JSON.parse(r.stdout);
      expect(Object.keys(message)).toEqual(["systemMessage"]);
      expect(message.systemMessage).toContain("execute_python was just used as a workaround");
      expect(message.systemMessage).toContain('feedback(action="submit")');
    }
    for (const r of [otherAction, disabled, noProject, garbage, empty]) {
      expect(r.code).toBe(0);
      expect(r.stdout).toBe("");
      expect(r.stderr).toBe("");
    }
  });

  it("any other event kind is a silent no-op", async () => {
    covers("cli.hook.other");
    const input = payload(on, "execute_python");
    const results = await Promise.all([
      runCli(sb, ["hook", "pre-tool-use"], { input, cwd: on }),
      runCli(sb, ["hook"], { input, cwd: on }),
    ]);
    for (const r of results) {
      expect(r.code).toBe(0);
      expect(r.stdout).toBe("");
      expect(r.stderr).toBe("");
    }
  });
});
