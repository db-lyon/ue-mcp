/**
 * Skill packs (V18), and the gate that keeps the shipped ones honest.
 *
 * A skill pack is prose that names calls. That makes it the one artifact in
 * this repository with no compiler and no schema behind it: rename an action
 * and the pack still installs, still loads, and still tells an agent to make a
 * call the server refuses. The cost lands mid-task, on somebody else.
 *
 * So the last test here is the load-bearing one: every `category(action="x")`
 * in every packaged pack must resolve against the declared tool graph. It is
 * the same property `tests/unit/action-schema.test.ts` holds for parameters,
 * one layer up.
 *
 * The rest covers the mechanism a pack needs to be shippable from somewhere
 * other than this repository: discovery from a plugin package, the sidecar
 * that declares what prose cannot, and install / remove being idempotent and
 * saying which.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ALL_TOOLS } from "../../src/tools.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import type { FlowConfig } from "../../src/flow/schema.js";
import type { ToolContext, ToolDef } from "../../src/types.js";
import { installSkills, uninstallSkills } from "../../src/skills.js";
import { FLOW_OWN_ACTIONS, flowCategoryForCheck } from "../../src/flow/skill-actions.js";
import {
  checkSkillPacks,
  dedupePacks,
  extractActionReferences,
  installPacks,
  packagedSkillsRoot,
  projectSkillsRoot,
  readSkillRoot,
  splitFrontmatter,
  uninstallPacks,
} from "../../src/skill-packs.js";

const EMPTY_CONFIG = { flows: {}, tasks: {} } as unknown as FlowConfig;

let tmp: string;
let projectDir: string;
let packRoot: string;
let tool: ToolDef;

function makeContext(dir: string | null, plugins: Array<{ name: string; pkgDir: string }> = []): ToolContext {
  return {
    bridge: { isConnected: false } as unknown as ToolContext["bridge"],
    project: { projectDir: dir } as unknown as ToolContext["project"],
    getPlugins: () =>
      plugins.map((p) => ({
        name: p.name,
        pkgDir: p.pkgDir,
        status: "active",
      })) as never,
  };
}

async function call(
  params: Record<string, unknown>,
  ctx: ToolContext = makeContext(projectDir),
): Promise<Record<string, unknown>> {
  return (await tool.handler(ctx, params)) as Record<string, unknown>;
}

/** Write a throwaway pack, so a test never depends on the shipped ones. */
function writePack(
  root: string,
  name: string,
  opts: { description?: string; frontmatterName?: string; body?: string; sidecar?: string } = {},
): string {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const front = [
    "---",
    `name: ${opts.frontmatterName ?? name}`,
    ...(opts.description === undefined ? [`description: what ${name} is for`] : opts.description === "" ? [] : [`description: ${opts.description}`]),
    "---",
    "",
  ].join("\n");
  fs.writeFileSync(path.join(dir, "SKILL.md"), front + (opts.body ?? "# body\n"));
  if (opts.sidecar !== undefined) fs.writeFileSync(path.join(dir, "ue-mcp.yml"), opts.sidecar);
  return dir;
}

beforeEach(() => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-skills-"));
  projectDir = path.join(tmp, "project");
  packRoot = path.join(tmp, "packs");
  fs.mkdirSync(projectDir, { recursive: true });
  fs.mkdirSync(packRoot, { recursive: true });
  tool = createFlowTool({} as never, () => EMPTY_CONFIG);
});

afterEach(() => {
  fs.rmSync(tmp, { recursive: true, force: true });
});

describe("reading a pack", () => {
  it("splits frontmatter off the body", () => {
    const { frontmatter, content } = splitFrontmatter("---\nname: x\ndescription: y\n---\n\n# heading\n");
    expect(frontmatter).toEqual({ name: "x", description: "y" });
    expect(content).toContain("# heading");
  });

  it("returns the whole document when there is no frontmatter", () => {
    const { frontmatter, content } = splitFrontmatter("# heading\n");
    expect(frontmatter).toEqual({});
    expect(content).toEqual("# heading\n");
  });

  it("pulls every action the body teaches, in either quoting style", () => {
    const refs = extractActionReferences(
      'Call `asset(action="list")` then niagara(action=\'create\', name="x") and asset(action="list").',
    );
    expect(refs).toEqual(["asset.list", "niagara.create"]);
  });

  it("reads declarations from the sidecar", () => {
    writePack(packRoot, "demo", {
      sidecar: "categories: [niagara]\nuePlugins: [Niagara]\nengine: '>=5.4'\ntriggers: [vfx, particle]\n",
    });
    const [pack] = readSkillRoot(packRoot, "packaged");
    expect(pack.declarations.categories).toEqual(["niagara"]);
    expect(pack.declarations.uePlugins).toEqual(["Niagara"]);
    expect(pack.declarations.engine).toEqual(">=5.4");
    expect(pack.declarations.triggers).toEqual(["vfx", "particle"]);
  });

  it("accepts a sidecar wrapped in a ue-mcp block, matching project config", () => {
    writePack(packRoot, "demo", { sidecar: "ue-mcp:\n  categories: [asset]\n" });
    const [pack] = readSkillRoot(packRoot, "packaged");
    expect(pack.declarations.categories).toEqual(["asset"]);
  });

  it("skips a directory with no SKILL.md rather than calling it a pack", () => {
    fs.mkdirSync(path.join(packRoot, "not-a-pack"), { recursive: true });
    writePack(packRoot, "real");
    expect(readSkillRoot(packRoot, "packaged").map((p) => p.name)).toEqual(["real"]);
  });
});

describe("checking a pack", () => {
  it("reports an action the surface does not have, with the closest spellings", () => {
    writePack(packRoot, "stale", { body: 'Call `asset(action="lst")`.\n' });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    expect(result.problemCount).toEqual(1);
    expect(result.problems[0].kind).toEqual("unknown_action");
    expect(result.problems[0].detail).toContain("asset.lst");
    expect(result.problems[0].didYouMean).toContain("asset.list");
  });

  it("passes a pack whose calls all exist", () => {
    writePack(packRoot, "good", { body: 'Call `asset(action="list")` and `project(action="get_status")`.\n' });
    expect(checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS).problemCount).toEqual(0);
  });

  it("resolves a wrapped engine tool like any other action", () => {
    // This case used to assert the opposite: an `epic_` reference was set
    // aside as unverifiable, because those actions only existed once a live
    // 5.8 editor had been read at startup, so a pack teaching one could not be
    // checked against anything. They are declared now, so the reference
    // resolves and the pack is held to it like every other call it teaches.
    writePack(packRoot, "epic", { body: 'Call `blueprint(action="epic_write_graph_dsl")`.\n' });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    expect(result.problemCount).toEqual(0);
    expect(result.enrichmentOnly).toEqual([]);
  });

  it("flags a wrapped engine tool that does not exist, which it could not before", () => {
    writePack(packRoot, "epic-typo", { body: 'Call `blueprint(action="epic_write_graph_dsel")`.\n' });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    expect(result.problems.map((p) => p.kind)).toContain("unknown_action");
    expect(result.problems[0].didYouMean).toContain("blueprint.epic_write_graph_dsl");
  });

  it("flags a missing description, which is what decides whether a pack is loaded at all", () => {
    writePack(packRoot, "mute", { description: "" });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    expect(result.problems.map((p) => p.kind)).toContain("missing_description");
  });

  it("flags a frontmatter name that disagrees with its directory", () => {
    writePack(packRoot, "dir-name", { frontmatterName: "other-name" });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    expect(result.problems.map((p) => p.kind)).toContain("name_mismatch");
  });

  it("flags an unparsable sidecar instead of ignoring it", () => {
    writePack(packRoot, "broken", { sidecar: "categories: [unclosed\n" });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    expect(result.problems.map((p) => p.kind)).toContain("unparsable_sidecar");
  });

  it("flags a declared category that does not exist", () => {
    writePack(packRoot, "wrong", { sidecar: "categories: [terrain]\n" });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    const problem = result.problems.find((p) => p.kind === "unknown_category");
    expect(problem?.detail).toContain("landscape");
  });

  it("flags a body dependency the sidecar's actions list omits, once a list exists", () => {
    writePack(packRoot, "partial", {
      body: 'Call `asset(action="list")` and `asset(action="rename")`.\n',
      sidecar: "actions: [asset.list]\n",
    });
    const result = checkSkillPacks(readSkillRoot(packRoot, "packaged"), ALL_TOOLS);
    const problem = result.problems.find((p) => p.kind === "undeclared_dependency");
    expect(problem?.detail).toContain("asset.rename");
  });

  it("keeps one copy per name so a defect is not reported twice", () => {
    writePack(packRoot, "dup", { body: 'Call `asset(action="lst")`.\n' });
    const installedRoot = path.join(tmp, "installed");
    writePack(installedRoot, "dup", { body: 'Call `asset(action="lst")`.\n' });
    const packs = dedupePacks([
      ...readSkillRoot(installedRoot, "project"),
      ...readSkillRoot(packRoot, "packaged"),
    ]);
    expect(packs).toHaveLength(1);
    expect(packs[0].source).toEqual("packaged");
    expect(checkSkillPacks(packs, ALL_TOOLS).problemCount).toEqual(1);
  });
});

describe("installing packs", () => {
  it("copies the SKILL.md and the sidecar, and reports a repeat as unchanged", () => {
    writePack(packRoot, "demo", { sidecar: "categories: [asset]\n" });
    const dest = projectSkillsRoot(projectDir);
    const first = installPacks(readSkillRoot(packRoot, "packaged"), dest);
    expect(first.installed).toEqual(["demo"]);
    expect(fs.existsSync(path.join(dest, "demo", "SKILL.md"))).toBe(true);
    expect(fs.existsSync(path.join(dest, "demo", "ue-mcp.yml"))).toBe(true);

    const second = installPacks(readSkillRoot(packRoot, "packaged"), dest);
    expect(second.installed).toEqual([]);
    expect(second.unchanged).toEqual(["demo"]);
  });

  it("overwrites a pack whose content moved on", () => {
    writePack(packRoot, "demo");
    const dest = projectSkillsRoot(projectDir);
    installPacks(readSkillRoot(packRoot, "packaged"), dest);
    writePack(packRoot, "demo", { body: "# rewritten\n" });
    const again = installPacks(readSkillRoot(packRoot, "packaged"), dest);
    expect(again.installed).toEqual(["demo"]);
    expect(fs.readFileSync(path.join(dest, "demo", "SKILL.md"), "utf-8")).toContain("rewritten");
  });

  it("removes what it installed and leaves what it did not", () => {
    writePack(packRoot, "demo");
    const dest = projectSkillsRoot(projectDir);
    installPacks(readSkillRoot(packRoot, "packaged"), dest);
    fs.mkdirSync(path.join(dest, "hand-written"), { recursive: true });
    fs.writeFileSync(path.join(dest, "hand-written", "SKILL.md"), "mine\n");

    const removed = uninstallPacks(["demo"], dest);
    expect(removed.removed).toEqual(["demo"]);
    expect(fs.existsSync(path.join(dest, "demo"))).toBe(false);
    expect(fs.existsSync(path.join(dest, "hand-written", "SKILL.md"))).toBe(true);

    // Idempotent: removing it again says absent rather than failing.
    expect(uninstallPacks(["demo"], dest).absent).toEqual(["demo"]);
  });

  it("still installs and removes the packaged set through the init entry point", () => {
    const result = installSkills(projectDir);
    expect(result.error).toBeUndefined();
    expect(result.installed.length).toBeGreaterThan(0);
    expect(fs.existsSync(path.join(result.skillsDir, result.installed[0], "SKILL.md"))).toBe(true);
    const removed = uninstallSkills(projectDir);
    expect(removed.removed.sort()).toEqual(result.installed.sort());
    expect(fs.existsSync(removed.skillsDir)).toBe(false);
  });
});

describe("flow(skill_*) actions", () => {
  it("lists packaged, plugin and project packs, and says which are installed", async () => {
    const pluginDir = path.join(tmp, "node_modules", "ue-mcp-example");
    writePack(path.join(pluginDir, "skills"), "ue-mcp-example-terrain", {
      body: 'Call `landscape(action="create")`.\n',
    });
    const ctx = makeContext(projectDir, [{ name: "ue-mcp-example", pkgDir: pluginDir }]);

    const listed = await call({ action: "skill_list" }, ctx);
    const rows = listed.skills as Array<Record<string, unknown>>;
    const contributed = rows.find((r) => r.name === "ue-mcp-example-terrain");
    expect(contributed).toBeDefined();
    expect(contributed!.source).toEqual("plugin");
    expect(contributed!.contributor).toEqual("ue-mcp-example");
    expect(contributed!.installed).toBe(false);
    expect(rows.some((r) => r.source === "packaged")).toBe(true);
  });

  it("installs a plugin-contributed pack, then reports it installed and removable", async () => {
    const pluginDir = path.join(tmp, "node_modules", "ue-mcp-example");
    writePack(path.join(pluginDir, "skills"), "ue-mcp-example-terrain");
    const ctx = makeContext(projectDir, [{ name: "ue-mcp-example", pkgDir: pluginDir }]);

    const installed = await call(
      { action: "skill_install", skillName: "ue-mcp-example-terrain" },
      ctx,
    );
    expect(installed.changed).toBe(true);
    expect(installed.installed).toEqual(["ue-mcp-example-terrain"]);
    expect(installed.undo).toContain("skill_remove");

    const again = await call({ action: "skill_install", skillName: "ue-mcp-example-terrain" }, ctx);
    expect(again.changed).toBe(false);
    expect(again.unchanged).toEqual(["ue-mcp-example-terrain"]);

    const got = await call({ action: "skill_get", skillName: "ue-mcp-example-terrain" }, ctx);
    expect(got.installed).toBe(true);
    expect(got.installedUpToDate).toBe(true);
    expect(got.body).toContain("body");

    const removed = await call({ action: "skill_remove", skillName: "ue-mcp-example-terrain" }, ctx);
    expect(removed.changed).toBe(true);
    expect(removed.removed).toEqual(["ue-mcp-example-terrain"]);
    expect(removed.undo).toContain("skill_install");
  });

  it("spots an installed copy that has drifted from the available one", async () => {
    const pluginDir = path.join(tmp, "node_modules", "ue-mcp-example");
    writePack(path.join(pluginDir, "skills"), "drifted");
    const ctx = makeContext(projectDir, [{ name: "ue-mcp-example", pkgDir: pluginDir }]);
    await call({ action: "skill_install", skillName: "drifted" }, ctx);
    fs.appendFileSync(path.join(projectSkillsRoot(projectDir), "drifted", "SKILL.md"), "\nlocal edit\n");

    const got = await call({ action: "skill_get", skillName: "drifted" }, ctx);
    expect(got.installedUpToDate).toBe(false);
  });

  it("names the available packs when asked for one that does not exist", async () => {
    await expect(call({ action: "skill_get", skillName: "no-such-pack" })).rejects.toThrow(
      /No skill pack named 'no-such-pack'.*ue-mcp-workflow/s,
    );
  });

  it("refuses a remove with no target, and one with two", async () => {
    await expect(call({ action: "skill_remove" })).rejects.toThrow(/needs a target/);
    await expect(call({ action: "skill_remove", skillName: "x", all: true })).rejects.toThrow(
      /never both/,
    );
  });

  it("names the accepted sources on a bad source filter", async () => {
    await expect(call({ action: "skill_list", source: "builtin" })).rejects.toThrow(
      /must be one of packaged, project, plugin/,
    );
  });

  it("refuses to install without a project to install into", async () => {
    await expect(call({ action: "skill_install" }, makeContext(null))).rejects.toThrow(
      /no project directory/,
    );
  });

  it("checks every pack it can see and answers ok when they all resolve", async () => {
    const result = await call({ action: "skill_check" });
    expect(result.skillCount).toBeGreaterThan(0);
    expect(result.ok).toBe(true);
  });
});

describe("the shipped packs teach calls that exist", () => {
  it("knows the flow tool's own actions, which no graph read from tools.ts carries", () => {
    const flowTool = createFlowTool({} as never, () => EMPTY_CONFIG);
    // The checker builds the flow category by hand because the flow tool is
    // registered outside ALL_TOOLS. If the two ever disagree, a pack teaching a
    // flow action reads as a dead call, or a dead flow action stops being
    // reported. Either way the gate below is lying.
    expect(Object.keys(flowCategoryForCheck().actions).sort()).toEqual(
      Object.keys(flowTool.actions).sort(),
    );
    for (const own of FLOW_OWN_ACTIONS) expect(flowTool.actions[own]).toBeDefined();
  });

  /**
   * The gate. A skill pack that names a removed or renamed action is a
   * document that costs an agent a turn every time it is loaded, and nothing
   * else in this repository would catch it.
   */
  it("resolves every action referenced by every packaged pack", () => {
    const packs = readSkillRoot(packagedSkillsRoot(), "packaged");
    expect(packs.length).toBeGreaterThan(0);
    const result = checkSkillPacks(packs, [...ALL_TOOLS, flowCategoryForCheck()]);
    const rendered = result.problems.map(
      (p) => `  ${p.skill} (${p.kind}): ${p.detail}`
        + (p.didYouMean?.length ? `\n      closest: ${p.didYouMean.join(", ")}` : ""),
    );
    expect(
      rendered,
      "Packaged skill packs disagree with the action surface. Every one of these is prose\n"
        + "an agent will act on, so fix the pack or restore the action:\n" + rendered.join("\n"),
    ).toEqual([]);
  });
});
