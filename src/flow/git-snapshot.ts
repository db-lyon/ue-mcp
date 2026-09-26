import * as fs from "node:fs";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
interface CallableBridge {
  isConnected: boolean;
  call(method: string, params: Record<string, unknown>): Promise<unknown>;
}

export interface Snapshot {
  treeHash: string;
  projectDir: string;
  gitDir: string;
  workTree: string;
  paths: string[];
  /** Absolute paths that existed at snapshot time. Used to diff on rollback. */
  tracked: Set<string>;
}

function git(gitDir: string, workTree: string, args: string[], input?: string): string {
  const fullArgs = [`--git-dir=${gitDir}`, `--work-tree=${workTree}`, ...args];
  const res = execFileSync("git", fullArgs, {
    input,
    encoding: "utf-8",
    stdio: input ? ["pipe", "pipe", "pipe"] : ["ignore", "pipe", "pipe"],
    maxBuffer: 1024 * 1024 * 256,
  });
  return res.trim();
}

function ensureShadowRepo(gitDir: string): void {
  if (fs.existsSync(path.join(gitDir, "HEAD"))) return;
  fs.mkdirSync(path.dirname(gitDir), { recursive: true });
  execFileSync("git", ["init", "--bare", "-q", gitDir], { stdio: "ignore" });
  // Set a permissive core.autocrlf so binary assets aren't mangled on Windows.
  execFileSync("git", [`--git-dir=${gitDir}`, "config", "core.autocrlf", "false"], { stdio: "ignore" });
  execFileSync("git", [`--git-dir=${gitDir}`, "config", "core.safecrlf", "false"], { stdio: "ignore" });
}

/**
 * Snapshot the given paths (relative to projectDir) into a shadow bare git repo.
 * Returns a handle that can be passed to `restoreSnapshot` to reset those paths.
 * Throws if any required tool (git) is missing.
 */
export function takeSnapshot(
  projectDir: string,
  paths: string[],
  snapshotDir: string,
): Snapshot {
  const gitDir = path.isAbsolute(snapshotDir)
    ? snapshotDir
    : path.join(projectDir, snapshotDir);
  ensureShadowRepo(gitDir);

  // Stage the requested paths into the shadow index, then write a tree.
  const tracked = new Set<string>();
  for (const rel of paths) {
    const abs = path.join(projectDir, rel);
    if (!fs.existsSync(abs)) continue;
    tracked.add(abs);
    git(gitDir, projectDir, ["add", "--force", "--", rel]);
  }
  const treeHash = git(gitDir, projectDir, ["write-tree"]);

  // Store the tree under a named ref so it isn't GC'd.
  const ref = `refs/ue-mcp/snapshots/${Date.now()}`;
  // commit-tree expects stdin; use --allow-empty-message via -m ""
  const commitHash = git(gitDir, projectDir, ["commit-tree", treeHash, "-m", "ue-mcp flow snapshot"]);
  git(gitDir, projectDir, ["update-ref", ref, commitHash]);

  return { treeHash, projectDir, gitDir, workTree: projectDir, paths, tracked };
}

/**
 * Restore the snapshotted paths. Files that were added after the snapshot are
 * removed; files that were modified are reverted; files that existed at
 * snapshot time are restored to their snapshot contents.
 */
export function restoreSnapshot(snap: Snapshot): { changedPaths: string[] } {
  // read-tree --reset -u resets the index AND working tree to the given tree,
  // scoped to the snapshotted paths via pathspec.
  const args = ["read-tree", "--reset", "-u", snap.treeHash, "--", ...snap.paths];
  git(snap.gitDir, snap.workTree, args);

  // Enumerate the paths now vs at snapshot time to hand the editor a reload list.
  const now = new Set<string>();
  function walk(dir: string): void {
    if (!fs.existsSync(dir)) return;
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.name.endsWith(".uasset") || entry.name.endsWith(".umap") || entry.name.endsWith(".ini")) {
        now.add(full);
      }
    }
  }
  for (const rel of snap.paths) walk(path.join(snap.projectDir, rel));

  const changed: string[] = [];
  for (const p of now) if (!snap.tracked.has(p)) changed.push(p);
  for (const p of snap.tracked) if (!now.has(p)) changed.push(p);
  return { changedPaths: changed };
}

/**
 * Best-effort call to reload_package on every Content-rooted .uasset / .umap
 * that changed during restore, so the editor drops stale in-memory copies.
 */
export async function reloadAffectedPackages(
  bridge: CallableBridge,
  projectDir: string,
  changedPaths: string[],
): Promise<{ reloaded: number; errors: number }> {
  const contentDir = path.join(projectDir, "Content");
  let reloaded = 0;
  let errors = 0;
  for (const abs of changedPaths) {
    if (!abs.startsWith(contentDir)) continue;
    const rel = path.relative(contentDir, abs).replace(/\\/g, "/").replace(/\.(uasset|umap)$/i, "");
    const assetPath = `/Game/${rel}`;
    try {
      await bridge.call("reload_package", { assetPath });
      reloaded++;
    } catch {
      errors++;
    }
  }
  return { reloaded, errors };
}

/**
 * Delete snapshot refs older than `maxAgeMs`. Call opportunistically so the
 * shadow repo doesn't grow unboundedly.
 */
export function pruneOldSnapshots(snapshotDir: string, maxAgeMs: number): void {
  if (!fs.existsSync(path.join(snapshotDir, "HEAD"))) return;
  try {
    const refs = execFileSync("git", [`--git-dir=${snapshotDir}`, "for-each-ref", "--format=%(refname)", "refs/ue-mcp/snapshots"], { encoding: "utf-8" });
    const now = Date.now();
    for (const ref of refs.split("\n").map(s => s.trim()).filter(Boolean)) {
      const ts = Number(ref.split("/").pop());
      if (Number.isFinite(ts) && now - ts > maxAgeMs) {
        execFileSync("git", [`--git-dir=${snapshotDir}`, "update-ref", "-d", ref], { stdio: "ignore" });
      }
    }
    execFileSync("git", [`--git-dir=${snapshotDir}`, "gc", "--prune=now", "--quiet"], { stdio: "ignore" });
  } catch {
    /* best effort */
  }
}
