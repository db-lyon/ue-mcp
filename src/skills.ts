/**
 * Claude Code skills: the `SKILL.md` guides that ship with ue-mcp and with
 * ue-mcp plugins, copied into `<project>/.claude/skills/` so the harness loads
 * them.
 *
 * A skill is a directory holding a `SKILL.md`. ue-mcp ships its own under
 * `<package>/skills/`; a plugin ships its own the same way, under
 * `<plugin package>/skills/`, with no manifest key.
 *
 * Every installed directory is recorded against the package that owns it in
 * `<project>/.ue-mcp/skills.json`. That record is what lets uninstall remove
 * exactly one owner's skills, and what stops one owner overwriting another's.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { nearestActions } from "./action-schema.js";
import { readPluginsList } from "./plugin/plugins-list.js";
import { findInstalledPackage } from "./plugin/resolver.js";
import { pluginSlug } from "./plugin/plugin-groups.js";

/** The owner name recorded for the skills that ship with ue-mcp itself. */
export const CORE_OWNER = "ue-mcp";

/** The `skills/` directory shipped in this package (sibling of `dist/`). */
export function packagedSkillsRoot(): string {
  const here = import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname);
  return path.resolve(here, "..", "skills");
}

/** Where installed skills live for a project. */
export function projectSkillsRoot(projectDir: string): string {
  return path.join(projectDir, ".claude", "skills");
}

/** Skill names under a root: every subdirectory that holds a SKILL.md. */
export function listSkills(root: string): string[] {
  if (!fs.existsSync(root)) return [];
  return fs
    .readdirSync(root, { withFileTypes: true })
    .filter((e) => e.isDirectory() && fs.existsSync(path.join(root, e.name, "SKILL.md")))
    .map((e) => e.name)
    .sort();
}

/* ── ownership record ──────────────────────────────────────────────── */

/** Owner (`ue-mcp` or a plugin's npm name) to the skill names it installed. */
export type SkillOwnership = Record<string, string[]>;

function stateFile(projectDir: string): string {
  return path.join(projectDir, ".ue-mcp", "skills.json");
}

export function readSkillOwnership(projectDir: string): SkillOwnership {
  try {
    const raw = JSON.parse(fs.readFileSync(stateFile(projectDir), "utf-8"));
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) return {};
    const out: SkillOwnership = {};
    for (const [owner, names] of Object.entries(raw)) {
      if (Array.isArray(names)) out[owner] = names.filter((n): n is string => typeof n === "string");
    }
    return out;
  } catch {
    return {};
  }
}

function writeSkillOwnership(projectDir: string, state: SkillOwnership): void {
  const file = stateFile(projectDir);
  const owners = Object.keys(state).filter((k) => state[k].length > 0).sort();
  if (owners.length === 0) {
    if (fs.existsSync(file)) fs.unlinkSync(file);
    return;
  }
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const sorted = Object.fromEntries(owners.map((k) => [k, [...state[k]].sort()]));
  fs.writeFileSync(file, JSON.stringify(sorted, null, 2) + "\n");
}

/* ── install and remove ────────────────────────────────────────────── */

export interface SkillInstallResult {
  skillsDir: string;
  /** Written for the first time, or overwritten with new content. */
  installed: string[];
  /** Destination already held the same files. */
  unchanged: string[];
  /** Installed by this owner before, no longer shipped, now deleted. */
  pruned: string[];
  /** Refused: the name belongs to another owner, or to the user. */
  conflicts: Array<{ skill: string; heldBy: string }>;
}

/**
 * The prefix a plugin's skills install under: the package name without its
 * scope separator or `ue-mcp-`, reduced to the characters a skill name allows.
 * `ue-mcp-meshy` gives `meshy`, `@studio/ue-mcp-foo` gives `studio-foo`.
 */
export function skillNamespace(pkgName: string): string {
  const scoped = /^@([^/]+)\/(.+)$/.exec(pkgName);
  const base = pluginSlug(scoped ? scoped[2] : pkgName);
  return (scoped ? `${scoped[1]}-${base}` : base)
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "");
}

/**
 * The directory, and frontmatter `name:`, a skill installs as. A plugin's
 * skills are prefixed with its namespace so two plugins can ship the same
 * name; one already carrying the prefix keeps it once. ue-mcp's own are not.
 */
export function installedSkillName(owner: string, skill: string): string {
  if (owner === CORE_OWNER) return skill;
  const ns = skillNamespace(owner);
  return skill === ns || skill.startsWith(`${ns}-`) ? skill : `${ns}-${skill}`;
}

/**
 * Install every skill under `sourceRoot` for one owner, and prune the ones that
 * owner installed before but no longer ships.
 *
 * A name another owner holds is refused rather than overwritten. So is an
 * untracked directory the user put there, unless its files already match.
 * The one exception is ue-mcp's own names, which earlier versions installed
 * without recording them.
 */
export function installSkillSet(
  projectDir: string,
  owner: string,
  sourceRoot: string,
): SkillInstallResult {
  const destRoot = projectSkillsRoot(projectDir);
  const state = readSkillOwnership(projectDir);
  const previous = new Set(state[owner] ?? []);
  const result: SkillInstallResult = {
    skillsDir: destRoot,
    installed: [],
    unchanged: [],
    pruned: [],
    conflicts: [],
  };
  const owned: string[] = [];

  for (const skill of listSkills(sourceRoot)) {
    const name = installedSkillName(owner, skill);
    const files = skillFiles(path.join(sourceRoot, skill), owner === CORE_OWNER ? undefined : name);
    const dest = path.join(destRoot, name);
    const heldBy = Object.keys(state).find((o) => o !== owner && state[o].includes(name));
    if (heldBy) {
      result.conflicts.push({ skill: name, heldBy });
      continue;
    }
    const current = matches(files, dest);
    const untracked = !previous.has(name) && fs.existsSync(dest);
    if (untracked && owner !== CORE_OWNER && !current) {
      result.conflicts.push({ skill: name, heldBy: "the user (untracked directory)" });
      continue;
    }
    if (current) {
      result.unchanged.push(name);
    } else {
      fs.rmSync(dest, { recursive: true, force: true });
      for (const [rel, data] of files) {
        fs.mkdirSync(path.dirname(path.join(dest, rel)), { recursive: true });
        fs.writeFileSync(path.join(dest, rel), data);
      }
      result.installed.push(name);
    }
    owned.push(name);
  }

  for (const name of previous) {
    if (owned.includes(name)) continue;
    fs.rmSync(path.join(destRoot, name), { recursive: true, force: true });
    result.pruned.push(name);
  }

  state[owner] = owned;
  writeSkillOwnership(projectDir, state);
  removeIfEmpty(destRoot);
  return result;
}

/** One line per refused skill, for a CLI to print as a warning. */
export function conflictMessages(result: SkillInstallResult): string[] {
  return result.conflicts.map(
    (c) => `skill '${c.skill}' not installed: .claude/skills/${c.skill} is held by ${c.heldBy}.`,
  );
}

/** Delete every skill directory one owner installed. Idempotent. */
export function removeSkillSet(projectDir: string, owner: string, legacyNames: string[] = []): string[] {
  const destRoot = projectSkillsRoot(projectDir);
  const state = readSkillOwnership(projectDir);
  // Before ownership was recorded, ue-mcp installed its own skills untracked,
  // so the caller passes those names in to catch that case.
  const names = new Set([...(state[owner] ?? []), ...legacyNames]);
  const removed: string[] = [];
  for (const name of names) {
    const dir = path.join(destRoot, name);
    if (!fs.existsSync(dir)) continue;
    fs.rmSync(dir, { recursive: true, force: true });
    removed.push(name);
  }
  delete state[owner];
  writeSkillOwnership(projectDir, state);
  removeIfEmpty(destRoot);
  return removed.sort();
}

/** ue-mcp's own skills: install, or remove on opt-out. */
export function installCoreSkills(projectDir: string): SkillInstallResult {
  return installSkillSet(projectDir, CORE_OWNER, packagedSkillsRoot());
}

export function removeCoreSkills(projectDir: string): string[] {
  return removeSkillSet(projectDir, CORE_OWNER, listSkills(packagedSkillsRoot()));
}

/** Whether ue-mcp's own skills are installed in this project. */
export function coreSkillsInstalled(projectDir: string): boolean {
  if ((readSkillOwnership(projectDir)[CORE_OWNER] ?? []).length > 0) return true;
  const root = projectSkillsRoot(projectDir);
  return listSkills(packagedSkillsRoot()).some((n) => fs.existsSync(path.join(root, n, "SKILL.md")));
}

export interface PluginSkillsSync {
  /** Per plugin that ships skills, what its install did. */
  plugins: Record<string, SkillInstallResult>;
  /** Plugins no longer in ue-mcp.yml whose skills were removed. */
  removed: Record<string, string[]>;
}

/**
 * Bring every plugin's skills in line with the project's `plugins:` list:
 * install or refresh each listed plugin's, and remove those of a plugin that
 * is no longer listed.
 */
export function syncPluginSkills(projectDir: string, configPath: string): PluginSkillsSync {
  const result: PluginSkillsSync = { plugins: {}, removed: {} };
  const listed = new Set<string>();
  for (const { name } of readPluginsList(configPath)) {
    listed.add(name);
    const pkgDir = findInstalledPackage(name, projectDir);
    if (!pkgDir) continue;
    const root = path.join(pkgDir, "skills");
    const hadSkills = (readSkillOwnership(projectDir)[name] ?? []).length > 0;
    if (listSkills(root).length === 0 && !hadSkills) continue;
    result.plugins[name] = installSkillSet(projectDir, name, root);
  }
  for (const owner of Object.keys(readSkillOwnership(projectDir))) {
    if (owner === CORE_OWNER || listed.has(owner)) continue;
    result.removed[owner] = removeSkillSet(projectDir, owner);
  }
  return result;
}

function listFiles(root: string): string[] {
  return (fs.readdirSync(root, { recursive: true, withFileTypes: true }) as fs.Dirent[])
    .filter((e) => e.isFile())
    .map((e) => path.relative(root, path.join(e.parentPath ?? e.path, e.name)))
    .sort();
}

/** A skill's files as they will be installed, with the frontmatter name set
 *  when `rename` is given. */
function skillFiles(src: string, rename?: string): Map<string, Buffer> {
  const files = new Map<string, Buffer>();
  for (const rel of listFiles(src)) {
    const data = fs.readFileSync(path.join(src, rel));
    files.set(
      rel,
      rel === "SKILL.md" && rename
        ? Buffer.from(setFrontmatterName(data.toString("utf-8"), rename), "utf-8")
        : data,
    );
  }
  return files;
}

function matches(files: Map<string, Buffer>, dest: string): boolean {
  if (!fs.existsSync(dest)) return false;
  const present = listFiles(dest);
  if (present.length !== files.size) return false;
  return present.every((rel) => files.get(rel)?.equals(fs.readFileSync(path.join(dest, rel))) ?? false);
}

/** Set the frontmatter `name:`, adding the field or the whole block if missing. */
export function setFrontmatterName(text: string, name: string): string {
  const m = /^---\r?\n([\s\S]*?)\r?\n---/.exec(text);
  if (!m) return `---\nname: ${name}\n---\n\n${text}`;
  const line = /^name:[^\r\n]*$/m;
  const block = line.test(m[1]) ? m[1].replace(line, `name: ${name}`) : `name: ${name}\n${m[1]}`;
  const start = m.index + (m[0].startsWith("---\r\n") ? 5 : 4);
  return text.slice(0, start) + block + text.slice(start + m[1].length);
}

function removeIfEmpty(dir: string): void {
  if (fs.existsSync(dir) && fs.readdirSync(dir).length === 0) fs.rmdirSync(dir);
}

/* ── checking ──────────────────────────────────────────────────────── */

export interface SkillProblem {
  skill: string;
  detail: string;
  didYouMean?: string[];
}

export interface SkillCheckResult {
  checked: string[];
  problems: SkillProblem[];
  /** References to categories this check has no knowledge of, such as
   *  another plugin's. Neither confirmed nor refuted. */
  unverified: string[];
}

/**
 * Every `category(action="name")` a skill body teaches. Code fences count: a
 * dead call in a code block is the one most likely to be copied verbatim.
 */
export function extractActionReferences(body: string): string[] {
  const found = new Set<string>();
  for (const m of body.matchAll(
    /\b([a-z][a-z0-9_]*)\s*\(\s*action\s*[=:]\s*["']([A-Za-z0-9_]+)["']/g,
  )) {
    found.add(`${m[1]}.${m[2]}`);
  }
  return [...found].sort();
}

/**
 * Check every skill under `root` against a set of `category.action` names:
 * each must have a frontmatter description, a name matching its directory
 * (unless the installer sets it, as for plugin skills), and teach only
 * actions that exist.
 */
export function checkSkills(
  root: string,
  known: ReadonlySet<string>,
  opts: { requireNameMatch?: boolean } = {},
): SkillCheckResult {
  const categories = new Set([...known].map((a) => a.slice(0, a.indexOf("."))));
  const result: SkillCheckResult = { checked: listSkills(root), problems: [], unverified: [] };
  const unverified = new Set<string>();

  for (const skill of result.checked) {
    const file = path.join(root, skill, "SKILL.md");
    const text = fs.readFileSync(file, "utf-8");
    const frontmatter = readFrontmatter(text);
    if (typeof frontmatter.description !== "string" || !frontmatter.description.trim()) {
      result.problems.push({
        skill,
        detail: `${file} has no 'description:' in its frontmatter, so Claude Code never loads it.`,
      });
    }
    if (opts.requireNameMatch !== false && typeof frontmatter.name === "string" && frontmatter.name !== skill) {
      result.problems.push({
        skill,
        detail: `frontmatter name '${frontmatter.name}' does not match the directory '${skill}'.`,
      });
    }
    for (const ref of extractActionReferences(text)) {
      if (known.has(ref)) continue;
      const [category, action] = [ref.slice(0, ref.indexOf(".")), ref.slice(ref.indexOf(".") + 1)];
      if (!categories.has(category)) {
        unverified.add(ref);
        continue;
      }
      const siblings = [...known]
        .filter((a) => a.startsWith(`${category}.`))
        .map((a) => a.slice(category.length + 1));
      result.problems.push({
        skill,
        detail: `teaches '${ref}', which does not exist.`,
        didYouMean: nearestActions(action, siblings).map((a) => `${category}.${a}`),
      });
    }
  }
  result.unverified = [...unverified].sort();
  return result;
}

function readFrontmatter(text: string): Record<string, unknown> {
  const m = /^---\r?\n([\s\S]*?)\r?\n---/.exec(text);
  if (!m) return {};
  try {
    const doc = yaml.load(m[1]);
    return doc && typeof doc === "object" && !Array.isArray(doc) ? (doc as Record<string, unknown>) : {};
  } catch {
    return {};
  }
}
