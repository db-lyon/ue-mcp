/**
 * Local stand-ins for the services feedback, auth and resolve talk to: the
 * feedback signing endpoint, GitHub's OAuth device flow and REST API, the
 * plugin registry's catalog, and `gh` and `claude` on PATH.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { FakeHttp, type FakeRequest, type Sandbox } from "./harness.js";

/** The signing service: files every report it is sent, numbering from 101. */
export async function fakeSigning(): Promise<FakeHttp & { reports: () => Array<{ title: string; body: string; labels: string[]; repo: string }> }> {
  let next = 101;
  const http = await FakeHttp.start((req) => {
    if (req.method !== "POST") return { status: 405, body: "" };
    const report = JSON.parse(req.body) as { repo: string };
    const number = next++;
    return { json: { ok: true, url: `https://github.com/${report.repo}/issues/${number}`, number, repo: report.repo, authoredBy: "ue-mcp-feedback[bot]" } };
  });
  return Object.assign(http, {
    reports: () => http.requests.filter((r) => r.method === "POST").map((r) => JSON.parse(r.body)),
  });
}

/** The plugin registry, publishing `plugins` at /api/plugins. */
export function fakeRegistry(plugins: unknown[]): Promise<FakeHttp> {
  return FakeHttp.start((req) => (req.url.startsWith("/api/plugins") ? { json: { plugins } } : { status: 404, body: "" }));
}

/** One answer the token endpoint gives, in order; the last one repeats. */
export type TokenAnswer = { error: string; interval?: number } | { access_token: string };

export interface FakeGitHubOptions {
  login?: string;
  /** What /login/oauth/access_token answers, poll by poll. */
  tokens?: TokenAnswer[];
  interval?: number;
}

/**
 * GitHub's OAuth origin and REST API on one port. Every request is stamped with
 * the time it arrived, so a test can read the poll cadence back.
 */
export class FakeGitHub {
  readonly arrivals: number[] = [];
  private polls = 0;
  private issueNumber = 500;
  private constructor(readonly http: FakeHttp, private opts: FakeGitHubOptions) {}

  static async start(opts: FakeGitHubOptions = {}): Promise<FakeGitHub> {
    let self: FakeGitHub | undefined;
    const http = await FakeHttp.start((req) => self!.answer(req));
    self = new FakeGitHub(http, opts);
    return self;
  }

  /** Replace the scripted token answers and restart the poll count. */
  script(opts: FakeGitHubOptions): void {
    this.opts = { ...this.opts, ...opts };
    this.polls = 0;
  }

  get origin(): string {
    return this.http.origin;
  }

  get requests(): FakeRequest[] {
    return this.http.requests;
  }

  /** Requests whose path starts with `prefix`. */
  hits(prefix: string): FakeRequest[] {
    return this.http.requests.filter((r) => r.url.startsWith(prefix));
  }

  /** The times the token endpoint was polled, in ms since the epoch. */
  pollTimes(): number[] {
    return this.http.requests
      .map((r, i) => ({ r, t: this.arrivals[i] }))
      .filter(({ r }) => r.url.startsWith("/login/oauth/access_token"))
      .map(({ t }) => t);
  }

  private answer(req: FakeRequest): { status?: number; json?: unknown; body?: string } {
    this.arrivals.push(Date.now());
    if (req.url === "/login/device/code") {
      return {
        json: {
          device_code: "dev-code-1",
          user_code: "WDJB-MJHT",
          verification_uri: `${this.origin}/login/device`,
          expires_in: 900,
          interval: this.opts.interval ?? 0.1,
        },
      };
    }
    if (req.url === "/login/oauth/access_token") {
      const script = this.opts.tokens ?? [{ access_token: "gho_fresh" }];
      const answer = script[Math.min(this.polls, script.length - 1)];
      this.polls++;
      return { json: answer };
    }
    if (req.url === "/user") {
      const token = (req.headers.authorization ?? "").replace(/^token /, "");
      return token ? { json: { login: this.opts.login ?? "octo" } } : { status: 401, json: { message: "Bad credentials" } };
    }
    const issues = req.url.match(/^\/repos\/([^/]+)\/([^/]+)\/issues$/);
    if (issues && req.method === "POST") {
      const number = this.issueNumber++;
      return { status: 201, json: { number, html_url: `https://github.com/${issues[1]}/${issues[2]}/issues/${number}` } };
    }
    return { status: 404, json: { message: "Not Found" } };
  }

  stop(): Promise<void> {
    return this.http.stop();
  }
}

/** The environment that points every feedback and GitHub origin at fakes. */
export function serviceEnv(o: { signing?: FakeHttp; registry?: FakeHttp; github?: FakeGitHub }): Record<string, string> {
  const env: Record<string, string> = {};
  if (o.signing) env.UE_MCP_FEEDBACK_ENDPOINT = `${o.signing.origin}/sign`;
  if (o.registry) env.UE_MCP_REGISTRY = o.registry.origin;
  if (o.github) {
    env.UE_MCP_GITHUB_OAUTH = o.github.origin;
    env.UE_MCP_GITHUB_API = o.github.origin;
  }
  return env;
}

/**
 * A command on the sandbox's PATH, run by node: `<bin>/<name>.cmd` for Windows
 * and an executable `<bin>/<name>` script for everything else. Every call is
 * appended to `<root>/<name>-calls.jsonl` with its argv, cwd and stdin before
 * `body` runs. `body` is the inside of an async function with `args`, `stdin`,
 * `fs`, `path` and `cp` (node:child_process) in scope; its return value is the
 * exit code.
 */
export function installShim(sb: Sandbox, name: string, body: string): { calls: () => Array<{ args: string[]; cwd: string; stdin: string }> } {
  const log = sb.path(`${name}-calls.jsonl`);
  const js = path.join(sb.bin, `fake-${name}.mjs`);
  fs.writeFileSync(
    js,
    `import fs from "node:fs";
import path from "node:path";
import cp from "node:child_process";
const args = process.argv.slice(2);
const stdin = process.stdin.isTTY ? "" : await new Promise((resolve) => {
  let data = "";
  process.stdin.setEncoding("utf8");
  process.stdin.on("data", (d) => (data += d));
  process.stdin.on("end", () => resolve(data));
  process.stdin.on("error", () => resolve(data));
});
fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({ args, cwd: process.cwd(), stdin }) + "\\n");
const code = await (async () => {
${body}
})();
process.exit(code ?? 0);
`,
  );
  fs.writeFileSync(path.join(sb.bin, `${name}.cmd`), `@"${process.execPath}" "${js}" %*\r\n`);
  const sh = path.join(sb.bin, name);
  fs.writeFileSync(sh, `#!/bin/sh\nexec "${process.execPath}" "${js}" "$@"\n`);
  fs.chmodSync(sh, 0o755);
  return {
    calls: () =>
      fs.existsSync(log) ? fs.readFileSync(log, "utf8").split("\n").filter(Boolean).map((l) => JSON.parse(l)) : [],
  };
}
