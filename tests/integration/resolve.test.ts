/**
 * `ue-mcp resolve <issue>`, spawned in a real git clone whose origin is a local
 * bare repository, with `gh` and `claude` replaced by recording shims at the
 * front of PATH. Nothing reaches GitHub and the real Claude Code never runs.
 */
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { plain, runCli, Sandbox, type CliResult } from "./harness.js";
import { installShim } from "./feedback-fakes.js";

let sb: Sandbox;
let gh: ReturnType<typeof installShim>;
let claude: ReturnType<typeof installShim>;
let remote: string;

const GIT_ENV = {
  GIT_AUTHOR_NAME: "Integration",
  GIT_AUTHOR_EMAIL: "integration@example.invalid",
  GIT_COMMITTER_NAME: "Integration",
  GIT_COMMITTER_EMAIL: "integration@example.invalid",
  GIT_TERMINAL_PROMPT: "0",
};

/**
 * Shell syntax for every shell the old command line went through: POSIX
 * substitution and backticks, and cmd's separators. Each one, run, would
 * leave a canary file in the clone.
 */
const HOSTILE_TITLE = 'Crash on "quoted" names $(touch canary-subst) `touch canary-tick` & echo x > canary-cmd & rem %PATH%';

function git(cwd: string, ...args: string[]): string {
  return execFileSync("git", args, { cwd, env: sb.env(GIT_ENV), encoding: "utf8", windowsHide: true }).trim();
}

/** A fresh clone of the shared origin, on main. */
function clone(name: string): string {
  const dir = sb.path("work", name);
  git(sb.root, "clone", "-q", remote, dir);
  return dir;
}

async function resolve(cwd: string, args: string[], env: Record<string, string> = {}): Promise<CliResult> {
  const r = await runCli(sb, ["resolve", ...args], { cwd, env: { ...GIT_ENV, ...env } });
  return { ...r, stdout: plain(r.stdout), stderr: plain(r.stderr) };
}

function issueFile(title: string, body: string): string {
  const file = sb.path(`issue-${Math.random().toString(36).slice(2, 8)}.json`);
  fs.writeFileSync(file, JSON.stringify({ title, body, labels: [{ name: "bug" }] }));
  return file;
}

beforeAll(() => {
  sb = new Sandbox();
  // owner/repo is read off the origin URL, so the bare repo sits at acme/widgets.git.
  remote = sb.path("remotes", "acme", "widgets.git").split(path.sep).join("/");
  fs.mkdirSync(remote, { recursive: true });
  git(remote, "init", "-q", "--bare");
  const seed = sb.path("seed");
  fs.mkdirSync(seed);
  git(seed, "init", "-q");
  git(seed, "checkout", "-q", "-b", "main");
  fs.writeFileSync(path.join(seed, "README.md"), "widgets\n");
  git(seed, "add", "README.md");
  git(seed, "commit", "-q", "-m", "seed");
  git(seed, "remote", "add", "origin", remote);
  git(seed, "push", "-q", "origin", "main");

  gh = installShim(sb, "gh", `
if (args[0] === "--version") return 0;
if (args[0] === "issue" && args[1] === "view") {
  process.stdout.write(fs.readFileSync(process.env.FAKE_GH_ISSUE, "utf8"));
  return 0;
}
if (args[0] === "api") {
  const input = args[args.indexOf("--input") + 1];
  fs.writeFileSync(path.join(process.cwd(), "..", path.basename(process.cwd()) + ".pr.json"), fs.readFileSync(input, "utf8"));
  if (process.env.FAKE_GH_API_FAIL) { console.error("HTTP 403: Resource not accessible"); return 1; }
  console.log("https://github.com/acme/widgets/pull/7");
  return 0;
}
console.error("fake gh: unexpected " + args.join(" "));
return 2;
`);
  claude = installShim(sb, "claude", `
if (args[0] === "--version") return 0;
const code = Number(process.env.FAKE_CLAUDE_EXIT || 0);
if (code !== 0) return code;
fs.writeFileSync("fix.txt", "fixed\\n");
cp.execFileSync("git", ["add", "fix.txt"]);
cp.execFileSync("git", ["commit", "-q", "-m", "Fix the thing"]);
return 0;
`);
});
afterAll(() => sb?.cleanup());

const prRequest = (work: string) => JSON.parse(fs.readFileSync(`${work}.pr.json`, "utf8")) as Record<string, string>;
const remoteBranches = () => git(remote, "branch", "--list", "--format=%(refname:short)").split("\n").filter(Boolean).sort();
const canaries = (dir: string) => fs.readdirSync(dir).filter((f) => f.startsWith("canary"));

describe("ue-mcp resolve", () => {
  it("hands the issue to claude, pushes its commit and opens the PR with the title passed verbatim", async () => {
    covers("cli.resolve");
    const work = clone("ok");
    const body = "Steps: open the `Arena` map and run $(whoami).";
    const r = await resolve(work, ["5", "--ci"], { FAKE_GH_ISSUE: issueFile(HOSTILE_TITLE, body) });
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(r.stdout).toContain("PR created: https://github.com/acme/widgets/pull/7");
    expect(r.stdout).toContain("1 commit(s) ready");
    // The run is unattended either way; the flag only says so.
    expect(plain(r.stdout)).toContain("--ci changes nothing: resolve always runs unattended.");

    const ghCalls = gh.calls().filter((c) => c.args[0] !== "--version");
    expect(ghCalls.map((c) => c.args.slice(0, 2))).toEqual([["issue", "view"], ["api", "repos/acme/widgets/pulls"]]);
    expect(ghCalls[0].args).toEqual(["issue", "view", "5", "--repo", "acme/widgets", "--json", "title,body,labels"]);
    expect(prRequest(work)).toMatchObject({ title: `Fix #5: ${HOSTILE_TITLE}`, head: "resolve/5", base: "main" });
    expect(prRequest(work).body).toContain("Resolves #5");

    const run = claude.calls().find((c) => c.args[0] !== "--version")!;
    expect(run.args).toEqual(["--print", "--dangerously-skip-permissions"]);
    expect(run.stdin).toContain(`## Issue: ${HOSTILE_TITLE}`);
    expect(run.stdin).toContain(body);
    expect(path.resolve(run.cwd)).toBe(path.resolve(work));

    expect(git(work, "rev-parse", "--abbrev-ref", "HEAD")).toBe("resolve/5");
    expect(remoteBranches()).toContain("resolve/5");
    expect(git(remote, "log", "-1", "--format=%s", "resolve/5")).toBe("Fix the thing");
    expect(canaries(work)).toEqual([]);
    expect(canaries(sb.root)).toEqual([]);
  });

  it("exits with claude's code when claude fails, and pushes and opens nothing", async () => {
    covers("cli.resolve");
    const work = clone("claude-fails");
    const before = gh.calls().length;
    const r = await resolve(work, ["6"], { FAKE_GH_ISSUE: issueFile("Widget spins forever", "It spins."), FAKE_CLAUDE_EXIT: "3" });
    expect(r.code).toBe(3);
    expect(plain(r.stdout)).not.toContain("--ci");
    expect(r.stdout).toContain("Claude exited with code 3. Branch resolve/6 preserved.");
    expect(git(work, "rev-parse", "--abbrev-ref", "HEAD")).toBe("resolve/6");
    expect(remoteBranches()).not.toContain("resolve/6");
    expect(gh.calls().slice(before).some((c) => c.args[0] === "api")).toBe(false);
  });

  it("exits non-zero when the pull request cannot be opened, leaving the branch pushed", async () => {
    covers("cli.resolve");
    const work = clone("pr-fails");
    const r = await resolve(work, ["8"], { FAKE_GH_ISSUE: issueFile("Widget leaks handles", "It leaks."), FAKE_GH_API_FAIL: "1" });
    expect(r.code).toBe(1);
    expect(r.stdout).toContain("Could not open the pull request. resolve/8 is pushed");
    expect(remoteBranches()).toContain("resolve/8");
  });
});
