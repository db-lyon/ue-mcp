/**
 * Skill packs: the workflow knowledge that ships alongside the action surface,
 * and the machinery that keeps it honest.
 *
 * A skill pack is a `SKILL.md` under a named directory. The six that ship with
 * this package are copied into `<project>/.claude/skills/` by `ue-mcp init`,
 * and until now that was the whole story: nothing listed them, nothing read
 * them back, and nothing checked that the calls they teach still exist.
 *
 * That last one is the defect this module exists to close. A pack is prose
 * that names actions, so a renamed or removed action turns it into a document
 * that confidently teaches a call the server will refuse - the same silent
 * failure shape the parameter-drift test was written for, one layer up. Every
 * `category(action="x")` reference in a pack body is extracted here and
 * checked against the live tool graph, so a pack cannot rot unnoticed.
 *
 * Three things a pack can declare, in an optional `ue-mcp.yml` beside its
 * `SKILL.md`, because they cannot be derived from the prose:
 *
 *   uePlugins      Unreal plugins that must be enabled for the pack to work
 *   unrealClasses  the engine types the workflow is about
 *   engine         the engine range it applies to
 *
 * Kept in a sidecar rather than in the `SKILL.md` frontmatter deliberately:
 * that frontmatter is read by the harness that loads the skill, and adding
 * keys it does not know about risks the pack being rejected outright. A
 * sidecar is invisible to that reader and carries no such risk. A `ue-mcp:`
 * block inside the frontmatter is still honoured for anyone who prefers one
 * file, with the sidecar winning if both exist.
 *
 * Discovery covers three sources, and the third is the point: any ue-mcp
 * plugin package that ships a `skills/` directory has its packs discovered,
 * validated and installed exactly like the built-in ones. That is what makes
 * a domain pack a plugin's business rather than this repository's.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import type { ToolDef } from "./types.js";
import { nearestActions } from "./action-schema.js";

export type SkillPackSource = "packaged" | "project" | "plugin";

/** What a pack declares about the world it needs, from its sidecar. */
export interface SkillDeclarations {
  /** Category tools the workflow drives. */
  categories: string[];
  /** Fully-qualified `category.action` names the workflow depends on. */
  actions: string[];
  /** Unreal plugins that must be enabled for the workflow to run at all. */
  uePlugins: string[];
  /** Engine types the workflow is about, for orientation and search. */
  unrealClasses: string[];
  /** Words that should make an agent reach for this pack. */
  triggers: string[];
  /** Engine range the pack applies to, e.g. ">=5.8". */
  engine?: string;
}

export interface SkillPack {
  /** Directory name, which is also the installed skill's identity. */
  name: string;
  dir: string;
  file: string;
  source: SkillPackSource;
  /** The package that contributed it, for a plugin-sourced pack. */
  contributor?: string;
  /** `name:` from the SKILL.md frontmatter. */
  frontmatterName?: string;
  /** `description:` from the SKILL.md frontmatter - the text an agent reads to
   *  decide whether to load the pack, so its absence is a real defect. */
  description?: string;
  declarations: SkillDeclarations;
  /** Every `category(action="x")` the body teaches, deduplicated and sorted. */
  referenced: string[];
  bytes: number;
}

export interface SkillProblem {
  skill: string;
  /** Which copy of the pack the defect is in. The installed copy under a
   *  project can lag the packaged one, so naming the source is what makes the
   *  report actionable. */
  source: SkillPackSource;
  /** What kind of defect: a missing frontmatter field, an action that does not
   *  exist, a declaration that does not match the body. */
  kind:
    | "missing_description"
    | "name_mismatch"
    | "unknown_action"
    | "unknown_category"
    | "undeclared_dependency"
    | "unparsable_sidecar";
  detail: string;
  /** Closest real spellings, when the defect is a name that does not resolve. */
  didYouMean?: string[];
}

const EMPTY_DECLARATIONS: SkillDeclarations = {
  categories: [],
  actions: [],
  uePlugins: [],
  unrealClasses: [],
  triggers: [],
};

/* ── discovery ─────────────────────────────────────────────────────── */

/** The `skills/` directory shipped in this package (sibling of `dist/`). */
export function packagedSkillsRoot(): string {
  const here = import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname);
  return path.resolve(here, "..", "skills");
}

/** Where installed packs live for a project. */
export function projectSkillsRoot(projectDir: string): string {
  return path.join(projectDir, ".claude", "skills");
}

/**
 * Read every pack under one root. A directory with no `SKILL.md` is not a
 * pack and is skipped silently: `.claude/skills/` legitimately holds skills
 * this server knows nothing about.
 */
export function readSkillRoot(
  root: string,
  source: SkillPackSource,
  contributor?: string,
): SkillPack[] {
  if (!fs.existsSync(root)) return [];
  const packs: SkillPack[] = [];
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(root, { withFileTypes: true });
  } catch {
    return [];
  }
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    const dir = path.join(root, entry.name);
    const file = path.join(dir, "SKILL.md");
    if (!fs.existsSync(file)) continue;
    packs.push(readSkillPack(dir, source, contributor));
  }
  return packs.sort((a, b) => a.name.localeCompare(b.name));
}

/** The sidecar that carries what the prose cannot. */
export function sidecarPath(skillDir: string): string {
  return path.join(skillDir, "ue-mcp.yml");
}

/** Parse one pack directory. Never throws: an unreadable pack is reported as
 *  a pack with a problem, because a discovery call that dies on one bad file
 *  hides the other twenty. */
export function readSkillPack(
  dir: string,
  source: SkillPackSource,
  contributor?: string,
): SkillPack {
  const file = path.join(dir, "SKILL.md");
  let body = "";
  try {
    body = fs.readFileSync(file, "utf-8");
  } catch {
    body = "";
  }
  const { frontmatter, content } = splitFrontmatter(body);
  const inline = readDeclarationBlock(frontmatter["ue-mcp"]);
  const sidecar = readSidecar(sidecarPath(dir));
  return {
    name: path.basename(dir),
    dir,
    file,
    source,
    contributor,
    frontmatterName: typeof frontmatter.name === "string" ? frontmatter.name : undefined,
    description:
      typeof frontmatter.description === "string" ? frontmatter.description : undefined,
    // The sidecar wins field by field, so a pack can put triggers in the
    // frontmatter and plugin requirements in the sidecar without either
    // erasing the other.
    declarations: mergeDeclarations(inline, sidecar.declarations),
    referenced: extractActionReferences(content),
    bytes: Buffer.byteLength(body, "utf-8"),
  };
}

interface SidecarRead {
  declarations: SkillDeclarations;
  error?: string;
}

function readSidecar(file: string): SidecarRead {
  if (!fs.existsSync(file)) return { declarations: { ...EMPTY_DECLARATIONS } };
  try {
    const doc = yaml.load(fs.readFileSync(file, "utf-8"));
    // Both spellings are accepted: a bare document, and one wrapped in a
    // `ue-mcp:` block to match how project config is written.
    const block =
      doc && typeof doc === "object" && !Array.isArray(doc) && "ue-mcp" in doc
        ? (doc as Record<string, unknown>)["ue-mcp"]
        : doc;
    return { declarations: readDeclarationBlock(block) };
  } catch (e) {
    return { declarations: { ...EMPTY_DECLARATIONS }, error: (e as Error).message };
  }
}

function readDeclarationBlock(raw: unknown): SkillDeclarations {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return { ...EMPTY_DECLARATIONS };
  const block = raw as Record<string, unknown>;
  return {
    categories: stringList(block.categories),
    actions: stringList(block.actions),
    uePlugins: stringList(block.uePlugins ?? block["ue-plugins"]),
    unrealClasses: stringList(block.unrealClasses ?? block["unreal-classes"]),
    triggers: stringList(block.triggers),
    engine: typeof block.engine === "string" ? block.engine : undefined,
  };
}

function mergeDeclarations(a: SkillDeclarations, b: SkillDeclarations): SkillDeclarations {
  const pick = (x: string[], y: string[]): string[] => (y.length > 0 ? y : x);
  return {
    categories: pick(a.categories, b.categories),
    actions: pick(a.actions, b.actions),
    uePlugins: pick(a.uePlugins, b.uePlugins),
    unrealClasses: pick(a.unrealClasses, b.unrealClasses),
    triggers: pick(a.triggers, b.triggers),
    engine: b.engine ?? a.engine,
  };
}

function stringList(raw: unknown): string[] {
  if (typeof raw === "string") return raw.split(",").map((s) => s.trim()).filter(Boolean);
  if (!Array.isArray(raw)) return [];
  return raw.filter((x): x is string => typeof x === "string").map((s) => s.trim()).filter(Boolean);
}

/** Split a `---`-delimited YAML frontmatter block off the head of a document. */
export function splitFrontmatter(text: string): {
  frontmatter: Record<string, unknown>;
  content: string;
} {
  if (!text.startsWith("---")) return { frontmatter: {}, content: text };
  const end = text.indexOf("\n---", 3);
  if (end < 0) return { frontmatter: {}, content: text };
  const head = text.slice(text.indexOf("\n") + 1, end);
  const rest = text.slice(end + 4).replace(/^[^\n]*\n?/, "");
  try {
    const doc = yaml.load(head);
    return {
      frontmatter:
        doc && typeof doc === "object" && !Array.isArray(doc)
          ? (doc as Record<string, unknown>)
          : {},
      content: rest,
    };
  } catch {
    return { frontmatter: {}, content: rest };
  }
}

/**
 * Every action a pack body teaches.
 *
 * The prevailing spelling in these documents is `category(action="name", ...)`,
 * which is also how an agent writes the call, so matching it is matching the
 * thing that has to be true. Fenced code and inline code are included on
 * purpose: a dead call inside a code block is the one most likely to be copied
 * verbatim.
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
 * One pack per name, preferring the source the server is responsible for.
 *
 * A project's installed copy is usually the packaged one, so checking both
 * reports every defect twice and doubles a listing for no information. The
 * installed copy is kept only when nothing available carries that name, which
 * is exactly the case where it is the only copy there is.
 */
export function dedupePacks(packs: SkillPack[]): SkillPack[] {
  const byName = new Map<string, SkillPack>();
  for (const pack of packs) {
    const held = byName.get(pack.name);
    if (!held || (held.source === "project" && pack.source !== "project")) {
      byName.set(pack.name, pack);
    }
  }
  return [...byName.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/* ── validation ────────────────────────────────────────────────────── */

export interface SkillCheckResult {
  skillCount: number;
  problemCount: number;
  problems: SkillProblem[];
  /**
   * Actions a pack teaches that this build cannot verify either way.
   *
   * Empty in practice now. Unreal's wrapped tools used to land here, because
   * they only existed once a live editor had been read at startup, so a pack
   * teaching one could not be checked against anything. They are declared
   * actions now and resolve like any other; the field stays because a plugin
   * category that this session did not load is the same situation, and
   * reporting "cannot say" has to remain distinct from reporting "wrong".
   */
  enrichmentOnly: string[];
}

/**
 * Whether an unresolved reference is one this build cannot judge.
 *
 * It used to be every `epic_` reference, because those actions were injected
 * from a live catalog at startup and a cold check had nothing to hold them
 * against. They are declared now, so an `epic_` name that does not resolve is
 * a typo like any other and is reported as one. The category has to be absent
 * entirely for a reference to be unverifiable, which is the plugin case.
 */
function isUnverifiable(ref: string, categories: ReadonlySet<string>): boolean {
  const [category] = splitRef(ref);
  return !categories.has(category);
}

/**
 * Check every pack against a tool graph.
 *
 * `tools` is whatever the caller considers the live surface: the pristine
 * declaration off-server, or the enriched per-session graph on one. Passing
 * the enriched graph is what makes `epic_*` references verifiable too.
 */
export function checkSkillPacks(packs: SkillPack[], tools: ToolDef[]): SkillCheckResult {
  const actions = new Set<string>();
  const categories = new Set<string>();
  for (const tool of tools) {
    categories.add(tool.name);
    for (const action of Object.keys(tool.actions)) actions.add(`${tool.name}.${action}`);
  }
  const allActions = [...actions];
  const problems: SkillProblem[] = [];
  const enrichmentOnly = new Set<string>();

  for (const pack of packs) {
    if (!pack.description || pack.description.trim().length === 0) {
      problems.push({
        skill: pack.name,
        source: pack.source,
        kind: "missing_description",
        detail:
          `${pack.file} has no 'description:' in its frontmatter. That text is the only `
          + `thing an agent reads when deciding whether to load the pack, so a pack without `
          + `one is never loaded. Add a 'description:' saying when to use it.`,
      });
    }
    if (pack.frontmatterName && pack.frontmatterName !== pack.name) {
      problems.push({
        skill: pack.name,
        source: pack.source,
        kind: "name_mismatch",
        detail:
          `frontmatter name '${pack.frontmatterName}' does not match the directory `
          + `'${pack.name}'. The directory is the installed identity, so rename one of them `
          + `to '${pack.name}'.`,
      });
    }
    if (!fs.existsSync(pack.file)) continue;

    const sidecar = sidecarPath(pack.dir);
    if (fs.existsSync(sidecar)) {
      const read = readSidecar(sidecar);
      if (read.error) {
        problems.push({
          skill: pack.name,
          source: pack.source,
          kind: "unparsable_sidecar",
          detail: `${sidecar} is not valid YAML (${read.error}). Fix it or delete it; the pack still loads without one.`,
        });
      }
    }

    for (const category of pack.declarations.categories) {
      if (categories.has(category)) continue;
      problems.push({
        skill: pack.name,
        source: pack.source,
        kind: "unknown_category",
        detail:
          `declares category '${category}', which this server does not expose. `
          + `Available categories: ${[...categories].sort().join(", ")}.`,
      });
    }

    // Declared and referenced are checked together: both are promises the pack
    // makes about calls that exist, and a reader cannot tell them apart.
    const claimed = new Set([...pack.declarations.actions, ...pack.referenced]);
    for (const ref of claimed) {
      if (actions.has(ref)) continue;
      if (isUnverifiable(ref, categories)) {
        enrichmentOnly.add(ref);
        continue;
      }
      const [category, action] = splitRef(ref);
      // Suggest against the BARE action names, then put the category back:
      // scoring "lst" against "asset.list" finds nothing, because the shared
      // prefix drowns the part that was misspelled.
      const scoped = categories.has(category)
        ? allActions
            .filter((a) => a.startsWith(`${category}.`))
            .map((a) => a.slice(category.length + 1))
        : allActions;
      const prefix = categories.has(category) ? `${category}.` : "";
      problems.push({
        skill: pack.name,
        source: pack.source,
        kind: "unknown_action",
        detail:
          `teaches '${ref}', which no category exposes. A pack that names a call the server `
          + `refuses costs an agent a turn every time it is loaded. Update the pack, or `
          + `restore the action.`,
        didYouMean: nearestActions(action || ref, scoped).map((a) => `${prefix}${a}`),
      });
    }

    // A declaration list that omits an action the body teaches is a weaker
    // defect than a dead call: the pack works, but the dependency it actually
    // has is invisible to anyone deciding whether it applies here.
    for (const ref of pack.referenced) {
      if (pack.declarations.actions.length === 0) break;
      if (pack.declarations.actions.includes(ref)) continue;
      if (isUnverifiable(ref, categories)) continue;
      problems.push({
        skill: pack.name,
        source: pack.source,
        kind: "undeclared_dependency",
        detail:
          `body teaches '${ref}' but the ue-mcp.yml sidecar does not list it under 'actions:'. `
          + `Either add it, or drop the 'actions:' list entirely and let the body speak for itself.`,
      });
    }
  }

  return {
    skillCount: packs.length,
    problemCount: problems.length,
    problems,
    enrichmentOnly: [...enrichmentOnly].sort(),
  };
}

function splitRef(ref: string): [string, string] {
  const at = ref.indexOf(".");
  return at < 0 ? [ref, ""] : [ref.slice(0, at), ref.slice(at + 1)];
}

/* ── install and remove ────────────────────────────────────────────── */

export interface PackInstallResult {
  skillsDir: string;
  /** Packs written for the first time or overwritten with new content. */
  installed: string[];
  /** Packs whose destination was already byte-identical. */
  unchanged: string[];
  /** Directories that carried no SKILL.md and are therefore not packs. */
  skipped: string[];
  error?: string;
}

/**
 * Copy packs into a destination root, `SKILL.md` and sidecar together.
 *
 * Idempotent and honest about it: a pack whose destination already holds the
 * same bytes is reported as `unchanged` rather than counted as an install, so
 * a caller can tell a real update from a no-op. Files in the destination that
 * did not come from a pack are never touched.
 */
export function installPacks(packs: SkillPack[], destRoot: string): PackInstallResult {
  const result: PackInstallResult = {
    skillsDir: destRoot,
    installed: [],
    unchanged: [],
    skipped: [],
  };
  if (packs.length === 0) {
    result.error =
      `No skill packs found to install. The packaged set lives at ${packagedSkillsRoot()}; `
      + `a plugin contributes packs by shipping a 'skills/<name>/SKILL.md' in its package.`;
    return result;
  }
  if (!fs.existsSync(destRoot)) fs.mkdirSync(destRoot, { recursive: true });

  for (const pack of packs) {
    if (!fs.existsSync(pack.file)) {
      result.skipped.push(pack.name);
      continue;
    }
    const destDir = path.join(destRoot, pack.name);
    if (!fs.existsSync(destDir)) fs.mkdirSync(destDir, { recursive: true });
    let changed = false;
    changed = copyIfDifferent(pack.file, path.join(destDir, "SKILL.md")) || changed;
    const sidecar = sidecarPath(pack.dir);
    if (fs.existsSync(sidecar)) {
      changed = copyIfDifferent(sidecar, path.join(destDir, "ue-mcp.yml")) || changed;
    }
    (changed ? result.installed : result.unchanged).push(pack.name);
  }
  return result;
}

function copyIfDifferent(from: string, to: string): boolean {
  if (fs.existsSync(to)) {
    try {
      if (fs.readFileSync(from).equals(fs.readFileSync(to))) return false;
    } catch {
      // Unreadable destination: fall through and overwrite it.
    }
  }
  fs.copyFileSync(from, to);
  return true;
}

export interface PackRemoveResult {
  skillsDir: string;
  removed: string[];
  /** Names that were asked for and were not installed. Not an error: removing
   *  a pack twice has to be safe. */
  absent: string[];
}

/**
 * Remove installed packs by name. Deletes the `SKILL.md` and the sidecar, then
 * the containing directory only if it ends up empty, then the skills root only
 * if that ends up empty too, so anything the user added alongside survives.
 */
export function uninstallPacks(names: string[], destRoot: string): PackRemoveResult {
  const result: PackRemoveResult = { skillsDir: destRoot, removed: [], absent: [] };
  if (!fs.existsSync(destRoot)) {
    result.absent = [...names];
    return result;
  }
  for (const name of names) {
    const dir = path.join(destRoot, name);
    const skill = path.join(dir, "SKILL.md");
    if (!fs.existsSync(skill)) {
      result.absent.push(name);
      continue;
    }
    fs.unlinkSync(skill);
    const sidecar = path.join(dir, "ue-mcp.yml");
    if (fs.existsSync(sidecar)) fs.unlinkSync(sidecar);
    if (fs.existsSync(dir) && fs.readdirSync(dir).length === 0) fs.rmdirSync(dir);
    result.removed.push(name);
  }
  if (fs.existsSync(destRoot) && fs.readdirSync(destRoot).length === 0) fs.rmdirSync(destRoot);
  return result;
}

/* ── presentation ──────────────────────────────────────────────────── */

/** A pack trimmed to a listing row. */
export function summarisePack(pack: SkillPack): Record<string, unknown> {
  return {
    name: pack.name,
    source: pack.source,
    contributor: pack.contributor,
    description: pack.description,
    categories:
      pack.declarations.categories.length > 0
        ? pack.declarations.categories
        : [...new Set(pack.referenced.map((r) => splitRef(r)[0]))].sort(),
    actionCount: pack.referenced.length,
    uePlugins: pack.declarations.uePlugins.length > 0 ? pack.declarations.uePlugins : undefined,
    engine: pack.declarations.engine,
    triggers: pack.declarations.triggers.length > 0 ? pack.declarations.triggers : undefined,
    bytes: pack.bytes,
    file: pack.file,
  };
}

/** A pack in full, including the body, for an agent that wants to read it
 *  without leaving the tool surface. */
export function detailPack(pack: SkillPack, includeBody: boolean): Record<string, unknown> {
  let body: string | undefined;
  if (includeBody) {
    try {
      body = fs.readFileSync(pack.file, "utf-8");
    } catch (e) {
      body = `(unreadable: ${(e as Error).message})`;
    }
  }
  return {
    ...summarisePack(pack),
    unrealClasses:
      pack.declarations.unrealClasses.length > 0 ? pack.declarations.unrealClasses : undefined,
    declaredActions:
      pack.declarations.actions.length > 0 ? pack.declarations.actions : undefined,
    referencedActions: pack.referenced,
    sidecar: fs.existsSync(sidecarPath(pack.dir)) ? sidecarPath(pack.dir) : undefined,
    body,
  };
}
