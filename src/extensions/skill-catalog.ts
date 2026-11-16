// @session independent: Reads the skills shipped in this package; the same for every editor.
/**
 * The skills that ship with ue-mcp, read as documents: name, description and
 * body from each `skills/<name>/SKILL.md`. The server serves them as MCP
 * prompts and resources, and init lists them in the agent guides it writes,
 * so a client that never reads `.claude/skills` still reaches them.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { listSkills, packagedSkillsRoot } from "./skills.js";
import { parseSkillFile } from "./skill-file.js";

export interface ShippedSkill {
  /** The directory name, which is also the frontmatter name. */
  name: string;
  description: string;
  /** The SKILL.md text without its frontmatter. */
  body: string;
  /** The whole SKILL.md as shipped. */
  text: string;
  file: string;
}

/** The URI a skill is read at through resources/read. */
export function skillResourceUri(name: string): string {
  return `skill://ue-mcp/${name}`;
}

/** A SKILL.md's `name` and `description` and its body. */
export function parseSkill(text: string): { name?: string; description?: string; body: string } {
  const { frontmatter: fields, body } = parseSkillFile(text);
  return {
    ...(typeof fields.name === "string" ? { name: fields.name } : {}),
    ...(typeof fields.description === "string" ? { description: fields.description.trim() } : {}),
    body: body.replace(/^\s+/, ""),
  };
}

const cache = new Map<string, ShippedSkill[]>();

/** Every skill under `root` (the package's own by default), sorted by name. */
export function shippedSkills(root: string = packagedSkillsRoot()): ShippedSkill[] {
  const hit = cache.get(root);
  if (hit) return hit;
  const out: ShippedSkill[] = [];
  for (const dir of listSkills(root)) {
    const file = path.join(root, dir, "SKILL.md");
    let text: string;
    try {
      text = fs.readFileSync(file, "utf8");
    } catch {
      continue;
    }
    const parsed = parseSkill(text);
    out.push({ name: dir, description: parsed.description ?? "", body: parsed.body, text, file });
  }
  cache.set(root, out);
  return out;
}
