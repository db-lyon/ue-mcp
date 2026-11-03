/**
 * Two signed extensions in one daemon: each serves its own UI bundle at
 * /ui/ext/<name>/ within its own version window, an extension with
 * `ui.root: false` never takes /ui/, and health lists both bundles.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { startDaemon, type Daemon } from "../../src/daemon/daemon.js";
import { signBundle } from "../../src/daemon/ui-bundles.js";
import { DAEMON_API_VERSION } from "../../src/daemon/discovery.js";
import { restoreEnv } from "../helpers/env.js";
import { userSandboxEnv } from "../helpers/user-sandbox.js";

let sandbox: string;
let daemon: Daemon;
let base: string;
let auth: Record<string, string>;
const savedEnv = { ...process.env };

const pair = () => {
  const k = crypto.generateKeyPairSync("ed25519");
  return { pub: k.publicKey.export({ type: "spki", format: "pem" }).toString(), priv: k.privateKey.export({ type: "pkcs8", format: "pem" }).toString() };
};

/** A built, signed bundle; returns its directory. */
function bundle(name: string, version: string, priv: string, module?: { entry: string; host: string }): string {
  const dir = path.join(sandbox, "build", `${name}-${version}`);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "index.html"), `<title>${name}@${version}</title>`);
  fs.writeFileSync(path.join(dir, "index.js"), `export const bundle = "${name}@${version}";`);
  signBundle(dir, { name, version, api: { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION }, ...(module ? { module } : {}) }, priv);
  return dir;
}

/** An extension that trusts its own key, installs its bundles, and reports ui.own and ui.active. */
function extension(name: string, version: string, pub: string, bundles: string[], ui?: Record<string, unknown>): void {
  const dir = path.join(sandbox, "extensions", name);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify({
    name: `${name}-ext`, version, type: "module", main: "index.js",
    ueMcpExtension: { name, api: { min: 1, max: 3 }, ...(ui ? { ui } : {}) },
  }));
  fs.writeFileSync(path.join(dir, "index.js"), `
    export function activate(api) {
      api.ui.trustKey(${JSON.stringify(pub)});
      for (const b of ${JSON.stringify(bundles)}) api.ui.install(b);
      api.route("GET", "/ui", () => ({ body: { own: api.ui.own?.() ?? null, active: api.ui.active(), features: api.features } }));
    }
  `);
}

beforeAll(async () => {
  sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ext-ui-"));
  for (const k of Object.keys(process.env)) if (k.startsWith("UE_MCP_")) delete process.env[k];
  process.env.UE_MCP_PORT = "1";
  Object.assign(process.env, userSandboxEnv(sandbox));
  process.env.HOME = sandbox;
  process.env.USERPROFILE = sandbox;
  process.env.UE_MCP_AUTH_DIR = path.join(sandbox, ".ue-mcp");

  const projectDir = path.join(sandbox, "Ext");
  fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
  const uproject = path.join(projectDir, "Ext.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));

  const studio = pair();
  const pro = pair();
  const module = { entry: "index.js", host: "studio" };
  // Each extension also installs a bundle newer than itself, which it must never be served.
  extension("studio", "0.1.19", studio.pub, [bundle("studio", "0.1.19", studio.priv), bundle("studio", "0.1.20", studio.priv)]);
  extension("pro", "1.0.0", pro.pub, [bundle("pro", "1.0.0", pro.priv, module), bundle("pro", "1.1.0", pro.priv, module)], { root: false });

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

const text = async (url: string) => {
  const res = await fetch(`${base}${url}`, { headers: auth });
  return { status: res.status, body: await res.text() };
};

describe("two extensions, each with its own UI bundle", () => {
  it("serves the root extension at /ui/ and the root:false one only at /ui/ext/<name>/", async () => {
    expect((await text("/ui/")).body).toBe("<title>studio@0.1.19</title>");
    expect((await text("/ui/ext/pro/index.js")).body).toBe('export const bundle = "pro@1.0.0";');
    expect((await text("/ui/ext/studio/index.js")).body).toBe('export const bundle = "studio@0.1.19";');
    expect((await text("/ui/ext/nobody/index.js")).status).toBe(404);
    expect((await fetch(`${base}/ui/ext/pro/index.js`)).status).toBe(401);
  });

  it("lists each extension's own bundle in health, with its signed module", async () => {
    const health = await (await fetch(`${base}/v1/health`, { headers: auth })).json();
    expect(health.ui).toBe("0.1.19");
    expect(health.features).toContain("ui.extensions");
    expect(health.uiBundles).toEqual(expect.arrayContaining([
      { name: "studio", version: "0.1.19", base: "/ui/ext/studio/" },
      { name: "pro", version: "1.0.0", base: "/ui/ext/pro/", module: { entry: "index.js", host: "studio" } },
    ]));
    expect(health.uiBundles).toHaveLength(2);
  });

  it("gives each extension its own bundle from ui.own, while ui.active stays the root", async () => {
    const studio = await (await fetch(`${base}/v1/ext/studio/ui`, { headers: auth })).json();
    const pro = await (await fetch(`${base}/v1/ext/pro/ui`, { headers: auth })).json();
    expect(`${studio.own.name}@${studio.own.version}`).toBe("studio@0.1.19");
    expect(`${pro.own.name}@${pro.own.version}`).toBe("pro@1.0.0");
    expect(pro.own.module).toEqual({ entry: "index.js", host: "studio" });
    expect(pro.active.name).toBe("studio");
    expect(pro.features).toEqual(expect.arrayContaining(["ui.extensions", "ui.scopedKeys"]));
  });
});
