/**
 * `ue-mcp auth`, spawned with no terminal, against a local fake of GitHub's
 * device flow: what it caches, where, and the exit code each ending gives.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { plain, runCli, Sandbox, type CliResult } from "./harness.js";
import { FakeGitHub, serviceEnv } from "./feedback-fakes.js";

let sb: Sandbox;
let github: FakeGitHub;

beforeAll(async () => {
  sb = new Sandbox();
  github = await FakeGitHub.start({ login: "octo" });
});
afterAll(async () => {
  await github?.stop();
  sb?.cleanup();
});

/** A fresh auth directory, outside ~/.ue-mcp, so the printed path has to be the real one. */
function authDir(name: string): string {
  const dir = sb.path("auth", name);
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

async function auth(dir: string): Promise<CliResult> {
  const r = await runCli(sb, ["auth"], { env: { ...serviceEnv({ github }), UE_MCP_AUTH_DIR: dir } });
  return { ...r, stdout: plain(r.stdout), stderr: plain(r.stderr) };
}

describe("ue-mcp auth", () => {
  it("signs in through the device flow without a terminal and caches the token where it says", async () => {
    covers("cli.auth");
    github.script({ tokens: [{ error: "authorization_pending" }, { access_token: "gho_cli" }] });
    const before = github.requests.length;
    const dir = authDir("ok");
    const r = await auth(dir);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(r.stdout).toContain(`Open: ${github.origin}/login/device`);
    expect(r.stdout).toContain("Enter code: WDJB-MJHT");
    expect(r.stdout).toContain("Authorized as @octo");
    expect(r.stdout).toContain(`Token cached at ${path.join(dir, "auth.json")}`);
    expect(JSON.parse(fs.readFileSync(path.join(dir, "auth.json"), "utf8"))).toMatchObject({ token: "gho_cli", login: "octo" });
    expect(fs.existsSync(path.join(dir, "device-pending.json"))).toBe(false);
    expect(fs.existsSync(path.join(sb.home, ".ue-mcp", "auth.json"))).toBe(false);

    const seen = github.requests.slice(before).map((q) => q.url);
    expect(seen).toEqual(["/login/device/code", "/login/oauth/access_token", "/login/oauth/access_token", "/user"]);
    const exchange = JSON.parse(github.hits("/login/oauth/access_token").at(-1)!.body);
    expect(exchange).toMatchObject({ device_code: "dev-code-1", grant_type: "urn:ietf:params:oauth:grant-type:device_code" });
    expect(github.hits("/user").at(-1)!.headers.authorization).toBe("token gho_cli");

    // Signed in already: reused, nothing asked of GitHub.
    const count = github.requests.length;
    const again = await auth(dir);
    expect(again.code).toBe(0);
    expect(again.stdout).toContain("@octo (cached token reused)");
    expect(github.requests.length).toBe(count);
  });

  it("exits non-zero and caches nothing when the user denies or the code expires", async () => {
    covers("cli.auth");
    for (const error of ["access_denied", "expired_token"]) {
      github.script({ tokens: [{ error }] });
      const dir = authDir(error);
      const r = await auth(dir);
      expect(r.code, `${error}: ${r.stdout}`).toBe(1);
      expect(r.stdout).toMatch(error === "access_denied" ? /Authorization denied/ : /Device code expired/);
      expect(fs.readdirSync(dir)).toEqual([]);
    }
  });

  it("waits longer between polls after GitHub says slow_down", async () => {
    covers("cli.auth");
    github.script({ interval: 0.05, tokens: [{ error: "slow_down", interval: 1.5 }, { access_token: "gho_slow" }] });
    const dir = authDir("slow");
    const r = await auth(dir);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const [first, second] = github.pollTimes().slice(-2);
    expect(second - first).toBeGreaterThanOrEqual(1400);
    expect(JSON.parse(fs.readFileSync(path.join(dir, "auth.json"), "utf8")).token).toBe("gho_slow");
  });

  it("--json prints the code, then the account, one JSON object per line", async () => {
    covers("cli.auth.json");
    github.script({ tokens: [{ error: "authorization_pending" }, { access_token: "gho_json" }] });
    const dir = authDir("json");
    const run = (d: string) => runCli(sb, ["auth", "--json"], { env: { ...serviceEnv({ github }), UE_MCP_AUTH_DIR: d } });
    const r = await run(dir);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const lines = r.stdout.trim().split(/\r?\n/).map((l) => JSON.parse(l));
    expect(lines).toEqual([
      { event: "code", verificationUri: `${github.origin}/login/device`, userCode: "WDJB-MJHT", expiresAt: expect.any(Number), interval: expect.any(Number) },
      { event: "signed-in", login: "octo", cached: false },
    ]);
    expect(JSON.parse(fs.readFileSync(path.join(dir, "auth.json"), "utf8")).token).toBe("gho_json");

    const again = await run(dir);
    expect(again.code).toBe(0);
    expect(JSON.parse(again.stdout)).toEqual({ event: "signed-in", login: "octo", cached: true });

    github.script({ tokens: [{ error: "access_denied" }] });
    const denied = await run(authDir("json-denied"));
    expect(denied.code).toBe(1);
    expect(denied.stdout.trim().split(/\r?\n/).map((l) => JSON.parse(l)).at(-1)).toEqual({ event: "failed", reason: "denied" });
  });
});
