import * as fs from "node:fs";
import * as path from "node:path";

export interface SkillsInstallResult {
  skillsDir: string;
  installed: string[];
  skipped: string[];
  error?: string;
}

/** Resolve the packaged skills directory (sibling of `dist/` in the published tarball). */
function packagedSkillsDir(): string {
  const here =
    import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname);
  return path.resolve(here, "..", "skills");
}

/**
 * Copy every `skills/<name>/SKILL.md` from the ue-mcp package into
 * `<projectDir>/.claude/skills/<name>/SKILL.md`. Overwrites any existing
 * SKILL.md from ue-mcp so updates propagate; does not touch skill files
 * that were added manually by the user (i.e. unrelated files in the
 * destination are left alone, and skills not present in the package are
 * left in place).
 */
export function installSkills(projectDir: string): SkillsInstallResult {
  const source = packagedSkillsDir();
  const dest = path.join(projectDir, ".claude", "skills");
  const result: SkillsInstallResult = { skillsDir: dest, installed: [], skipped: [] };

  if (!fs.existsSync(source)) {
    result.error = `Packaged skills not found at ${source}`;
    return result;
  }

  if (!fs.existsSync(dest)) fs.mkdirSync(dest, { recursive: true });

  for (const entry of fs.readdirSync(source, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const srcSkill = path.join(source, entry.name);
    const srcFile = path.join(srcSkill, "SKILL.md");
    if (!fs.existsSync(srcFile)) {
      result.skipped.push(entry.name);
      continue;
    }
    const destSkill = path.join(dest, entry.name);
    if (!fs.existsSync(destSkill)) fs.mkdirSync(destSkill, { recursive: true });
    const destFile = path.join(destSkill, "SKILL.md");
    fs.copyFileSync(srcFile, destFile);
    result.installed.push(entry.name);
  }

  return result;
}

export interface SkillsUninstallResult {
  skillsDir: string;
  removed: string[];
}

/**
 * Inverse of installSkills: remove every `<projectDir>/.claude/skills/<name>/SKILL.md`
 * whose `<name>` matches a directory in the packaged skills folder. Removes the
 * containing `<name>` directory only if it ends up empty (preserves any user
 * additions). Removes the parent `.claude/skills/` directory only if it ends
 * up empty too. Idempotent: a missing destination is a no-op.
 */
export function uninstallSkills(projectDir: string): SkillsUninstallResult {
  const source = packagedSkillsDir();
  const dest = path.join(projectDir, ".claude", "skills");
  const result: SkillsUninstallResult = { skillsDir: dest, removed: [] };

  if (!fs.existsSync(dest) || !fs.existsSync(source)) return result;

  for (const entry of fs.readdirSync(source, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const destSkill = path.join(dest, entry.name);
    const destFile = path.join(destSkill, "SKILL.md");
    if (fs.existsSync(destFile)) {
      fs.unlinkSync(destFile);
      result.removed.push(entry.name);
    }
    if (fs.existsSync(destSkill) && fs.readdirSync(destSkill).length === 0) {
      fs.rmdirSync(destSkill);
    }
  }

  if (fs.existsSync(dest) && fs.readdirSync(dest).length === 0) {
    fs.rmdirSync(dest);
  }

  return result;
}
