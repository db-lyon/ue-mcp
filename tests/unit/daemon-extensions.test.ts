/**
 * Extensions and UI bundles (spec 5.3, 5.5): an extension loaded from the
 * extensions directory gets routes, in-process actions and a data dir, and the
 * daemon serves a UI bundle only when it is signed by a key an extension
 * trusted, intact, and compatible with the daemon's API.
 */
import { afterAll, beforeAll, describe, expect, it, vi } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as net from "node:net";
import * as os from "node:os";
import * as path from "node:path";
import { startDaemon, type Daemon } from "../../src/daemon/daemon.js";
import { signBundle, UiBundleStore } from "../../src/daemon/ui-bundles.js";
import { DAEMON_API_VERSION } from "../../src/daemon/discovery.js";
import { restoreEnv } from "../helpers/env.js";
import { userSandboxEnv } from "../helpers/user-sandbox.js";

let sandbox: string;
let daemon: Daemon;
let base: string;
let auth: Record<string, string>;
const savedEnv = { ...process.env };
const keys = crypto.generateKeyPairSync("ed25519");
const publicPem = keys.publicKey.export({ type: "spki", format: "pem" }).toString();
const privatePem = keys.privateKey.export({ type: "pkcs8", format: "pem" }).toString();

function bundle(name: string, version: string, api = { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION }, pem = privatePem): string {
  const dir = path.join(sandbox, "build", `${name}-${version}`);
  fs.mkdirSync(path.join(dir, "assets"), { recursive: true });
  fs.writeFileSync(path.join(dir, "index.html"), `<!doctype html><title>${version}</title>`);
  fs.writeFileSync(path.join(dir, "assets", "app.js"), `console.log(${JSON.stringify(version)})`);
  signBundle(dir, { name, version, api }, pem);
  return dir;
}

beforeAll(async () => {
  sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ext-"));
  for (const k of Object.keys(process.env)) if (k.startsWith("UE_MCP_")) delete process.env[k];
  process.env.UE_MCP_PORT = "1";
  Object.assign(process.env, userSandboxEnv(sandbox));
  process.env.HOME = sandbox;
  process.env.USERPROFILE = sandbox;
  // Named explicitly: HOME does not reach os.homedir() once an earlier file replaced process.env.
  process.env.UE_MCP_AUTH_DIR = path.join(sandbox, ".ue-mcp");
  fs.mkdirSync(path.join(sandbox, ".ue-mcp"), { recursive: true });
  fs.writeFileSync(path.join(sandbox, ".ue-mcp", "registry.json"), JSON.stringify({
    token: "uemcp_test", login: "someone", registry: "https://plugins.ue-mcp.com", authorized_at: "now",
  }));

  const projectDir = path.join(sandbox, "Ext");
  fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
  const uproject = path.join(projectDir, "Ext.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));

  // A sample extension, written as plain ESM the way a published one ships.
  const extDir = path.join(sandbox, "extensions", "sample");
  fs.mkdirSync(extDir, { recursive: true });
  fs.writeFileSync(path.join(extDir, "package.json"), JSON.stringify({
    name: "sample-ext", version: "0.1.0", type: "module", main: "index.js",
    ueMcpExtension: { name: "sample", api: { min: 1, max: 1 } },
  }));
  fs.writeFileSync(path.join(extDir, "index.js"), `
    export function activate(api) {
      api.ui.trustKey(${JSON.stringify(publicPem)});
      api.route("GET", "/hello", () => ({ body: { editors: api.editors().length, dataDir: api.dataDir } }));
      api.route("GET", "/items/:id", (req) => ({ body: { id: req.params.id } }));
      api.route("GET", "/account", async () => {
        const a = await api.registryAccount();
        return { body: a ? { login: a.login, hasToken: a.token.length > 0 } : null };
      });
      api.route("GET", "/features", () => ({ body: { features: api.features ?? null } }));
      api.route("POST", "/status-prompted", async () => {
        let asked = false;
        const r = await api.callAction("project", "get_status", {}, {
          dialogPrompt: async () => { asked = true; return { action: "cancel" }; },
          dialogWaitMs: 10,
        });
        return { body: { isError: r.isError, asked } };
      });
      api.route("POST", "/feedback-asked", async () => {
        let asked = null;
        const r = await api.callAction("feedback", "submit", {
          title: "pcg has no wait for a generation to finish",
          summary: "Polling get_component_details is the only way to know a generation finished, which costs a call per poll.",
          author: "bot",
        }, { elicit: async (form) => { asked = form.message; return { action: "decline" }; } });
        return { body: { isError: r.isError, asked, content: r.content } };
      });
      api.route("POST", "/modes", (req) => {
        if (req.body?.feedback !== undefined) api.modes.setFeedback(req.body.feedback ?? undefined);
        if (req.body?.dialog !== undefined) api.modes.setDialog(req.body.dialog ?? undefined);
        if (req.body?.playtest !== undefined) api.modes.setPlaytest(req.body.playtest ?? undefined);
        return { body: api.modes.get() };
      });
      api.route("POST", "/status", async () => {
        const r = await api.callAction("project", "get_status");
        api.events.publish("checked", { isError: r.isError });
        return { body: { isError: r.isError } };
      });
    }
  `);
  // One that targets an extension API this daemon does not have.
  const future = path.join(sandbox, "extensions", "future");
  fs.mkdirSync(future, { recursive: true });
  fs.writeFileSync(path.join(future, "package.json"), JSON.stringify({
    name: "future-ext", version: "9.0.0", type: "module", main: "index.js",
    ueMcpExtension: { name: "future", api: { min: 99, max: 99 } },
  }));
  fs.writeFileSync(path.join(future, "index.js"), "export function activate() {}");

  // One that needs a ue-mcp newer than this one.
  const ahead = path.join(sandbox, "extensions", "ahead");
  fs.mkdirSync(ahead, { recursive: true });
  fs.writeFileSync(path.join(ahead, "package.json"), JSON.stringify({
    name: "ahead-ext", version: "2.0.0", type: "module", main: "index.js",
    ueMcpExtension: { name: "ahead", api: { min: 1, max: 99 }, ueMcp: "999.0.0" },
  }));
  fs.writeFileSync(path.join(ahead, "index.js"), "export function activate() {}");

  // Two that talk through a service, and one that provides a service and then fails to activate.
  // Load order follows the directory listing, so the consumer may activate before or after the provider.
  const plain = (name: string, body: string) => {
    const dir = path.join(sandbox, "extensions", name);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify({
      name: `${name}-ext`, version: "1.0.0", type: "module", main: "index.js",
      ueMcpExtension: { name, api: { min: 1, max: 3 } },
    }));
    fs.writeFileSync(path.join(dir, "index.js"), `export function activate(api) {${body}}`);
  };
  plain("consumer", `
    const seen = [];
    let greeter;
    api.services.watch("greeter", (impl) => { greeter = impl; seen.push(impl ? "provided" : "revoked"); });
    api.route("GET", "/greet", () => ({ body: {
      greeting: greeter ? greeter.greet("consumer") : null,
      viaGet: api.services.get("greeter")?.greet("get") ?? null,
      unknown: api.services.get("nothing") ?? null,
      broken: api.services.get("broken.svc") ?? null,
      seen,
    } }));
  `);
  plain("provider", `api.services.provide("greeter", { greet: (who) => "hello " + who });`);
  plain("broken", `api.services.provide("broken.svc", {}); throw new Error("activation fails after providing");`);

  daemon = await startDaemon({
    projects: [uproject],
    publish: false,
    extensionsDir: path.join(sandbox, "extensions"),
    uiDir: path.join(sandbox, "ui"),
  });
  base = `http://127.0.0.1:${daemon.port}`;
  auth = { authorization: `Bearer ${daemon.token}` };
}, 180_000);

afterAll(async () => {
  await daemon?.close();
  restoreEnv(savedEnv);
  fs.rmSync(sandbox, { recursive: true, force: true });
});

describe("extensions", () => {
  it("loads the compatible extension and skips the other", async () => {
    const health = await (await fetch(`${base}/v1/health`, { headers: auth })).json();
    expect(health.extensions).toEqual(expect.arrayContaining([{ name: "sample", version: "0.1.0" }, { name: "consumer", version: "1.0.0" }, { name: "provider", version: "1.0.0" }]));
    expect(health.extensions).toHaveLength(3);
  });

  it("refuses an extension that needs a newer ue-mcp, and says which in health", async () => {
    const health = await (await fetch(`${base}/v1/health`, { headers: auth })).json();
    expect(health.refusedExtensions).toEqual(expect.arrayContaining([
      expect.objectContaining({ name: "ahead", version: "2.0.0", needsUeMcp: "999.0.0", reason: expect.stringContaining("needs ue-mcp 999.0.0 or later") }),
    ]));
    expect(health.extensions.map((x: { name: string }) => x.name)).not.toContain("ahead");
  });

  it("serves its routes, with path params and a per-project data dir", async () => {
    const hello = await (await fetch(`${base}/v1/ext/sample/hello`, { headers: auth })).json();
    expect(hello.editors).toBe(1);
    expect(fs.existsSync(hello.dataDir)).toBe(true);
    expect(await (await fetch(`${base}/v1/ext/sample/items/42`, { headers: auth })).json()).toEqual({ id: "42" });
    expect((await fetch(`${base}/v1/ext/sample/nope`, { headers: auth })).status).toBe(404);
    expect((await fetch(`${base}/v1/ext/sample/hello`)).status).toBe(401);
  });

  it("reads the ue-mcp account this machine signed in with", async () => {
    const res = await (await fetch(`${base}/v1/ext/sample/account`, { headers: auth })).json();
    expect(res).toEqual({ login: "someone", hasToken: true });
  });

  it("runs actions in process and publishes namespaced events", async () => {
    const before = daemon.events.lastId;
    const res = await (await fetch(`${base}/v1/ext/sample/status`, { method: "POST", headers: auth })).json();
    expect(res.isError).toBe(false);
    expect(daemon.events.since(before).events.map((e) => e.type)).toContain("sample.checked");
  }, 60_000);
});

describe("modes, for an extension", () => {
  const modes = async (body: unknown) =>
    (await fetch(`${base}/v1/ext/sample/modes`, { method: "POST", headers: { ...auth, "content-type": "application/json" }, body: JSON.stringify(body) })).json();

  it("sets this project's feedback, dialog and playtest modes and reads them back, then clears them", async () => {
    expect(await modes({ feedback: "defer", dialog: "auto", playtest: "manual" })).toEqual({
      feedback: { mode: "defer", project: true },
      dialog: { mode: "auto", project: true },
      playtest: { mode: "manual", project: true },
    });
    expect(await modes({ feedback: null, dialog: null, playtest: null })).toMatchObject({
      feedback: { project: false },
      dialog: { project: false },
      playtest: { mode: null, project: false },
    });
  });
});

describe("dialogs, for an extension", () => {
  it("advertises the dialog prompt and the dialog events", async () => {
    const res = await (await fetch(`${base}/v1/ext/sample/features`, { headers: auth })).json();
    expect(res.features).toEqual(expect.arrayContaining(["callAction.dialogPrompt", "callAction.elicit", "events.dialog"]));
  });

  it("puts any other form a call raises to the extension, such as feedback approval", async () => {
    const saved = process.env.UE_MCP_FEEDBACK_MODE;
    process.env.UE_MCP_FEEDBACK_MODE = "interactive";
    try {
      const res = await (await fetch(`${base}/v1/ext/sample/feedback-asked`, { method: "POST", headers: auth })).json();
      expect(typeof res.asked, JSON.stringify(res)).toBe("string");
      expect(res.asked).toMatch(/pcg has no wait/);
    } finally {
      if (saved === undefined) delete process.env.UE_MCP_FEEDBACK_MODE;
      else process.env.UE_MCP_FEEDBACK_MODE = saved;
    }
  }, 60_000);

  it("takes a dialog prompt on callAction and asks it nothing while no dialog is up", async () => {
    const res = await (await fetch(`${base}/v1/ext/sample/status-prompted`, { method: "POST", headers: auth })).json();
    expect(res).toEqual({ isError: false, asked: false });
  }, 60_000);

  it("puts a guard's dialog on the event stream", async () => {
    const { existingGuard } = await import("../../src/editor/dialog-guard.js");
    const before = daemon.events.lastId;
    const session = daemon.runtime.sessions.list()[0];
    const guard = existingGuard(session)!;
    guard.note({ title: "Save Content", message: "Save these?", buttons: ["Save", "Cancel"], choices: [] });
    guard.clear();
    const seen = daemon.events.since(before).events;
    expect(seen.map((e) => e.type)).toEqual(["dialog.opened", "dialog.closed"]);
    expect(seen[0].data).toMatchObject({ title: "Save Content", buttons: ["Save", "Cancel"] });
  });
});

describe("updates", () => {
  it("refuses to apply an update to a project with no bridge, and says why", async () => {
    const before = daemon.events.lastId;
    const res = await fetch(`${base}/v1/update/apply`, { method: "POST", headers: auth });
    expect(res.status).toBe(500);
    expect((await res.json()).error).toMatch(/not installed/);
    expect(daemon.events.since(before).events.map((e) => e.type)).toContain("update.failed");
  });
});

describe("install state", () => {
  it("reports the project the way ue-mcp status --json does", async () => {
    const res = await (await fetch(`${base}/v1/install`, { headers: auth })).json();
    expect(res.project.name).toBe("Ext");
    expect(res.plugin.deployed).toBe(false);
    expect(res.problems.map((p: { code: string }) => p.code)).toContain("bridge_not_deployed");
  }, 60_000);
});

describe("UI bundles", () => {
  it("says no client is installed until one is", async () => {
    expect((await fetch(`${base}/ui/`, { headers: auth })).status).toBe(503);
  });

  it("refuses a bundle signed by a key nobody trusted", () => {
    const other = crypto.generateKeyPairSync("ed25519").privateKey.export({ type: "pkcs8", format: "pem" }).toString();
    expect(() => daemon.ui.install(bundle("client", "0.9.0", undefined, other))).toThrow(/signature/);
  });

  it("refuses a bundle named for a loaded extension unless that extension trusted its key", async () => {
    const other = crypto.generateKeyPairSync("ed25519");
    daemon.ui.trustKey(other.publicKey.export({ type: "spki", format: "pem" }).toString(), "future");
    const otherPem = other.privateKey.export({ type: "pkcs8", format: "pem" }).toString();
    expect(() => daemon.ui.install(bundle("sample", "0.1.0", undefined, otherPem))).toThrow(/signature does not verify/);
    const res = await (await fetch(`${base}/v1/ext/sample/features`, { headers: auth })).json();
    expect(res.features).toContain("ui.scopedKeys");
    expect((await (await fetch(`${base}/v1/health`, { headers: auth })).json()).features).toContain("ui.scopedKeys");
  });

  it("refuses a bundle with a file changed after signing", () => {
    const dir = bundle("client", "0.9.1");
    fs.writeFileSync(path.join(dir, "assets", "app.js"), "alert('changed')");
    expect(() => daemon.ui.install(dir)).toThrow(/hash/);
  });

  it("serves the newest compatible bundle, behind a cookie set from the token", async () => {
    daemon.ui.install(bundle("client", "1.0.0"));
    daemon.ui.install(bundle("client", "1.1.0"));
    daemon.ui.install(bundle("client", "2.0.0", { min: DAEMON_API_VERSION + 1, max: DAEMON_API_VERSION + 1 }));

    const login = await fetch(`${base}/ui/?token=${daemon.token}`, { redirect: "manual" });
    expect(login.status).toBe(302);
    const cookie = login.headers.get("set-cookie")!.split(";")[0];
    expect(login.headers.get("set-cookie")).toMatch(/HttpOnly; SameSite=Strict/);

    const page = await fetch(`${base}/ui/`, { headers: { cookie } });
    expect(page.status).toBe(200);
    expect(await page.text()).toContain("<title>1.1.0</title>");
    expect((await fetch(`${base}/ui/assets/app.js`, { headers: { cookie } })).headers.get("content-type")).toMatch(/javascript/);
    expect((await fetch(`${base}/ui/manifest.sig`, { headers: { cookie } })).status).toBe(404);
    expect((await fetch(`${base}/ui/`)).status).toBe(401);
    // The cookie also opens the daemon API for the page.
    expect((await fetch(`${base}/v1/health`, { headers: { cookie } })).status).toBe(200);
  });

  it("ranks a release above its own prereleases", () => {
    const store = new UiBundleStore(DAEMON_API_VERSION, path.join(sandbox, "ui-semver"));
    store.trustKey(publicPem);
    for (const v of ["1.2.0-beta.3", "1.2.0", "1.2.0-beta.10", "1.1.9"]) store.install(bundle("semver", v));
    expect(store.active()?.manifest.version).toBe("1.2.0");
  });

  it("names its cookie by port, so another project's daemon on the same host cannot overwrite it", async () => {
    const login = await fetch(`${base}/ui/?token=${daemon.token}`, { redirect: "manual" });
    const cookie = login.headers.get("set-cookie")!.split(";")[0];
    expect(cookie).toBe(`ue_mcp_token_${new URL(base).port}=${daemon.token}`);
    // The browser also holds another daemon's login, and a cookie from before the rename.
    const jar = `ue_mcp_token=${"a".repeat(64)}; ue_mcp_token_1=${"b".repeat(64)}; ${cookie}`;
    expect((await fetch(`${base}/v1/health`, { headers: { cookie: jar } })).status).toBe(200);
    expect((await fetch(`${base}/v1/health`, { headers: { cookie: `ue_mcp_token_1=${"b".repeat(64)}` } })).status).toBe(401);
  });

  it("clears the logins of daemons no longer listening, so restarts cannot grow the jar past the header limit", async () => {
    // A port held open stands in for another project's daemon that is still running.
    const live = net.createServer().listen(0, "127.0.0.1");
    await new Promise<void>((r) => live.once("listening", () => r()));
    const livePort = (live.address() as net.AddressInfo).port;
    // A port opened and closed again stands in for a daemon that has exited.
    const gone = net.createServer().listen(0, "127.0.0.1");
    await new Promise<void>((r) => gone.once("listening", () => r()));
    const deadPort = (gone.address() as net.AddressInfo).port;
    await new Promise<void>((r) => gone.close(() => r()));
    try {
      const own = new URL(base).port;
      // Past Node's default 16 KiB header limit: the login must still be reached.
      const filler = Array.from({ length: 250 }, (_, i) => `other_${i}=${"c".repeat(64)}`).join("; ");
      const jar = [
        `ue_mcp_token=${"a".repeat(64)}`,
        `ue_mcp_token_${deadPort}=${"b".repeat(64)}`,
        `ue_mcp_token_${livePort}=${"d".repeat(64)}`,
        `ue_mcp_token_${own}=${"e".repeat(64)}`,
        filler,
      ].join("; ");
      expect(jar.length).toBeGreaterThan(16 * 1024);
      const login = await fetch(`${base}/ui/?token=${daemon.token}`, { redirect: "manual", headers: { cookie: jar } });
      expect(login.status).toBe(302);
      const set = login.headers.getSetCookie();
      expect(set[0]).toMatch(new RegExp(`^ue_mcp_token_${own}=${daemon.token};`));
      const cleared = set.slice(1).map((c) => c.split("=")[0]).sort();
      expect(cleared).toEqual(["ue_mcp_token", `ue_mcp_token_${deadPort}`].sort());
      for (const c of set.slice(1)) expect(c).toMatch(/Max-Age=0/);
      // The jar no longer blocks the API either.
      expect((await fetch(`${base}/v1/health`, { headers: { cookie: `${jar}; ${set[0].split(";")[0]}` } })).status).toBe(200);
    } finally {
      await new Promise<void>((r) => live.close(() => r()));
    }
  });

  it("stops serving an installed bundle that is altered on disk", async () => {
    const served = daemon.ui.active()!;
    fs.appendFileSync(path.join(served.dir, "index.html"), "<script>evil()</script>");
    daemon.ui.trustKey(publicPem);
    expect(daemon.ui.active()?.manifest.version).toBe("1.0.0");
  });
});

describe("a held event loop", () => {
  it("is reported with how long nothing was answered", async () => {
    const lines: string[] = [];
    const spy = vi.spyOn(console, "error").mockImplementation((line: unknown) => void lines.push(String(line)));
    try {
      const until = Date.now() + 5000;
      while (Date.now() < until) { /* hold the loop, as a synchronous call would */ }
      await new Promise((r) => setTimeout(r, 1500));
      expect(lines.some((l) => /warn daemon: answered nothing for \d+\.\ds: something held the event loop/.test(l))).toBe(true);
    } finally {
      spy.mockRestore();
    }
  }, 15_000);
});

describe("test script events", () => {
  it("advertises the script events and republishes the bus as script.<type>", async () => {
    const res = await (await fetch(`${base}/v1/ext/sample/features`, { headers: auth })).json();
    expect(res.features).toEqual(expect.arrayContaining(["events.script"]));

    const { emitScriptEvent } = await import("../../src/pie/script-events.js");
    const before = daemon.events.lastId;
    const common = { runId: "run-x", script: "/Game/PIEStudio/Scripts/Door", at: "2026-10-23T10:00:00.000Z" };
    emitScriptEvent({ ...common, type: "step_passed", atMs: 1200, seq: 3, step: { index: 0, instruction: "Open the door", judge: "auto", status: "passed" } });
    emitScriptEvent({ ...common, type: "verdict_requested", atMs: 1500, seq: 4, awaiting: [{ index: 1, instruction: "The door opens smoothly" }] });
    emitScriptEvent({ ...common, type: "run_finished", atMs: 9000, seq: 7, state: "finished", summary: { total: 2, passed: 2, failed: 0, skipped: 0, pending: 0 } });
    const seen = daemon.events.since(before).events;
    expect(seen.map((e) => e.type)).toEqual(["script.step_passed", "script.verdict_requested", "script.run_finished"]);
    expect(seen[0].data).toEqual({ ...common, atMs: 1200, seq: 3, step: { index: 0, instruction: "Open the door", judge: "auto", status: "passed" } });
    expect(seen[1].data).toMatchObject({ runId: "run-x", awaiting: [{ index: 1 }] });
    expect(seen[2].data).toMatchObject({ seq: 7, summary: { passed: 2 } });
    expect(seen.every((e) => !("type" in e.data))).toBe(true);
  });
});

describe("extension services", () => {
  it("lets one extension use what another provides, whichever activates first", async () => {
    const res = await (await fetch(`${base}/v1/ext/consumer/greet`, { headers: auth })).json();
    expect(res).toEqual({ greeting: "hello consumer", viaGet: "hello get", unknown: null, broken: null, seen: ["provided"] });
    const features = await (await fetch(`${base}/v1/ext/sample/features`, { headers: auth })).json();
    expect(features.features).toContain("extension.services");
  });
});

// Last: it stops the daemon.
describe("extension services on shutdown", () => {
  it("revokes each extension's services after disposing it", async () => {
    const { ExtensionServices } = await import("../../src/daemon/extension-services.js");
    const revoked = vi.spyOn(ExtensionServices.prototype, "revokeOwner");
    try {
      await daemon.close();
      expect(revoked.mock.calls.map((c) => c[0])).toEqual(expect.arrayContaining(["sample", "consumer", "provider"]));
    } finally {
      revoked.mockRestore();
    }
  });
});
