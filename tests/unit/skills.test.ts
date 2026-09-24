/**
 * Claude Code skills: install and removal per owning package, plugin skills
 * following the project's plugins: list, and the gate that every shipped
 * skill teaches only actions that exist.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ALL_TOOLS } from "../../src/tools.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { flowCategoryForCheck } from "../../src/flow/flow-surface.js";
import type { FlowConfig } from "../../src/flow/schema.js";
import {
  CORE_OWNER,
  checkSkills,
  extractActionReferences,
  installCoreSkills,
  installSkillSet,
  listSkills,
  packagedSkillsRoot,
  projectSkillsRoot,
  readSkillOwnership,
  removeCoreSkills,
  removeSkillSet,
  syncPluginSkills,
} from "../../src/skills.js";

let tmp: string;
let projectDir: string;

function writeSkill(root: string, name: string, body = "# body\n", description: string | null = `what ${name} is for`): void {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const front = ["---", `name: ${name}`, ...(description === null ? [] : [`description: ${description}`]), "---", ""];
  fs.writeFileSync(path.join(dir, "SKILL.md"), front.join("\n") + body);
}

/** A fake installed plugin package with a skills/ directory. */
function writePlugin(name: string, skills: string[]): string {
  const pkgDir = path.join(projectDir, "node_modules", name);
  fs.mkdirSync(pkgDir, { recursive: true });
  fs.writeFileSync(path.join(pkgDir, "package.json"), JSON.stringify({ name, version: "1.0.0" }));
  for (const s of skills) writeSkill(path.join(pkgDir, "skills"), s);
  return pkgDir;
}

function writeConfig(plugins: string[]): string {
  const file = path.join(projectDir, "ue-mcp.yml");
  fs.writeFileSync(file, `plugins:\n${plugins.map((p) => `  - name: ${p}\n`).join("")}`);
  return file;
}

const installed = (): string[] => listSkills(projectSkillsRoot(projectDir));

beforeEach(() => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-skills-"));
  projectDir = path.join(tmp, "project");
  fs.mkdirSync(projectDir, { recursive: true });
});

afterEach(() => {
  fs.rmSync(tmp, { recursive: true, force: true });
});

describe("installing one owner's skills", () => {
  it("installs, then reports a repeat as unchanged", () => {
    const src = path.join(tmp, "src");
    writeSkill(src, "a");
    writeSkill(src, "b");
    expect(installSkillSet(projectDir, "pkg", src).installed).toEqual(["a", "b"]);
    const again = installSkillSet(projectDir, "pkg", src);
    expect(again.installed).toEqual([]);
    expect(again.unchanged).toEqual(["a", "b"]);
    expect(readSkillOwnership(projectDir)).toEqual({ pkg: ["a", "b"] });
  });

  it("prunes a skill the owner stopped shipping", () => {
    const src = path.join(tmp, "src");
    writeSkill(src, "a");
    writeSkill(src, "b");
    installSkillSet(projectDir, "pkg", src);
    fs.rmSync(path.join(src, "b"), { recursive: true });
    expect(installSkillSet(projectDir, "pkg", src).pruned).toEqual(["b"]);
    expect(installed()).toEqual(["a"]);
  });

  it("refuses a name another owner holds", () => {
    const one = path.join(tmp, "one");
    const two = path.join(tmp, "two");
    writeSkill(one, "shared");
    writeSkill(two, "shared", "# different\n");
    installSkillSet(projectDir, "first", one);
    const r = installSkillSet(projectDir, "second", two);
    expect(r.conflicts).toEqual([{ skill: "shared", heldBy: "first" }]);
    expect(fs.readFileSync(path.join(projectSkillsRoot(projectDir), "shared", "SKILL.md"), "utf-8")).toContain("# body");
  });

  it("refuses to overwrite a skill the user wrote", () => {
    writeSkill(projectSkillsRoot(projectDir), "mine", "# the user's\n");
    const src = path.join(tmp, "src");
    writeSkill(src, "mine");
    expect(installSkillSet(projectDir, "pkg", src).conflicts[0].skill).toBe("mine");
    expect(fs.readFileSync(path.join(projectSkillsRoot(projectDir), "mine", "SKILL.md"), "utf-8")).toContain("the user's");
  });

  it("removes only its own skills, and leaves the user's", () => {
    const src = path.join(tmp, "src");
    writeSkill(src, "a");
    writeSkill(projectSkillsRoot(projectDir), "mine");
    installSkillSet(projectDir, "pkg", src);
    expect(removeSkillSet(projectDir, "pkg")).toEqual(["a"]);
    expect(installed()).toEqual(["mine"]);
    expect(readSkillOwnership(projectDir)).toEqual({});
  });
});

describe("ue-mcp's own skills", () => {
  it("takes over copies an earlier version installed without recording them", () => {
    const name = listSkills(packagedSkillsRoot())[0];
    writeSkill(projectSkillsRoot(projectDir), name, "# stale\n");
    const r = installCoreSkills(projectDir);
    expect(r.conflicts).toEqual([]);
    expect(r.installed).toContain(name);
    expect(readSkillOwnership(projectDir)[CORE_OWNER]).toEqual(listSkills(packagedSkillsRoot()));
  });

  it("removes untracked copies from an earlier version on opt-out", () => {
    const name = listSkills(packagedSkillsRoot())[0];
    writeSkill(projectSkillsRoot(projectDir), name);
    expect(removeCoreSkills(projectDir)).toEqual([name]);
    expect(fs.existsSync(projectSkillsRoot(projectDir))).toBe(false);
  });
});

describe("plugin skills follow the plugins: list", () => {
  it("installs a listed plugin's skills and removes them once it is unlisted", () => {
    writePlugin("ue-mcp-foo", ["foo-guide"]);
    const config = writeConfig(["ue-mcp-foo"]);
    expect(syncPluginSkills(projectDir, config).plugins["ue-mcp-foo"].installed).toEqual(["foo-guide"]);
    expect(installed()).toEqual(["foo-guide"]);

    writeConfig([]);
    expect(syncPluginSkills(projectDir, config).removed).toEqual({ "ue-mcp-foo": ["foo-guide"] });
    expect(installed()).toEqual([]);
  });

  it("leaves ue-mcp's own skills alone", () => {
    installCoreSkills(projectDir);
    const before = installed();
    syncPluginSkills(projectDir, writeConfig([]));
    expect(installed()).toEqual(before);
  });

  it("skips a plugin that ships no skills", () => {
    writePlugin("ue-mcp-bare", []);
    expect(syncPluginSkills(projectDir, writeConfig(["ue-mcp-bare"])).plugins).toEqual({});
  });
});

describe("checking skills", () => {
  const known = new Set(["asset.list", "asset.search", "niagara.create"]);

  it("pulls every action the body teaches, in either quoting style", () => {
    expect(
      extractActionReferences('`asset(action="list")` then niagara(action=\'create\') and asset(action="list")'),
    ).toEqual(["asset.list", "niagara.create"]);
  });

  it("reports a dead action with the closest real one", () => {
    const root = path.join(tmp, "check");
    writeSkill(root, "x", 'asset(action="lst")\n');
    const r = checkSkills(root, known);
    expect(r.problems).toHaveLength(1);
    expect(r.problems[0].didYouMean).toContain("asset.list");
  });

  it("reports a missing description and a name that disagrees with its directory", () => {
    const root = path.join(tmp, "check");
    writeSkill(root, "x", "# body\n", null);
    fs.writeFileSync(path.join(root, "y.md"), "not a skill");
    const dir = path.join(root, "z");
    fs.mkdirSync(dir);
    fs.writeFileSync(path.join(dir, "SKILL.md"), "---\nname: other\ndescription: d\n---\n");
    expect(checkSkills(root, known).problems.map((p) => p.skill)).toEqual(["x", "z"]);
  });

  it("lists a reference to an unknown category as unverified, not wrong", () => {
    const root = path.join(tmp, "check");
    writeSkill(root, "x", 'otherplugin(action="go")\n');
    const r = checkSkills(root, known);
    expect(r.problems).toEqual([]);
    expect(r.unverified).toEqual(["otherplugin.go"]);
  });
});

describe("the shipped skills teach calls that exist", () => {
  it("restates the flow tool's actions exactly", () => {
    const flowTool = createFlowTool({} as never, () => ({ flows: {}, tasks: {} }) as unknown as FlowConfig);
    expect(Object.keys(flowCategoryForCheck().actions).sort()).toEqual(Object.keys(flowTool.actions).sort());
  });

  it("resolves every action referenced by every packaged skill", () => {
    const known = new Set<string>();
    for (const tool of [...ALL_TOOLS, flowCategoryForCheck()]) {
      for (const action of Object.keys(tool.actions)) known.add(`${tool.name}.${action}`);
    }
    const r = checkSkills(packagedSkillsRoot(), known);
    expect(r.checked.length).toBeGreaterThan(0);
    const rendered = r.problems.map(
      (p) => `  ${p.skill}: ${p.detail}${p.didYouMean?.length ? ` closest: ${p.didYouMean.join(", ")}` : ""}`,
    );
    expect(rendered, "Packaged skills name actions that do not exist:\n" + rendered.join("\n")).toEqual([]);
    expect(r.unverified).toEqual([]);
  });
});
