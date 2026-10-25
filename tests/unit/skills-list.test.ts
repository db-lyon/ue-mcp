/**
 * listDaemonSkills: one entry per skill id across ue-mcp's packaged skills and
 * the project's .claude/skills and .agents/skills, each with its owner.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { listDaemonSkills, parseSkillFile } from "../../src/extensions/skills-list.js";
import { packageVersion } from "../../src/core/package-root.js";

let tmp: string;
let project: string;
let packaged: string;

function skill(root: string, id: string, text: string, extra: Record<string, string> = {}): string {
  const dir = path.join(root, id);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "SKILL.md"), text);
  for (const [rel, data] of Object.entries(extra)) {
    fs.mkdirSync(path.dirname(path.join(dir, rel)), { recursive: true });
    fs.writeFileSync(path.join(dir, rel), data);
  }
  return dir;
}

beforeEach(() => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-skills-list-"));
  project = path.join(tmp, "Game");
  packaged = path.join(tmp, "packaged");
  fs.mkdirSync(project, { recursive: true });
});
afterEach(() => fs.rmSync(tmp, { recursive: true, force: true }));

describe("parseSkillFile", () => {
  it("splits frontmatter from the body", () => {
    const r = parseSkillFile("---\nname: a\ndescription: does a\n---\n\n# A\nbody\n");
    expect(r.frontmatter).toEqual({ name: "a", description: "does a" });
    expect(r.body).toBe("# A\nbody\n");
    expect(r.problems).toEqual([]);
  });

  it("reports malformed frontmatter instead of throwing", () => {
    const r = parseSkillFile("---\nname: [unclosed\n---\nbody\n");
    expect(r.frontmatter).toEqual({});
    expect(r.body).toBe("body\n");
    expect(r.problems[0]).toMatch(/not valid YAML/);
    expect(parseSkillFile("no frontmatter").problems).toEqual(["SKILL.md has no frontmatter block."]);
    expect(parseSkillFile("---\n- a\n- b\n---\nx").problems[0]).toMatch(/not a mapping/);
  });
});

describe("listDaemonSkills", () => {
  it("lists packaged skills owned by ue-mcp with files, body and tokens", () => {
    const text = "---\nname: ue-mcp-x\ndescription: X things\n---\nDo X.\n";
    const dir = skill(packaged, "ue-mcp-x", text, { "ref/notes.md": "n", "a.txt": "a" });
    const [x] = listDaemonSkills(project, { packagedRoot: packaged });
    expect(x).toEqual({
      id: "ue-mcp-x",
      name: "ue-mcp-x",
      description: "X things",
      dir,
      files: ["a.txt", "ref/notes.md"],
      body: "Do X.\n",
      frontmatter: { name: "ue-mcp-x", description: "X things" },
      tokens: Math.ceil(text.length / 4),
      owner: { kind: "ue-mcp", version: packageVersion() },
      location: "packaged",
      problems: [],
    });
  });

  it("names each project skill's owner from the ownership record", () => {
    skill(packaged, "ue-mcp-x", "---\ndescription: packaged\n---\n");
    const claude = path.join(project, ".claude", "skills");
    const installed = skill(claude, "ue-mcp-x", "---\ndescription: installed\n---\n");
    skill(claude, "meshy-gen", "---\ndescription: plugin\n---\n");
    skill(claude, "gone-skill", "---\ndescription: plugin not installed\n---\n");
    skill(claude, "mine", "---\ndescription: hand written\n---\n");
    const agentsMine = skill(path.join(project, ".agents", "skills"), "mine", "---\ndescription: other agent copy\n---\n");
    skill(path.join(project, ".agents", "skills"), "agents-only", "---\ndescription: agents\n---\n");
    fs.mkdirSync(path.join(project, ".ue-mcp"), { recursive: true });
    fs.writeFileSync(path.join(project, ".ue-mcp", "skills.json"), JSON.stringify({
      "ue-mcp": ["ue-mcp-x"],
      "ue-mcp-meshy": ["meshy-gen"],
      "ue-mcp-gone": ["gone-skill"],
    }));
    const pkg = path.join(project, "node_modules", "ue-mcp-meshy");
    fs.mkdirSync(pkg, { recursive: true });
    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "ue-mcp-meshy", version: "2.3.4" }));

    const skills = listDaemonSkills(project, { packagedRoot: packaged });
    const byId = Object.fromEntries(skills.map((s) => [s.id, s]));
    expect(skills.map((s) => s.id)).toEqual(["gone-skill", "meshy-gen", "mine", "ue-mcp-x", "agents-only"]);

    expect(byId["ue-mcp-x"]).toMatchObject({
      dir: installed,
      description: "installed",
      owner: { kind: "ue-mcp", version: packageVersion() },
      location: "project",
      folder: ".claude/skills",
      shadows: [{ location: "packaged", dir: path.join(packaged, "ue-mcp-x") }],
    });
    expect(byId["meshy-gen"].owner).toEqual({ kind: "plugin", package: "ue-mcp-meshy", version: "2.3.4" });
    expect(byId["gone-skill"].owner).toEqual({ kind: "plugin", package: "ue-mcp-gone", version: null });
    expect(byId.mine).toMatchObject({ owner: { kind: "project" }, folder: ".claude/skills", shadows: [{ location: "project", folder: ".agents/skills", dir: agentsMine }] });
    expect(byId["agents-only"]).toMatchObject({ owner: { kind: "project" }, location: "project", folder: ".agents/skills" });
    expect(byId["agents-only"].shadows).toBeUndefined();
  });

  it("lists a skill with broken frontmatter and says what is wrong", () => {
    skill(path.join(project, ".claude", "skills"), "broken", "---\ndescription: : :\n  bad: [\n---\nbody\n");
    skill(path.join(project, ".claude", "skills"), "quiet", "---\nname: Quiet One\n---\nbody\n");
    const byId = Object.fromEntries(listDaemonSkills(project, { packagedRoot: packaged }).map((s) => [s.id, s]));
    expect(byId.broken).toMatchObject({ name: "broken", description: "", frontmatter: {}, body: "body\n" });
    expect(byId.broken.problems[0]).toMatch(/not valid YAML/);
    expect(byId.quiet).toMatchObject({ name: "Quiet One", problems: ["SKILL.md frontmatter has no 'description'."] });
  });

  it("reads ue-mcp's real packaged skills", () => {
    const skills = listDaemonSkills(project);
    expect(skills.length).toBeGreaterThan(0);
    for (const s of skills) {
      expect(s).toMatchObject({ location: "packaged", owner: { kind: "ue-mcp" }, problems: [] });
      expect(s.description).not.toBe("");
    }
  });
});
