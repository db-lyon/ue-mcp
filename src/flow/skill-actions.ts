/**
 * The `flow(skill_*)` actions: discovering, verifying and installing skill
 * packs.
 *
 * A skill pack is the written half of what the flow tool runs: a `SKILL.md`
 * that says which calls to make, in what order, and what has to be true first.
 * Until now the packs were files this package copied into a project during
 * `ue-mcp init` and nothing more - invisible from the tool surface, and
 * unchecked against the actions they teach.
 *
 * `skill_check` is the reason this exists. A pack names actions in prose, so a
 * renamed or removed action turns it into a document that confidently teaches
 * a call the server refuses, and the cost lands on an agent mid-task. The
 * check reads every `category(action="x")` out of every pack and holds it
 * against the live graph.
 *
 * `skill_list` also reads packs out of every loaded plugin package, which is
 * what makes a domain pack shippable from a sibling package instead of from
 * here: a plugin that ships `skills/<name>/SKILL.md` has its packs discovered,
 * verified and installed exactly like the built-in ones, with no manifest key
 * to add and no change to this repository.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import type { ActionEffect, ActionSpec, ToolContext, ToolDef } from "../types.js";
import { McpError, ErrorCode } from "../errors.js";
import { getLiveToolGraph } from "../tools.js";
import { journalActions } from "./journal-actions.js";
import {
  checkSkillPacks,
  dedupePacks,
  detailPack,
  installPacks,
  packagedSkillsRoot,
  projectSkillsRoot,
  readSkillRoot,
  summarisePack,
  uninstallPacks,
  type SkillPack,
  type SkillPackSource,
} from "../skill-packs.js";

const SOURCES: SkillPackSource[] = ["packaged", "project", "plugin"];

/**
 * Every pack this server can see, from all three sources.
 *
 * The plugin sweep is by directory convention rather than a manifest key: a
 * package that ships `skills/` gets its packs picked up with no declaration,
 * which is the whole point of making a pack cheap to publish.
 */
function discover(ctx: ToolContext): SkillPack[] {
  const packs: SkillPack[] = [...readSkillRoot(packagedSkillsRoot(), "packaged")];
  for (const plugin of ctx.getPlugins?.() ?? []) {
    if (plugin.status !== "active" || !plugin.pkgDir) continue;
    packs.push(
      ...readSkillRoot(path.join(plugin.pkgDir, "skills"), "plugin", plugin.name),
    );
  }
  const projectDir = ctx.project.projectDir;
  if (projectDir) packs.push(...readSkillRoot(projectSkillsRoot(projectDir), "project"));
  return packs;
}

/**
 * The flow tool's own actions, the three that are not journal or skill.
 *
 * The flow tool is registered outside `ALL_TOOLS` (it is built per server with
 * a task registry and a config source), so a graph read from `tools.ts` does
 * not contain it and a pack teaching `flow(action="run")` would read as a dead
 * call. `tests/unit/skill-packs.test.ts` fails if this list stops matching the
 * tool, so it cannot drift into being the lie it is there to prevent.
 */
export const FLOW_OWN_ACTIONS = ["run", "plan", "list"] as const;

/**
 * What each of those three does to the addressed editor.
 *
 * Restated here rather than read off the built tool because `flow-tool.ts`
 * imports this module and importing it back would close a cycle at module
 * init. `tests/unit/flow-action-contract.test.ts` fails when the two disagree,
 * so the restatement cannot drift into a second opinion.
 *
 * `run` is `mutate` rather than `unknown` even though a flow is only ever
 * whatever its steps are: a flow of pure reads is theoretically a read, no
 * flow in this repo or any shipped plugin is one, and both labels gate
 * identically. The wider answer is the one that stands.
 */
export const FLOW_OWN_EFFECTS: Record<(typeof FLOW_OWN_ACTIONS)[number], ActionEffect> = {
  run: "mutate",
  plan: "read",
  list: "read",
};

/** The flow category as a graph entry, for the check to hold packs against. */
export function flowCategoryForCheck(): ToolDef {
  const own = Object.entries(FLOW_OWN_EFFECTS).map(
    ([name, effect]) => [name, { kind: "handler", effect, handler: async () => ({}) }] as const,
  );
  return {
    name: "flow",
    description: "",
    schema: {},
    // The real specs for everything but the three own actions, so a check that
    // reads an effect off this stand-in reads the declaration rather than a
    // placeholder that happens to be next to it.
    actions: { ...Object.fromEntries(own), ...journalActions, ...skillActions },
    handler: async () => ({}),
  };
}

/** The graph a check holds packs against: the enriched one on a running
 *  server, the pristine declaration off one, plus the flow tool itself. */
function graphFor(): ToolDef[] {
  return [...getLiveToolGraph(), flowCategoryForCheck()];
}

function requireProjectDir(ctx: ToolContext, verb: string): string {
  const dir = ctx.project.projectDir;
  if (dir) return dir;
  throw new McpError(
    ErrorCode.PROJECT_NOT_LOADED,
    `Cannot ${verb} skill packs: this session has no project directory, so there is no `
      + `.claude/skills to write to. Start the server with a .uproject path, or address an `
      + `editor that has one.`,
  );
}

export const skillActions: Record<string, ActionSpec> = {
  skill_list: {
    kind: "handler",
    effect: "read",
    description:
      "Every skill pack this server can see: the ones bundled with ue-mcp, the ones contributed by "
      + "loaded plugins (any plugin package shipping skills/<name>/SKILL.md), and the ones installed "
      + "into this project's .claude/skills. Each row carries the description an agent reads to decide "
      + "whether to load it, the categories it drives, and any Unreal plugin it needs. Params: source? "
      + "(packaged|project|plugin, default all), detail? (include declared and referenced actions plus "
      + "the sidecar path). Returns the packs grouped by source, plus whether each is installed.",
    handler: async (ctx, params) => skillList(ctx, params),
  },
  skill_get: {
    kind: "handler",
    effect: "read",
    description:
      "One skill pack in full, optionally including its markdown, so an agent can read the workflow "
      + "without leaving the tool surface. Reports whether the project's installed copy matches the "
      + "available one, which is how a stale install is spotted. Params: skillName, includeBody? "
      + "(default true). Returns the pack, its declarations, the actions it teaches, and its install state.",
    handler: async (ctx, params) => skillGet(ctx, params),
  },
  skill_check: {
    kind: "handler",
    effect: "read",
    description:
      "Verify every skill pack against the live action surface. A pack is prose that names calls, so a "
      + "renamed or removed action leaves a document that teaches a call the server refuses; this reads "
      + "every category(action=\"x\") out of every pack and reports the ones that no longer resolve, with "
      + "the closest real spellings. Also flags a missing frontmatter description, a name that disagrees "
      + "with its directory, an unparsable ue-mcp.yml sidecar, and a sidecar 'actions:' list that omits "
      + "something the body teaches. epic_* references are reported separately, since those exist only "
      + "once a 5.8 editor enriches the surface. Params: none",
    handler: async (ctx) => skillCheck(ctx),
  },
  skill_install: {
    kind: "handler",
    effect: "mutate",
    description:
      "Copy skill packs into this project's .claude/skills, SKILL.md and ue-mcp.yml sidecar together. "
      + "Idempotent and specific about it: a pack whose destination already holds the same bytes comes "
      + "back as unchanged rather than counted as installed, so a repeat call is distinguishable from a "
      + "real update. Files in .claude/skills that did not come from a pack are never touched. Params: "
      + "skillName? (one pack; omitted, every packaged and plugin-contributed pack). Returns what "
      + "changed, what did not, and the call that removes them.",
    handler: async (ctx, params) => skillInstall(ctx, params),
  },
  skill_remove: {
    kind: "handler",
    effect: "mutate",
    description:
      "Remove installed skill packs from this project's .claude/skills. Deletes the SKILL.md and its "
      + "sidecar, then the pack directory only if it ends up empty and the skills directory only if it "
      + "does too, so anything the user added alongside survives. Idempotent: a pack that was not "
      + "installed is reported as absent rather than failing. Params: exactly one of skillName (that "
      + "pack) or all (true, every pack ue-mcp installed). Returns what was removed and the call that "
      + "puts it back.",
    handler: async (ctx, params) => skillRemove(ctx, params),
  },
};

/* ── handlers ──────────────────────────────────────────────────────── */

function skillList(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  const source = readSource(params.source);
  const detail = params.detail === true;
  const all = discover(ctx);
  const installed = new Set(all.filter((p) => p.source === "project").map((p) => p.name));
  const rows = (source ? all.filter((p) => p.source === source) : all).map((pack) => ({
    ...(detail ? detailPack(pack, false) : summarisePack(pack)),
    installed: installed.has(pack.name),
  }));
  return {
    skillCount: rows.length,
    sources: {
      packaged: packagedSkillsRoot(),
      project: ctx.project.projectDir ? projectSkillsRoot(ctx.project.projectDir) : undefined,
      plugin: (ctx.getPlugins?.() ?? [])
        .filter((p) => p.status === "active" && p.pkgDir)
        .map((p) => path.join(p.pkgDir, "skills"))
        .filter((dir) => fs.existsSync(dir)),
    },
    skills: rows,
  };
}

function skillGet(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  const all = discover(ctx);
  const name = typeof params.skillName === "string" ? params.skillName.trim() : "";
  if (!name) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      `'skillName' is required. Available: ${[...new Set(all.map((p) => p.name))].sort().join(", ") || "(none)"}.`,
    );
  }
  const matches = all.filter((p) => p.name === name);
  if (matches.length === 0) {
    throw new McpError(
      ErrorCode.NOT_FOUND,
      `No skill pack named '${name}'. Available: `
        + `${[...new Set(all.map((p) => p.name))].sort().join(", ") || "(none)"}. `
        + `flow(action="skill_list") reports each one's source and description.`,
    );
  }
  // The available copy is the authority; the installed one is what the project
  // is actually reading, so both are reported and compared.
  const available = matches.find((p) => p.source !== "project") ?? matches[0];
  const projectCopy = matches.find((p) => p.source === "project");
  const includeBody = params.includeBody !== false;
  return {
    ...detailPack(available, includeBody),
    installed: projectCopy !== undefined,
    installedFile: projectCopy?.file,
    installedUpToDate:
      projectCopy && available.source !== "project" ? sameFile(available.file, projectCopy.file) : undefined,
  };
}

function skillCheck(ctx: ToolContext): Record<string, unknown> {
  const packs = dedupePacks(discover(ctx));
  const result = checkSkillPacks(packs, graphFor());
  return {
    skillCount: result.skillCount,
    problemCount: result.problemCount,
    ok: result.problemCount === 0,
    problems: result.problems,
    enrichmentOnly: result.enrichmentOnly,
    enrichmentNote:
      result.enrichmentOnly.length > 0
        ? "These are Epic toolset actions, injected into a category only on UE 5.8 with the "
          + "ToolsetRegistry available. Absent from this graph means the editor is down or older, "
          + "not that the pack is wrong."
        : undefined,
    checkedAgainst: `${graphFor().length} categories`,
  };
}

function skillInstall(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  const projectDir = requireProjectDir(ctx, "install");
  const dest = projectSkillsRoot(projectDir);
  const available = discover(ctx).filter((p) => p.source !== "project");
  const name = typeof params.skillName === "string" ? params.skillName.trim() : "";
  let chosen = available;
  if (name) {
    chosen = available.filter((p) => p.name === name);
    if (chosen.length === 0) {
      throw new McpError(
        ErrorCode.NOT_FOUND,
        `No installable skill pack named '${name}'. Installable packs: `
          + `${available.map((p) => p.name).sort().join(", ") || "(none)"}. `
          + `A pack already in .claude/skills is installed, not installable.`,
      );
    }
  }
  const result = installPacks(dedupePacks(chosen), dest);
  return {
    changed: result.installed.length > 0,
    skillsDir: result.skillsDir,
    installed: result.installed,
    unchanged: result.unchanged,
    skipped: result.skipped,
    error: result.error,
    undo: name
      ? `flow(action="skill_remove", skillName="${name}")`
      : 'flow(action="skill_remove", all=true)',
  };
}

function skillRemove(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  const projectDir = requireProjectDir(ctx, "remove");
  const dest = projectSkillsRoot(projectDir);
  const name = typeof params.skillName === "string" ? params.skillName.trim() : "";
  const all = params.all === true;
  if (name && all) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      "Pass skillName or all, never both: one removes a single pack and the other removes every pack "
        + "ue-mcp installed into this project.",
    );
  }
  if (!name && !all) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      'skill_remove needs a target: pass skillName to remove one pack, or all=true to remove every '
        + `pack ue-mcp installed. flow(action="skill_list", source="project") reports what is installed.`,
    );
  }
  const names = name
    ? [name]
    : dedupePacks(discover(ctx).filter((p) => p.source !== "project")).map((p) => p.name);
  const result = uninstallPacks(names, dest);
  return {
    changed: result.removed.length > 0,
    skillsDir: result.skillsDir,
    removed: result.removed,
    absent: result.absent,
    undo: name
      ? `flow(action="skill_install", skillName="${name}")`
      : 'flow(action="skill_install")',
  };
}

/* ── parameter reading ─────────────────────────────────────────────── */

function readSource(raw: unknown): SkillPackSource | undefined {
  if (raw === undefined || raw === null || raw === "") return undefined;
  if (typeof raw === "string" && (SOURCES as string[]).includes(raw)) return raw as SkillPackSource;
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `'source' must be one of ${SOURCES.join(", ")}. Got ${JSON.stringify(raw)}. `
      + `'packaged' ships with ue-mcp, 'plugin' comes from a loaded plugin package, and 'project' is `
      + `what is installed under this project's .claude/skills.`,
  );
}

function sameFile(a: string, b: string): boolean {
  try {
    return fs.readFileSync(a).equals(fs.readFileSync(b));
  } catch {
    return false;
  }
}
