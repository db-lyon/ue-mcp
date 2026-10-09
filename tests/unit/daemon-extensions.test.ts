/**
 * Extensions and UI bundles (spec 5.3, 5.5): an extension loaded from the
 * extensions directory gets routes, in-process actions and a data dir, and the
 * daemon serves a UI bundle only when it is signed by a key an extension
 * trusted, intact, and compatible with the daemon's API.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { startDaemon, type Daemon } from "../../src/daemon/daemon.js";
import { signBundle } from "../../src/daemon/ui-bundles.js";
import { DAEMON_API_VERSION } from "../../src/daemon/discovery.js";

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
  process.env.UE_MCP_STATE_DIR = path.join(sandbox, "state");
  process.env.UE_MCP_CONFIG_DIR = path.join(sandbox, "config");
  process.env.HOME = sandbox;
  process.env.USERPROFILE = sandbox;

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
  process.env = savedEnv;
  fs.rmSync(sandbox, { recursive: true, force: true });
});

describe("extensions", () => {
  it("loads the compatible extension and skips the other", async () => {
    const health = await (await fetch(`${base}/v1/health`, { headers: auth })).json();
    expect(health.extensions).toEqual([{ name: "sample", version: "0.1.0" }]);
  });

  it("serves its routes, with path params and a per-project data dir", async () => {
    const hello = await (await fetch(`${base}/v1/ext/sample/hello`, { headers: auth })).json();
    expect(hello.editors).toBe(1);
    expect(fs.existsSync(hello.dataDir)).toBe(true);
    expect(await (await fetch(`${base}/v1/ext/sample/items/42`, { headers: auth })).json()).toEqual({ id: "42" });
    expect((await fetch(`${base}/v1/ext/sample/nope`, { headers: auth })).status).toBe(404);
    expect((await fetch(`${base}/v1/ext/sample/hello`)).status).toBe(401);
  });

  it("runs actions in process and publishes namespaced events", async () => {
    const before = daemon.events.lastId;
    const res = await (await fetch(`${base}/v1/ext/sample/status`, { method: "POST", headers: auth })).json();
    expect(res.isError).toBe(false);
    expect(daemon.events.since(before).events.map((e) => e.type)).toContain("sample.checked");
  }, 60_000);
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

  it("stops serving an installed bundle that is altered on disk", async () => {
    const served = daemon.ui.active()!;
    fs.appendFileSync(path.join(served.dir, "index.html"), "<script>evil()</script>");
    daemon.ui.trustKey(publicPem);
    expect(daemon.ui.active()?.manifest.version).toBe("1.0.0");
  });
});
