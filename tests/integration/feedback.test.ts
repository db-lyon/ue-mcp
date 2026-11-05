/**
 * Feedback end to end: the mode set with the CLI decides what the spawned
 * server does with feedback(submit), deferred reports are listed, shown,
 * discarded and approved by the CLI, and every post lands on a local fake of
 * the signing service or the GitHub API with the body privacy-scrubbed.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { DEAD_ORIGIN, json, McpServer, plain, runCli, Sandbox, type CliResult } from "./harness.js";
import { FakeGitHub, fakeRegistry, fakeSigning, serviceEnv } from "./feedback-fakes.js";
import { helloTask, placeInstalled, pluginsYml, writePluginPackage } from "./plugin-fixtures.js";
import type { FakeHttp } from "./harness.js";

let sb: Sandbox;
let uproject: string;
let projectDir: string;
let signing: Awaited<ReturnType<typeof fakeSigning>>;
let registry: FakeHttp;
let github: FakeGitHub;
let server: McpServer;
let env: Record<string, string>;

const FIXTURE = "ue-mcp-fixture";
const PLUGIN_TRACKER = "acme/fixture-tracker";
/** A secret-shaped token a careless summary might carry. */
const SECRET = `ghp_${"A1b2C3d4E5".repeat(4).slice(0, 36)}`;

const authDir = () => path.join(sb.home, ".ue-mcp");
const authJson = () => path.join(authDir(), "auth.json");
const pendingDir = () => path.join(sb.home, ".ue-mcp", "pending-feedback");
const statePath = () => path.join(sb.home, ".ue-mcp", "state.json");
const state = () => JSON.parse(fs.readFileSync(statePath(), "utf8")) as {
  preferences?: { feedback?: { mode?: string } };
  projects?: Record<string, { feedback?: { mode?: string } }>;
};
const pendingIds = () => (fs.existsSync(pendingDir()) ? fs.readdirSync(pendingDir()).filter((f) => f.endsWith(".json")).map((f) => f.slice(0, -5)).sort() : []);
const entry = (id: string) => JSON.parse(fs.readFileSync(path.join(pendingDir(), `${id}.json`), "utf8")) as Record<string, unknown>;

function cli(args: string[], extra: Record<string, string | undefined> = {}): Promise<CliResult> {
  return runCli(sb, ["feedback", ...args], { env: { ...env, ...extra } }).then((r) => ({ ...r, stdout: plain(r.stdout), stderr: plain(r.stderr) }));
}

/** A report whose summary carries the project root, the home directory and a token. */
function leakyReport(title: string, extra: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    action: "submit",
    title,
    summary: `Ran it against ${path.join(projectDir, "Content", "Maps", "Arena.umap")} with the log at ${path.join(sb.home, "logs", "run.txt")}; the call also echoed ${SECRET}.`,
    ...extra,
  };
}

function assertScrubbed(text: string): void {
  expect(text).not.toContain(sb.home);
  expect(text).not.toContain(projectDir);
  expect(text).not.toContain(SECRET);
}

beforeAll(async () => {
  sb = new Sandbox();
  uproject = sb.makeProject("Game", { config: pluginsYml([FIXTURE]) });
  projectDir = path.dirname(uproject);
  placeInstalled(projectDir, writePluginPackage(sb.path("src", "fixture"), {
    name: FIXTURE,
    version: "1.0.0",
    manifest: `actionPrefix: fx\nprovides:\n  fixturecat:\n    description: A category the fixture plugin owns\n    actions:\n      greet:\n        task: fx_hello\n        description: Greets\ntasks:\n  fx_hello:\n    class_path: tasks/hello\n`,
    files: { "tasks/hello.js": helloTask(FIXTURE) },
  }));
  [signing, registry, github] = await Promise.all([
    fakeSigning(),
    fakeRegistry([{ slug: "fixture", name: "Fixture", packageName: FIXTURE, repoUrl: `https://github.com/${PLUGIN_TRACKER}`, status: "published" }]),
    FakeGitHub.start({ login: "octo" }),
  ]);
  env = serviceEnv({ signing, registry, github });
  server = await McpServer.start(sb, { projects: [uproject], strategy: "full", env });
}, 60_000);

afterAll(async () => {
  await server?.close();
  await Promise.all([signing?.stop(), registry?.stop(), github?.stop()]);
  sb?.cleanup();
});

describe("feedback mode", () => {
  it("prints the default, stores a user-wide and a per-editor mode, and lets the variable win", async () => {
    covers("cli.feedback.mode");
    const initial = await cli(["mode"]);
    expect(initial.code).toBe(0);
    expect(initial.stdout).toMatch(/effective:\s+interactive/);
    expect(initial.stdout).toContain("(not set; default interactive)");

    expect((await cli(["mode", "defer"])).code).toBe(0);
    expect(state().preferences?.feedback?.mode).toBe("defer");

    expect((await cli(["mode", "auto-approve", "--editor", uproject])).code).toBe(0);
    expect(Object.values(state().projects ?? {}).map((p) => p.feedback?.mode)).toEqual(["auto-approve"]);
    expect(state().preferences?.feedback?.mode).toBe("defer");

    const scoped = await cli(["mode", "--editor", uproject]);
    expect(scoped.stdout).toMatch(/effective:\s+auto-approve/);
    expect(scoped.stdout).toContain("editor 'Game': auto-approve");

    const overridden = await cli(["mode"], { UE_MCP_FEEDBACK_MODE: "interactive" });
    expect(overridden.stdout).toMatch(/effective:\s+interactive/);
    expect(overridden.stdout).toContain("UE_MCP_FEEDBACK_MODE env override: interactive");

    const before = fs.readFileSync(statePath(), "utf8");
    const bad = await cli(["mode", "sometimes"]);
    expect(bad.code).toBe(1);
    expect(bad.stdout + bad.stderr).toContain('Unknown mode "sometimes"');
    expect(fs.readFileSync(statePath(), "utf8")).toBe(before);

    expect((await cli(["mode", "clear", "--editor", uproject])).code).toBe(0);
    expect(state().projects).toBeUndefined();
    expect(state().preferences?.feedback?.mode).toBe("defer");
  });
});

describe("defer mode", () => {
  const ids: Record<string, string> = {};

  it("queues a scrubbed report on disk and posts nothing", async () => {
    covers("feedback.submit.defer");
    const r = await server.call("feedback", leakyReport("level get_outliner drops hidden actors", { author: "bot" }));
    expect(r.isError, r.text).toBe(false);
    const out = json<{ deferred: boolean; id: string; mode: string; target_repo: string }>(r);
    expect(out).toMatchObject({ deferred: true, mode: "defer", target_repo: "db-lyon/ue-mcp" });
    ids.bot = out.id;
    const saved = entry(out.id);
    expect(saved).toMatchObject({ id: out.id, author: "bot", project: "Game", repo: "db-lyon/ue-mcp", title: "level get_outliner drops hidden actors" });
    assertScrubbed(JSON.stringify(saved));
    expect(saved.body).toContain("REDACTED_PROJECT_ROOT");
    expect(saved.body).toContain("REDACTED_HOME");
    expect(signing.requests).toHaveLength(0);

    // A user-authored report needs a cached sign-in to be accepted at all.
    fs.mkdirSync(authDir(), { recursive: true });
    fs.writeFileSync(authJson(), JSON.stringify({ token: "gho_stale", login: "octo", authorized_at: new Date().toISOString() }));
    const asUser = json<{ id: string }>(await server.call("feedback", leakyReport("asset rename_asset leaves redirectors behind")));
    ids.user = asUser.id;
    fs.rmSync(authJson());
    const extra = json<{ id: string }>(await server.call("feedback", leakyReport("material set_parameter ignores vector defaults", { author: "bot" })));
    ids.extra = extra.id;
    expect(entry(ids.user).author).toBe("user");
    expect(pendingIds()).toEqual([ids.bot, ids.user, ids.extra].sort());
    expect(signing.requests).toHaveLength(0);
    expect(github.hits("/repos/")).toHaveLength(0);
  });

  it("lists every deferred report with its author and tracker", async () => {
    covers("cli.feedback.list");
    const r = await cli(["list"]);
    expect(r.code).toBe(0);
    expect(r.stdout).toContain("3 pending feedback submission(s)");
    for (const id of Object.values(ids)) expect(r.stdout).toContain(id);
    const botLine = r.stdout.split("\n").find((l) => l.includes(ids.bot))!;
    expect(botLine).toMatch(/Game\s+db-lyon\/ue-mcp\s+bot\s+level get_outliner drops hidden actors/);
    expect(r.stdout.split("\n").find((l) => l.includes(ids.user))).toMatch(/\buser\b/);

    const other = await cli(["list", "--editor", sb.makeProject("Other")]);
    expect(other.stdout).toContain("No pending feedback submissions from 'Other'. 3 pending from other editors.");
  });

  it("shows one report's metadata and scrubbed body, and refuses an unknown id", async () => {
    covers("cli.feedback.show");
    const r = await cli(["show", ids.bot]);
    expect(r.code).toBe(0);
    expect(r.stdout).toMatch(/author\s+bot/);
    expect(r.stdout).toMatch(/tracker\s+db-lyon\/ue-mcp/);
    expect(r.stdout).toContain("REDACTED_HOME");
    expect(r.stdout).toContain(entry(ids.bot).body as string);
    assertScrubbed(r.stdout);
    const missing = await cli(["show", "20000101T000000000-abcdef"]);
    expect(missing.code).toBe(1);
    expect(missing.stdout + missing.stderr).toContain('No pending feedback with id "20000101T000000000-abcdef"');
  });

  it("discards one report without posting it", async () => {
    covers("cli.feedback.discard");
    const r = await cli(["discard", ids.extra]);
    expect(r.code).toBe(0);
    expect(r.stdout).toContain(`Discarded ${ids.extra}`);
    expect(pendingIds()).toEqual([ids.bot, ids.user].sort());
    expect((await cli(["discard", ids.extra])).code).toBe(1);
    expect(signing.requests).toHaveLength(0);
  });

  it("refuses to walk a non-empty queue without a terminal, and touches nothing", async () => {
    covers("cli.feedback.review");
    const r = await cli(["review"]);
    expect(r.code).toBe(1);
    expect(r.stdout + r.stderr).toContain("requires an interactive terminal");
    expect(pendingIds()).toEqual([ids.bot, ids.user].sort());
  });

  it("approves a bot report through the signing service, keeping it when the service is down", async () => {
    covers("cli.feedback.approve");
    const down = await cli(["approve", ids.bot], { UE_MCP_FEEDBACK_ENDPOINT: `${DEAD_ORIGIN}/sign` });
    expect(down.code).toBe(1);
    expect(down.stdout).toContain("Open it manually:");
    expect(pendingIds()).toContain(ids.bot);

    const stored = entry(ids.bot);
    const r = await cli(["approve", ids.bot]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(r.stdout).toContain("Posted as db-lyon/ue-mcp#101 (https://github.com/db-lyon/ue-mcp/issues/101)");
    expect(signing.reports()).toEqual([{ title: stored.title, body: stored.body, labels: stored.labels, repo: "db-lyon/ue-mcp" }]);
    expect(signing.requests[0].url).toBe("/sign");
    expect(pendingIds()).toEqual([ids.user]);
  });

  it("approving a user report with no sign-in runs the device flow across calls, then posts as the user", async () => {
    covers("auth.device-flow");
    github.script({ tokens: [{ error: "authorization_pending" }, { access_token: "gho_fresh" }] });
    const pendingFile = path.join(authDir(), "device-pending.json");

    const first = await cli(["approve", ids.user]);
    expect(first.code).toBe(1);
    expect(first.stdout).toContain("Enter code: WDJB-MJHT");
    expect(first.stdout).toContain(`${github.origin}/login/device`);
    expect(JSON.parse(fs.readFileSync(pendingFile, "utf8"))).toMatchObject({ device_code: "dev-code-1", user_code: "WDJB-MJHT" });
    expect(JSON.parse(github.hits("/login/device/code")[0].body)).toHaveProperty("client_id");

    // Not authorized yet: the same code is offered again, no second flow is started.
    const second = await cli(["approve", ids.user]);
    expect(second.code).toBe(1);
    expect(second.stdout).toContain("Enter code: WDJB-MJHT");
    expect(github.hits("/login/device/code")).toHaveLength(1);
    expect(fs.existsSync(authJson())).toBe(false);

    const stored = entry(ids.user);
    const third = await cli(["approve", ids.user]);
    expect(third.code, third.stdout + third.stderr).toBe(0);
    expect(third.stdout).toContain("Posted as db-lyon/ue-mcp#500");
    expect(JSON.parse(fs.readFileSync(authJson(), "utf8"))).toMatchObject({ token: "gho_fresh", login: "octo" });
    expect(fs.existsSync(pendingFile)).toBe(false);
    expect(github.hits("/user")[0].headers.authorization).toBe("token gho_fresh");
    const post = github.hits("/repos/db-lyon/ue-mcp/issues");
    expect(post).toHaveLength(1);
    expect(post[0].headers.authorization).toBe("token gho_fresh");
    expect(JSON.parse(post[0].body)).toEqual({ title: stored.title, body: stored.body, labels: stored.labels });
    expect(pendingIds()).toEqual([]);

    const empty = await cli(["review"]);
    expect(empty.code).toBe(0);
    expect(empty.stdout).toContain("No pending feedback submissions.");
  });
});

describe("auto-approve mode", () => {
  beforeAll(async () => {
    // Per-editor, so it also proves the server reads the scope the CLI wrote.
    expect((await cli(["mode", "auto-approve", "--editor", uproject])).code).toBe(0);
  });

  it("files a bot report through the signing service with the body scrubbed", async () => {
    covers("feedback.submit.bot");
    const before = signing.reports().length;
    const r = await server.call("feedback", leakyReport("blueprint compile swallows the error text", { author: "bot" }));
    expect(r.isError, r.text).toBe(false);
    const out = json<Record<string, unknown>>(r);
    expect(out).toMatchObject({ authored_as: "bot", mode: "auto-approve", target_repo: "db-lyon/ue-mcp", issue_number: 102 });
    const sent = signing.reports().slice(before);
    expect(sent).toHaveLength(1);
    expect(sent[0].title).toBe("blueprint compile swallows the error text");
    expect(sent[0].repo).toBe("db-lyon/ue-mcp");
    expect(sent[0].labels).toContain("agent-feedback");
    expect(sent[0].labels).toContain("blueprint");
    expect(sent[0].body).toContain("## Summary");
    expect(sent[0].body).toContain("REDACTED_PROJECT_ROOT");
    assertScrubbed(JSON.stringify(sent[0]));
    expect(pendingIds()).toEqual([]);
  });

  it("files a user report through the GitHub API with the cached token", async () => {
    covers("feedback.submit.user");
    const before = github.hits("/repos/").length;
    const r = await server.call("feedback", leakyReport("animation retarget loses root motion", { author: "user" }));
    expect(r.isError, r.text).toBe(false);
    expect(json(r)).toMatchObject({ authored_as: "user", authored_by: "octo", target_repo: "db-lyon/ue-mcp" });
    const posts = github.hits("/repos/").slice(before);
    expect(posts).toHaveLength(1);
    expect(posts[0].url).toBe("/repos/db-lyon/ue-mcp/issues");
    expect(posts[0].method).toBe("POST");
    expect(posts[0].headers.authorization).toBe("token gho_fresh");
    const body = JSON.parse(posts[0].body) as { title: string; body: string; labels: string[] };
    expect(body.title).toBe("animation retarget loses root motion");
    expect(body.labels).toContain("animation");
    assertScrubbed(body.body);
  });

  it("routes a report about a plugin's category to that plugin's tracker, and to core when routing is off", async () => {
    covers("feedback.routing");
    const report = {
      action: "submit",
      author: "bot",
      title: "fixturecat greet ignores the who parameter",
      summary: "Calling greet with who set still answers hello world, so the option never reaches the task.",
      idealTool: "fixturecat(greet)",
    };
    const before = signing.reports().length;
    const routed = json<Record<string, unknown>>(await server.call("feedback", report));
    expect(routed).toMatchObject({ target_repo: PLUGIN_TRACKER, authored_as: "bot" });
    expect(String(routed.routing)).toContain(`${PLUGIN_TRACKER} - matched Fixture (certain)`);
    const [sent] = signing.reports().slice(before);
    expect(sent.repo).toBe(PLUGIN_TRACKER);
    // Only labels that mean something on any tracker travel off core.
    expect(sent.labels).toContain("agent-feedback");
    expect(sent.labels.filter((l) => !["agent-feedback", "bug", "enhancement"].includes(l))).toEqual([]);
    expect(sent.body).toContain("## Routing");
    expect(registry.requests.some((r) => r.url.startsWith("/api/plugins"))).toBe(true);

    const registryCalls = registry.requests.length;
    const off = await McpServer.start(sb, { projects: [uproject], strategy: "full", env: { ...env, UE_MCP_FEEDBACK_ROUTING: "off" } });
    try {
      const pinned = json<Record<string, unknown>>(await off.call("feedback", report));
      expect(pinned).toMatchObject({ target_repo: "db-lyon/ue-mcp" });
      expect(String(pinned.routing)).toContain("routing disabled");
    } finally {
      await off.close();
    }
    const last = signing.reports().at(-1)!;
    expect(last.repo).toBe("db-lyon/ue-mcp");
    expect(last.body).not.toContain("## Routing");
    expect(registry.requests.length).toBe(registryCalls);
  });
});
