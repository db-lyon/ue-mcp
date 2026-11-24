// @session per-session: Reads the source control state of the project directory it is handed.
/**
 * Which source control the project is under, and where it stands: Git,
 * Perforce, Unity Version Control (Plastic) or Subversion. Each is found by
 * its folder or its client's own answer, read with its command-line tool when
 * that is installed, and every command has a short timeout, so a server
 * asking for a login never holds anything up. The hub reads it to say which
 * revision a run tested.
 */
import { execFile } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

/** The project's source control. */
export interface VcsStatus {
  kind: "git" | "perforce" | "plastic" | "svn";
  /** Its product name, for display. */
  name: string;
  /** The branch, stream or workspace the project is on. */
  branch: string | null;
  /** More about where it is, such as the Perforce workspace of a stream. */
  detail?: string;
  /** Changed (Git, Plastic, Subversion) or opened (Perforce) files; null when the tool is not installed. */
  changes: number | null;
  /** Git only: commits ahead of and behind the upstream branch. */
  ahead?: number;
  behind?: number;
  root: string;
}

/** Run a tool in a folder; null when it is missing, fails or takes too long. */
export type Run = (cmd: string, args: string[], cwd: string) => Promise<string | null>;

const TIMEOUT_MS = 4000;

export const run: Run = (cmd, args, cwd) =>
  new Promise((resolve) => {
    execFile(cmd, args, { cwd, timeout: TIMEOUT_MS, windowsHide: true, maxBuffer: 8 * 1024 * 1024, env: { ...process.env, GIT_OPTIONAL_LOCKS: "0" } }, (err, stdout) =>
      resolve(err ? null : String(stdout)));
  });

/** The nearest folder at or above `dir` that holds `name`. */
export function findUp(dir: string, name: string): string | null {
  let cur = path.resolve(dir);
  for (;;) {
    if (fs.existsSync(path.join(cur, name))) return cur;
    const up = path.dirname(cur);
    if (up === cur) return null;
    cur = up;
  }
}

/** `git status --porcelain=v2 --branch`: the branch, ahead and behind its upstream, and the changed paths. */
export function parseGitStatus(out: string): { branch: string | null; ahead: number; behind: number; changes: number } {
  let branch: string | null = null;
  let oid: string | null = null;
  let ahead = 0;
  let behind = 0;
  let changes = 0;
  for (const line of out.split(/\r?\n/)) {
    if (line.startsWith("# branch.head ")) branch = line.slice(14).trim();
    else if (line.startsWith("# branch.oid ")) oid = line.slice(13).trim();
    else if (line.startsWith("# branch.ab ")) {
      const m = /\+(\d+) -(\d+)/.exec(line);
      if (m) [ahead, behind] = [Number(m[1]), Number(m[2])];
    } else if (line && !line.startsWith("#")) changes++;
  }
  // A detached HEAD reports "(detached)"; its short commit says more.
  if (branch === "(detached)") branch = oid && oid !== "(initial)" ? oid.slice(0, 8) : "detached";
  return { branch, ahead, behind, changes };
}

/** The branch a Plastic workspace's selector names. */
export function parsePlasticSelector(text: string): string | null {
  const m = /\b(?:smartbranch|br|branch)\s+"([^"]+)"/i.exec(text);
  return m ? m[1] : null;
}

/** `p4 -ztag -F %clientName%|%clientRoot%|%clientStream%|%userName% info`. */
export function parseP4Info(out: string): { client: string; root: string; stream: string; user: string } | null {
  const [client, root, stream, user] = out.trim().split("|");
  if (!client || client === "*unknown*" || !root) return null;
  return { client, root, stream: stream ?? "", user: user ?? "" };
}

const lines = (out: string | null): number | null => (out === null ? null : out.split(/\r?\n/).filter((l) => l.trim()).length);

const inside = (child: string, parent: string): boolean => {
  const rel = path.relative(path.resolve(parent).toLowerCase(), path.resolve(child).toLowerCase());
  return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
};

async function git(dir: string, r: Run): Promise<VcsStatus | null> {
  const root = findUp(dir, ".git");
  if (!root) return null;
  const out = await r("git", ["status", "--porcelain=v2", "--branch"], dir);
  if (out !== null) {
    const s = parseGitStatus(out);
    return { kind: "git", name: "Git", branch: s.branch, changes: s.changes, ahead: s.ahead, behind: s.behind, root };
  }
  // No git on PATH: the branch is still in HEAD.
  try {
    const head = fs.readFileSync(path.join(root, ".git", "HEAD"), "utf-8").trim();
    const m = /^ref: refs\/heads\/(.+)$/.exec(head);
    return { kind: "git", name: "Git", branch: m ? m[1] : head.slice(0, 8), changes: null, root };
  } catch {
    return { kind: "git", name: "Git", branch: null, changes: null, root };
  }
}

async function plastic(dir: string, r: Run): Promise<VcsStatus | null> {
  const root = findUp(dir, ".plastic");
  if (!root) return null;
  let branch: string | null = null;
  try {
    branch = parsePlasticSelector(fs.readFileSync(path.join(root, ".plastic", "plastic.selector"), "utf-8"));
  } catch {
    // An older workspace keeps no selector file; the CLI still knows.
  }
  if (!branch) {
    const wi = await r("cm", ["wi"], dir);
    branch = wi ? parsePlasticSelector(wi) ?? (/Branch\s+(\S+)/.exec(wi)?.[1] ?? null) : null;
  }
  return { kind: "plastic", name: "Unity Version Control", branch, changes: lines(await r("cm", ["status", "--short"], dir)), root };
}

async function svn(dir: string, r: Run): Promise<VcsStatus | null> {
  const root = findUp(dir, ".svn");
  if (!root) return null;
  const url = await r("svn", ["info", "--show-item", "relative-url"], dir);
  return { kind: "svn", name: "Subversion", branch: url?.trim() || null, changes: lines(await r("svn", ["status", "-q"], dir)), root };
}

async function perforce(dir: string, r: Run): Promise<VcsStatus | null> {
  const info = await r("p4", ["-ztag", "-F", "%clientName%|%clientRoot%|%clientStream%|%userName%", "info"], dir);
  const p = info ? parseP4Info(info) : null;
  if (!p || !inside(dir, p.root)) return null;
  const opened = await r("p4", ["-ztag", "-F", "%depotFile%", "opened", "-C", p.client], dir);
  return {
    kind: "perforce",
    name: "Perforce",
    branch: p.stream || p.client,
    detail: p.stream ? `workspace ${p.client}` : undefined,
    changes: lines(opened),
    root: p.root,
  };
}

/** The project's source control, nearest first by kind; null when it has none this can see. */
export async function detectVcs(projectDir: string, r: Run = run): Promise<VcsStatus | null> {
  for (const probe of [git, plastic, svn, perforce]) {
    const found = await probe(projectDir, r);
    if (found) return found;
  }
  return null;
}

/**
 * The revision the workspace is at: Git's commit, the newest Perforce
 * changelist the workspace has synced, Subversion's revision or Plastic's
 * changeset. Null when it cannot be read.
 */
export async function revisionOf(vcs: VcsStatus, projectDir: string, r: Run = run): Promise<string | null> {
  let out: string | null = null;
  if (vcs.kind === "git") out = await r("git", ["rev-parse", "HEAD"], vcs.root);
  else if (vcs.kind === "perforce") {
    const info = await r("p4", ["-ztag", "-F", "%clientName%|%clientRoot%|%clientStream%|%userName%", "info"], projectDir);
    const client = info ? parseP4Info(info)?.client : null;
    if (client) out = await r("p4", ["-ztag", "-F", "%change%", "changes", "-m1", `//${client}/...#have`], projectDir);
  } else if (vcs.kind === "svn") out = await r("svn", ["info", "--show-item", "revision"], vcs.root);
  else if (vcs.kind === "plastic") out = /\bcs:(\d+)/.exec((await r("cm", ["status", "--header"], vcs.root)) ?? "")?.[1] ?? null;
  return out?.trim().split(/\r?\n/)[0]?.trim() || null;
}
