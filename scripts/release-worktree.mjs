#!/usr/bin/env node
/**
 * The release worktree: one checkout reused by every release, so its build
 * output carries over and the gate build compiles only what changed (#1465).
 *
 *   node scripts/release-worktree.mjs [--path DIR] [--ref REF]
 *
 * Creates DIR (default: ue-mcp-release beside this checkout) as a detached
 * worktree at REF (default origin/main) the first time. After that it moves
 * the existing one to REF and removes untracked files, keeping the ignored
 * ones: tests/ue_mcp's Intermediate, Binaries and the deployed plugin are
 * what make the next build incremental. node_modules is reinstalled only when
 * package-lock.json changed.
 */
import { execFileSync } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const flag = (name, fallback) => {
  const i = argv.indexOf(name);
  return i === -1 ? fallback : argv[i + 1];
};
const target = path.resolve(flag("--path", path.join(path.dirname(ROOT), "ue-mcp-release")));
const ref = flag("--ref", "origin/main");
const STATE = ".release-worktree.json";

const git = (cwd, ...args) => execFileSync("git", args, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "inherit"] }).trim();

git(ROOT, "fetch", "-q", "origin");
if (!fs.existsSync(target)) {
  console.log(`creating ${target} at ${ref}`);
  git(ROOT, "worktree", "add", "--detach", target, ref);
} else {
  const common = path.resolve(target, git(target, "rev-parse", "--git-common-dir"));
  if (path.resolve(ROOT, git(ROOT, "rev-parse", "--git-common-dir")) !== common) {
    throw new Error(`${target} is not a worktree of this repository`);
  }
  console.log(`moving ${target} to ${ref}`);
  git(target, "checkout", "-q", "--detach", "--force", ref);
  // Untracked files go; ignored build output stays.
  git(target, "clean", "-q", "-fd");
}

const lock = fs.readFileSync(path.join(target, "package-lock.json"));
const hash = crypto.createHash("sha256").update(lock).digest("hex");
// Kept under node_modules, which is ignored, so the clean above leaves it.
const statePath = path.join(target, "node_modules", STATE);
const state = fs.existsSync(statePath) ? JSON.parse(fs.readFileSync(statePath, "utf8")) : {};
if (state.lockHash !== hash || !fs.existsSync(path.join(target, "node_modules"))) {
  console.log("package-lock.json changed: npm ci");
  execFileSync(process.platform === "win32" ? "npm.cmd" : "npm", ["ci", "--no-audit", "--no-fund"], { cwd: target, stdio: "inherit", shell: process.platform === "win32" });
}
fs.writeFileSync(statePath, `${JSON.stringify({ lockHash: hash, ref, head: git(target, "rev-parse", "HEAD") }, null, 2)}\n`);
console.log(`ready: ${target} at ${git(target, "rev-parse", "--short", "HEAD")}. Run the gates there with node scripts/release-run.mjs`);
