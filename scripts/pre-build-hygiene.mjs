/**
 * What has to be true of the deployed tree before UnrealBuildTool runs.
 *
 * Both of these were written down as a recipe somebody was supposed to follow
 * by hand when new handlers came back "Unknown method" from a build that had
 * reported success. A recipe for recovering from a broken state is worth less
 * than not reaching the state.
 */
import fs from "node:fs";
import path from "node:path";

/**
 * Live Coding patch files left over from an earlier session.
 *
 * Live Coding writes `Module.patch_1.dll` beside the real binary and the
 * editor loads the newest patch on top of a freshly built DLL. So a clean
 * build produces correct output, the editor loads a stale patch over it, and
 * the handler that was just fixed still runs its old code. Deleting them is
 * the difference between a build being believable and not.
 */
export function findLiveCodingPatches(binariesDir) {
  if (!fs.existsSync(binariesDir)) return [];
  return fs
    .readdirSync(binariesDir)
    .filter((name) => /\.patch_\d+\.(dll|pdb|lib|exp)$/i.test(name))
    .map((name) => path.join(binariesDir, name));
}

/**
 * Sources present in the deployed tree that the authored tree does not have.
 *
 * UnrealBuildTool compiles what it finds, so a file left behind by a rename is
 * compiled alongside its replacement and the link fails on symbols defined
 * twice. The deployer prunes these, so finding any here means the deployed
 * tree was built from something other than a deploy: an edit made in place, or
 * a hand copy.
 */
export function findOrphanedSources(sourceDir, deployedDir) {
  if (!fs.existsSync(sourceDir) || !fs.existsSync(deployedDir)) return [];

  const collect = (root) => {
    const out = new Map();
    const walk = (dir, rel) => {
      for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (entry.name === "Binaries" || entry.name === "Intermediate" || entry.name === "Saved") {
          continue;
        }
        const full = path.join(dir, entry.name);
        const key = path.posix.join(rel, entry.name).toLowerCase();
        if (entry.isDirectory()) walk(full, path.posix.join(rel, entry.name));
        else if (/\.(cpp|h)$/i.test(entry.name)) out.set(key, full);
      }
    };
    walk(root, "");
    return out;
  };

  const authored = collect(sourceDir);
  const deployed = collect(deployedDir);
  return [...deployed.entries()].filter(([key]) => !authored.has(key)).map(([, full]) => full);
}

/** Delete the patches and report what went, or report an orphaned tree. */
export function runHygiene({ binariesDir, sourceDir, deployedDir, remove = true, log = console.error }) {
  const patches = findLiveCodingPatches(binariesDir);
  for (const p of patches) {
    if (remove) fs.rmSync(p, { force: true });
  }
  if (patches.length > 0) {
    log(
      `[ue-mcp] removed ${patches.length} Live Coding patch file(s) from ${binariesDir}. `
        + "The editor loads the newest patch on top of a freshly built DLL, so leaving them "
        + "makes a correct build behave like the old code.",
    );
  }

  const orphans = findOrphanedSources(sourceDir, deployedDir);
  if (orphans.length > 0) {
    log(
      `[ue-mcp] ${orphans.length} source file(s) in the deployed plugin are not in plugin/:\n`
        + orphans.slice(0, 8).map((o) => `    ${o}`).join("\n")
        + (orphans.length > 8 ? `\n    ...and ${orphans.length - 8} more` : "")
        + "\n  UnrealBuildTool compiles what it finds, so these are compiled alongside the "
        + "real sources. The deployer prunes them, so run `node scripts/deploy.mjs` and "
        + "build again rather than editing the deployed tree.",
    );
  }

  return { patches, orphans };
}
