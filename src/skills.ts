import {
  installPacks,
  packagedSkillsRoot,
  projectSkillsRoot,
  readSkillRoot,
  uninstallPacks,
} from "./skill-packs.js";

export interface SkillsInstallResult {
  skillsDir: string;
  installed: string[];
  skipped: string[];
  error?: string;
}

/**
 * Copy every packaged skill pack into `<projectDir>/.claude/skills/<name>/`.
 *
 * A pack is its `SKILL.md` plus the optional `ue-mcp.yml` sidecar that
 * declares what the pack needs. Overwrites the ue-mcp-owned files so updates
 * propagate; anything else in the destination is left alone, including skills
 * that did not come from this package.
 *
 * The copying itself lives in `skill-packs.ts`, which is also what the
 * `flow(skill_install)` action calls. Two implementations of "install a skill"
 * would drift, and the one nobody ran would be the one that broke.
 */
export function installSkills(projectDir: string): SkillsInstallResult {
  const packs = readSkillRoot(packagedSkillsRoot(), "packaged");
  const dest = projectSkillsRoot(projectDir);
  if (packs.length === 0) {
    return {
      skillsDir: dest,
      installed: [],
      skipped: [],
      error: `Packaged skills not found at ${packagedSkillsRoot()}`,
    };
  }
  const result = installPacks(packs, dest);
  return {
    skillsDir: result.skillsDir,
    // Callers of this function report "installed" to the user after an init or
    // a deploy, where an unchanged pack is still an installed one. The
    // change/no-change split is reported by `flow(skill_install)`, which is
    // where it is actionable.
    installed: [...result.installed, ...result.unchanged].sort(),
    skipped: result.skipped,
    error: result.error,
  };
}

export interface SkillsUninstallResult {
  skillsDir: string;
  removed: string[];
}

/**
 * Inverse of installSkills: remove every installed pack whose name matches a
 * packaged one. The containing directory goes only if it ends up empty, and
 * the skills root only if it does too, so user additions survive. Idempotent:
 * a missing destination is a no-op.
 */
export function uninstallSkills(projectDir: string): SkillsUninstallResult {
  const names = readSkillRoot(packagedSkillsRoot(), "packaged").map((p) => p.name);
  const result = uninstallPacks(names, projectSkillsRoot(projectDir));
  return { skillsDir: result.skillsDir, removed: result.removed };
}
