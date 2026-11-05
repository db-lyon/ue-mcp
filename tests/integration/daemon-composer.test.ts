/**
 * What the daemon offers a client that composes its own agent context: the
 * skills it can load and the surface a context strategy would advertise, over
 * HTTP and through the extension API, against a
 * spawned daemon in a sandbox.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { PACKAGE_VERSION, Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, writeExtension, type Discovery } from "./daemon-fakes.js";

let sb: Sandbox;
let projectDir: string;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("Composer");
  projectDir = path.dirname(project);

  // A hand-written project skill, and one with frontmatter that does not parse.
  const own = path.join(projectDir, ".claude", "skills", "level-notes");
  fs.mkdirSync(own, { recursive: true });
  fs.writeFileSync(path.join(own, "SKILL.md"), "---\nname: level-notes\ndescription: How this project lays out levels\n---\nUse sublevels.\n");
  fs.writeFileSync(path.join(own, "layout.md"), "detail");
  const broken = path.join(projectDir, ".agents", "skills", "broken");
  fs.mkdirSync(broken, { recursive: true });
  fs.writeFileSync(path.join(broken, "SKILL.md"), "---\ndescription: [never closed\n---\nbody\n");

  writeExtension(path.join(sb.home, ".ue-mcp", "extensions"), {
    name: "composer",
    version: "1.0.0",
    activate: `
      api.route("GET", "/features", () => ({ body: { features: api.features } }));
      api.route("GET", "/skills", () => ({ body: { skills: api.skills.list() } }));
      api.route("GET", "/modes", () => ({ body: api.modes.get() }));
      api.route("PUT", "/modes/playtest", (req) => { api.modes.setPlaytest(req.body.mode ?? undefined); return { body: api.modes.get().playtest }; });
      api.route("POST", "/action", async (req) => ({ body: await api.callAction(req.body.category, req.body.action, req.body.args) }));
      api.route("GET", "/actions", () => ({ body: { actions: api.actions.effects(["asset"]) } }));
      api.route("GET", "/projection", () => ({ body: api.context.project({ strategy: "lean", categories: ["level"] }) }));
      api.route("POST", "/call", async (req) => ({ body: await api.context.call(req.body.tool, req.body.input, req.body.opts) }));
    `,
  });
  d = await startDaemonCli(sb, project);
});

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("skills", () => {
  it("lists packaged and project skills with owners over /v1/skills and api.skills", async () => {
    covers("daemon.skills");
    const health = await call(d, "GET", "/v1/health");
    expect(health.body.features).toContain("skills");
    expect((await call(d, "GET", "/v1/ext/composer/features")).body.features).toContain("skills");

    const http = await call(d, "GET", "/v1/skills");
    expect(http.status, http.text).toBe(200);
    const skills = http.body.skills as Array<Record<string, any>>;
    const byId = Object.fromEntries(skills.map((s) => [s.id, s]));

    expect(byId["level-notes"]).toMatchObject({
      name: "level-notes",
      description: "How this project lays out levels",
      dir: path.join(projectDir, ".claude", "skills", "level-notes"),
      files: ["layout.md"],
      body: "Use sublevels.\n",
      owner: { kind: "project" },
      location: "project",
      folder: ".claude/skills",
      problems: [],
    });
    expect(byId.broken).toMatchObject({ owner: { kind: "project" }, folder: ".agents/skills", frontmatter: {} });
    expect(byId.broken.problems[0]).toMatch(/not valid YAML/);
    const packaged = skills.filter((s) => s.location === "packaged");
    expect(packaged.length).toBeGreaterThan(0);
    for (const s of packaged) expect(s.owner).toEqual({ kind: "ue-mcp", version: PACKAGE_VERSION });

    const ext = await call(d, "GET", "/v1/ext/composer/skills");
    expect(ext.status, ext.text).toBe(200);
    expect(ext.body.skills).toEqual(skills);
  });
});

describe("context projection", () => {
  it("serves a strategy and category scope over /v1/context/projection and api.context", async () => {
    covers("daemon.context-projection");
    expect((await call(d, "GET", "/v1/health")).body.features).toContain("context.projection");

    const all = await call(d, "GET", "/v1/context/projection?strategy=lean");
    expect(all.status, all.text).toBe(200);
    const scoped = await call(d, "GET", "/v1/context/projection?strategy=lean&categories=level,blueprint");
    expect(scoped.status, scoped.text).toBe(200);
    expect(scoped.body).toMatchObject({ strategy: "lean", estimator: "chars/4" });
    expect([...scoped.body.categories].sort()).toEqual(["blueprint", "level"]);
    expect(scoped.body.tools.map((t: { name: string }) => t.name).sort()).toEqual(["blueprint", "catalog", "flow", "level"]);
    expect(scoped.body.seedTokens).toBeLessThan(all.body.seedTokens);
    expect(scoped.body.tools.find((t: { name: string }) => t.name === "level").inputSchema).toMatchObject({ type: "object" });

    const micro = await call(d, "GET", "/v1/context/projection");
    expect(micro.body.strategy).toBe("micro");
    expect((await call(d, "GET", "/v1/context/projection?strategy=huge")).status).toBe(400);
    expect((await call(d, "GET", "/v1/context/projection?categories=level,nope")).status).toBe(400);

    const viaExt = await call(d, "GET", "/v1/ext/composer/projection");
    expect(viaExt.body).toEqual((await call(d, "GET", "/v1/context/projection?strategy=lean&categories=level")).body);

    const described = await call(d, "POST", "/v1/ext/composer/call", { tool: "level", input: { action: "describe" }, opts: { strategy: "lean", categories: ["level"] } });
    expect(described.body.isError, described.text).toBe(false);
    expect(JSON.parse(described.body.content[0].text)).toMatchObject({ category: "level" });

    const refused = await call(d, "POST", "/v1/ext/composer/call", { tool: "tools", input: { action: "call", category: "asset", method: "list", args: {} }, opts: { strategy: "micro", categories: ["level"] } });
    expect(refused.body.isError).toBe(true);
    expect(refused.body.content[0].text).toMatch(/'asset' is outside this context's categories/);
  });
});

describe("action effects", () => {
  it("lists each action's effect and destructive flag over /v1/actions and api.actions", async () => {
    covers("daemon.actions-effects");
    expect((await call(d, "GET", "/v1/health")).body.features).toContain("actions.effects");
    const all = await call(d, "GET", "/v1/actions");
    expect(all.status, all.text).toBe(200);
    const find = (category: string, action: string) =>
      (all.body.actions as Array<Record<string, unknown>>).find((a) => a.category === category && a.action === action);
    expect(find("asset", "delete")).toEqual({ category: "asset", action: "delete", effect: "mutate", effectSource: "declared", destructive: true, destructiveSource: "lexicon" });
    expect(find("level", "get_outliner")).toMatchObject({ effect: "read", destructive: false, destructiveSource: "none" });
    expect(find("project", "drop_editor")).toMatchObject({ destructive: false, destructiveSource: "declared" });

    const asset = await call(d, "GET", "/v1/actions?categories=asset");
    expect(new Set(asset.body.actions.map((a: { category: string }) => a.category))).toEqual(new Set(["asset"]));
    expect((await call(d, "GET", "/v1/ext/composer/actions")).body.actions).toEqual(asset.body.actions);
    expect((await call(d, "GET", "/v1/actions?categories=asset,nope")).status).toBe(400);
  });
});

describe("playtest mode", () => {
  it("is read and set through api.modes, and does not gate an extension's own calls", async () => {
    covers("daemon.modes-playtest");
    expect((await call(d, "GET", "/v1/ext/composer/features")).body.features).toContain("modes.playtest");
    expect((await call(d, "GET", "/v1/ext/composer/modes")).body.playtest).toEqual({ mode: null, project: false });
    expect((await call(d, "PUT", "/v1/ext/composer/modes/playtest", { mode: "manual" })).body).toEqual({ mode: "manual", project: true });

    // Manual refuses an MCP session's run; an extension's callAction reaches dispatch as before.
    const r = await call(d, "POST", "/v1/ext/composer/action", { category: "pie", action: "script_run", args: { script: "Scripts/smoke.yaml" } });
    expect(r.status, r.text).toBe(200);
    expect(JSON.stringify(r.body.content)).not.toContain("playtestMode");

    expect((await call(d, "PUT", "/v1/ext/composer/modes/playtest", { mode: null })).body).toEqual({ mode: null, project: false });
  });
});
