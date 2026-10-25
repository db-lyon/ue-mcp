/**
 * What the daemon offers a client that composes its own agent context: the
 * skills it can load, over HTTP and through the extension API, against a
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
