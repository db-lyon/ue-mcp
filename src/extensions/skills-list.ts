/**
 * Every skill a client of this project can load, with where it lives and who
 * owns it: ue-mcp's packaged skills, and every skill directory under the
 * project's `.claude/skills` and `.agents/skills`.
 *
 * Read-only. Installing and pruning stay in skills.ts; this reads what they
 * recorded in `<project>/.ue-mcp/skills.json` to name each directory's owner.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { packageVersion } from "../core/package-root.js";
import { findInstalledPackage } from "./resolver.js";
import { CORE_OWNER, listSkills, packagedSkillsRoot, readSkillOwnership } from "./skills.js";

/** Who a skill belongs to. */
export type DaemonSkillOwner =
  | { kind: "ue-mcp"; version: string }
  | { kind: "plugin"; package: string; version: string | null }
  | { kind: "project" };

/** The project folders skills are read from, in the order a same-named skill wins. */
export const PROJECT_SKILL_FOLDERS = [".claude/skills", ".agents/skills"] as const;
export type ProjectSkillFolder = (typeof PROJECT_SKILL_FOLDERS)[number];

/** A copy of a skill another entry with the same id hides. */
export interface ShadowedSkill {
  location: "packaged" | "project";
  folder?: ProjectSkillFolder;
  dir: string;
}

export interface DaemonSkill {
  /**
   * The skill's directory name, unique in one listing. A plugin's skills are
   * installed as `<namespace>-<skill>` (installedSkillName), so that is their id.
   */
  id: string;
  /** Frontmatter `name`, or the id when the frontmatter has none. */
  name: string;
  /** Frontmatter `description`, or "" (reported in `problems`). */
  description: string;
  /** Absolute path of the skill directory. */
  dir: string;
  /** Every other file in the directory, relative to it with `/` separators. */
  files: string[];
  /** SKILL.md without its frontmatter. */
  body: string;
  frontmatter: Record<string, unknown>;
  /** SKILL.md length in characters / 4, rounded up. */
  tokens: number;
  owner: DaemonSkillOwner;
  location: "packaged" | "project";
  /** Which project folder, for a project skill. */
  folder?: ProjectSkillFolder;
  /**
   * The copies with the same id this one hides: a project skill hides ue-mcp's
   * packaged copy of itself, and `.claude/skills` hides `.agents/skills`.
   * Absent when it hides nothing.
   */
  shadows?: ShadowedSkill[];
  /** What is wrong with SKILL.md, such as frontmatter that does not parse. Empty when nothing is. */
  problems: string[];
}

const FRONTMATTER = /^---\r?\n([\s\S]*?)\r?\n---[ \t]*(?:\r?\n|$)/;

/** Split SKILL.md into its frontmatter and body without ever throwing. */
export function parseSkillFile(text: string): { frontmatter: Record<string, unknown>; body: string; problems: string[] } {
  const m = FRONTMATTER.exec(text);
  if (!m) return { frontmatter: {}, body: text, problems: ["SKILL.md has no frontmatter block."] };
  const body = text.slice(m[0].length).replace(/^(?:[ \t]*\r?\n)+/, "");
  try {
    const doc = yaml.load(m[1]);
    if (doc === undefined || doc === null) return { frontmatter: {}, body, problems: ["SKILL.md frontmatter is empty."] };
    if (typeof doc !== "object" || Array.isArray(doc)) {
      return { frontmatter: {}, body, problems: ["SKILL.md frontmatter is not a mapping of keys to values."] };
    }
    return { frontmatter: doc as Record<string, unknown>, body, problems: [] };
  } catch (e) {
    const reason = e instanceof Error ? e.message.split("\n")[0] : String(e);
    return { frontmatter: {}, body, problems: [`SKILL.md frontmatter is not valid YAML: ${reason}`] };
  }
}

function filesBeside(dir: string): string[] {
  try {
    return (fs.readdirSync(dir, { recursive: true, withFileTypes: true }) as fs.Dirent[])
      .filter((e) => e.isFile())
      .map((e) => path.relative(dir, path.join(e.parentPath ?? e.path, e.name)).split(path.sep).join("/"))
      .filter((rel) => rel !== "SKILL.md")
      .sort();
  } catch {
    return [];
  }
}

function readSkill(
  dir: string,
  id: string,
  owner: DaemonSkillOwner,
  location: DaemonSkill["location"],
  folder?: ProjectSkillFolder,
): DaemonSkill {
  let text = "";
  const problems: string[] = [];
  try {
    text = fs.readFileSync(path.join(dir, "SKILL.md"), "utf-8");
  } catch (e) {
    problems.push(`SKILL.md cannot be read: ${e instanceof Error ? e.message : String(e)}`);
  }
  const parsed = parseSkillFile(text);
  problems.push(...parsed.problems);
  const { frontmatter } = parsed;
  const description = typeof frontmatter.description === "string" ? frontmatter.description.trim() : "";
  if (!description && parsed.problems.length === 0) problems.push("SKILL.md frontmatter has no 'description'.");
  return {
    id,
    name: typeof frontmatter.name === "string" && frontmatter.name.trim() ? frontmatter.name.trim() : id,
    description,
    dir,
    files: filesBeside(dir),
    body: parsed.body,
    frontmatter,
    tokens: Math.ceil(text.length / 4),
    owner,
    location,
    ...(folder ? { folder } : {}),
    problems,
  };
}

/** The version of an installed plugin package, or null when it cannot be found. */
function pluginVersion(pkg: string, projectDir: string): string | null {
  const dir = findInstalledPackage(pkg, projectDir);
  if (!dir) return null;
  try {
    const version = (JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf-8")) as { version?: unknown }).version;
    return typeof version === "string" ? version : null;
  } catch {
    return null;
  }
}

/**
 * Every skill for this project, one entry per id. A project skill wins over
 * ue-mcp's packaged copy of the same name, and `.claude/skills` over
 * `.agents/skills`; the winner lists what it hides in `shadows`.
 *
 * Owners come from `.ue-mcp/skills.json`, which records only what ue-mcp
 * installed into `.claude/skills`. A directory there that the record names
 * under `ue-mcp` is an installed copy of a packaged skill; under a plugin's
 * package name it is that plugin's; anything else, and everything in
 * `.agents/skills`, belongs to the project.
 */
export function listDaemonSkills(projectDir: string, opts: { packagedRoot?: string } = {}): DaemonSkill[] {
  const coreVersion = packageVersion();
  const ownership = readSkillOwnership(projectDir);
  const ownerOf = (id: string): DaemonSkillOwner => {
    const owner = Object.keys(ownership).find((o) => ownership[o].includes(id));
    if (owner === undefined) return { kind: "project" };
    if (owner === CORE_OWNER) return { kind: "ue-mcp", version: coreVersion };
    return { kind: "plugin", package: owner, version: pluginVersion(owner, projectDir) };
  };

  const byId = new Map<string, DaemonSkill>();
  const order: string[] = [];
  const add = (skill: DaemonSkill): void => {
    const winner = byId.get(skill.id);
    if (!winner) {
      byId.set(skill.id, skill);
      order.push(skill.id);
      return;
    }
    winner.shadows = [...(winner.shadows ?? []), { location: skill.location, ...(skill.folder ? { folder: skill.folder } : {}), dir: skill.dir }];
  };

  for (const folder of PROJECT_SKILL_FOLDERS) {
    const root = path.join(projectDir, ...folder.split("/"));
    for (const id of listSkills(root)) {
      add(readSkill(path.join(root, id), id, folder === ".claude/skills" ? ownerOf(id) : { kind: "project" }, "project", folder));
    }
  }
  const packaged = opts.packagedRoot ?? packagedSkillsRoot();
  for (const id of listSkills(packaged)) {
    add(readSkill(path.join(packaged, id), id, { kind: "ue-mcp", version: coreVersion }, "packaged"));
  }
  return order.map((id) => byId.get(id)!);
}
