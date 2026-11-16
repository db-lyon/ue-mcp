// @session independent: The SKILL.md format and where skill folders sit; pure text and path rules.
/**
 * What a skill is on disk, said once: a folder holding a SKILL.md whose YAML
 * frontmatter carries `name` and `description`, shipped under a package's
 * `skills/` and installed into a project's `.claude/skills/`. Installing,
 * listing, checking and serving skills all read and write SKILL.md through
 * this module.
 */
import * as path from "node:path";
import yaml from "js-yaml";

/** A package's skills folder, relative to its root: ue-mcp's own and every plugin's. */
export const PACKAGE_SKILLS_DIR = "skills";

/** Where a project's installed skills live, relative to the project. */
export const PROJECT_SKILLS_DIR = ".claude/skills";

/** The skills folder of the package rooted at `pkgDir`. */
export function pluginSkillsRoot(pkgDir: string): string {
  return path.join(pkgDir, PACKAGE_SKILLS_DIR);
}

/** Where installed skills live for a project. */
export function projectSkillsRoot(projectDir: string): string {
  return path.join(projectDir, ...PROJECT_SKILLS_DIR.split("/"));
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

/** Set the frontmatter `name:`, adding the field or the whole block if missing. */
export function setFrontmatterName(text: string, name: string): string {
  const m = FRONTMATTER.exec(text);
  if (!m) return `---\nname: ${name}\n---\n\n${text}`;
  const line = /^name:[^\r\n]*$/m;
  const block = line.test(m[1]) ? m[1].replace(line, `name: ${name}`) : `name: ${name}\n${m[1]}`;
  const start = m.index + (m[0].startsWith("---\r\n") ? 5 : 4);
  return text.slice(0, start) + block + text.slice(start + m[1].length);
}
