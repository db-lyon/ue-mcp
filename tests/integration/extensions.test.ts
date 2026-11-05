/**
 * Daemon extensions end to end: `ue-mcp extension install|list|remove`
 * against the sandbox's extensions directory, a spawned daemon loading what
 * is installed and serving its routes, and the signed UI bundles it serves.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeHttp, plain, runCli, Sandbox, stopSandboxDaemons } from "./harness.js";
import { projectKey } from "../../src/daemon/discovery.js";
import {
  base,
  bearer,
  call,
  daemonEnv,
  pidAlive,
  readDiscovery,
  readSse,
  signedBundle,
  startDaemonCli,
  waitGone,
  writeExtension,
  zipExtension,
  type Discovery,
} from "./daemon-fakes.js";

let sb: Sandbox;
let work: string;
let extDir: string;
beforeAll(() => {
  sb = new Sandbox();
  work = sb.path("work");
  fs.mkdirSync(work, { recursive: true });
  extDir = path.join(sb.home, ".ue-mcp", "extensions");
});
afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

const listJson = async () => {
  const r = await runCli(sb, ["extension", "list", "--json"]);
  expect(r.code, r.stderr).toBe(0);
  return JSON.parse(r.stdout) as { ok: boolean; dir: string; extensions: Array<{ name: string; version: string; installed: string[] }> };
};

describe("ue-mcp extension", () => {
  it("lists nothing before anything is installed", async () => {
    covers("cli.extension.list");
    expect(await listJson()).toEqual({ ok: true, dir: extDir, extensions: [] });
    const text = await runCli(sb, ["extension", "list"]);
    expect(text.code).toBe(0);
    expect(plain(text.stdout + text.stderr)).toContain(`no extensions in ${extDir}`);
  });

  it("installs a verified zip into its version's folder, and an upgrade no project records replaces it", async () => {
    covers("cli.extension.install");
    const v1 = zipExtension(work, { name: "probe", version: "0.1.0" });
    const r = await runCli(sb, ["extension", "install", v1.zip, "--sha256", v1.sha256, "--json"]);
    expect(r.code, r.stderr).toBe(0);
    expect(JSON.parse(r.stdout)).toMatchObject({ ok: true, name: "probe", version: "0.1.0", sha256: v1.sha256, daemonRestarted: false });
    expect(JSON.parse(fs.readFileSync(path.join(extDir, "probe@0.1.0", "package.json"), "utf8")).version).toBe("0.1.0");

    const v2 = zipExtension(work, { name: "probe", version: "0.2.0" });
    const up = await runCli(sb, ["extension", "install", v2.zip]);
    expect(up.code, up.stderr).toBe(0);
    expect(plain(up.stdout + up.stderr)).toContain("installed probe 0.2.0");
    expect(fs.readdirSync(extDir)).toEqual(["probe@0.2.0"]);
    expect((await listJson()).extensions).toEqual([{ name: "probe", version: "0.2.0", installed: ["0.2.0"] }]);
  });

  it("refuses an archive whose digest does not match, in either flag spelling, and keeps what was installed", async () => {
    covers("cli.extension.install");
    const v3 = zipExtension(work, { name: "probe", version: "0.3.0" });
    const wrong = "0".repeat(64);
    for (const args of [["--sha256", wrong], [`--sha256=${wrong}`]]) {
      const r = await runCli(sb, ["extension", "install", v3.zip, ...args, "--json"]);
      expect(r.code, `${args.join(" ")}: ${r.stdout}`).toBe(1);
      expect(JSON.parse(r.stdout)).toMatchObject({ ok: false, error: expect.stringMatching(/does not match the expected SHA-256/) });
    }
    expect((await listJson()).extensions).toEqual([{ name: "probe", version: "0.2.0", installed: ["0.2.0"] }]);
    const right = await runCli(sb, ["extension", "install", v3.zip, `--sha256=${v3.sha256}`, "--json"]);
    expect(right.code, right.stdout).toBe(0);
    expect((await listJson()).extensions).toEqual([{ name: "probe", version: "0.3.0", installed: ["0.3.0"] }]);
  });

  it("installs from a URL only with a digest, and from a release manifest that names one", async () => {
    covers("cli.extension.install");
    const remote = zipExtension(work, { name: "remote", version: "1.0.0" });
    const bytes = fs.readFileSync(remote.zip);
    const fake = await FakeHttp.start((req) => {
      if (req.url === "/remote.zip") return { body: bytes, headers: { "content-type": "application/zip" } };
      if (req.url === "/remote.json") return { json: { url: "remote.zip", sha256: remote.sha256 } };
      return { status: 404 };
    });
    try {
      const bare = await runCli(sb, ["extension", "install", `${fake.origin}/remote.zip`, "--json"]);
      expect(bare.code).toBe(1);
      expect(JSON.parse(bare.stdout).error).toMatch(/needs --sha256/);
      expect(fake.requests).toEqual([]);
      expect(fs.existsSync(path.join(extDir, "remote@1.0.0"))).toBe(false);

      const viaManifest = await runCli(sb, ["extension", "install", `${fake.origin}/remote.json`, "--json"]);
      expect(viaManifest.code, viaManifest.stdout).toBe(0);
      expect(JSON.parse(viaManifest.stdout)).toMatchObject({ name: "remote", version: "1.0.0", sha256: remote.sha256 });
      expect(fake.requests.map((r) => r.url)).toEqual(["/remote.json", "/remote.zip"]);
    } finally {
      await fake.stop();
    }
    expect((await listJson()).extensions.map((e) => e.name).sort()).toEqual(["probe", "remote"]);
  });

  it("removes one extension and nothing else", async () => {
    covers("cli.extension.remove");
    const r = await runCli(sb, ["extension", "remove", "remote", "--json"]);
    expect(r.code, r.stderr).toBe(0);
    expect(JSON.parse(r.stdout)).toEqual({ ok: true, removed: true, daemonRestarted: false });
    expect(fs.readdirSync(extDir)).toEqual(["probe@0.3.0"]);

    const again = await runCli(sb, ["extension", "remove", "remote"]);
    expect(again.code).toBe(0);
    expect(plain(again.stdout + again.stderr)).toContain("remote is not installed");
  });

  it("refuses a name that reaches outside the extensions directory", async () => {
    covers("cli.extension.remove");
    const userDir = path.join(sb.home, ".ue-mcp");
    fs.writeFileSync(path.join(userDir, "keep.txt"), "user state");
    for (const name of ["..", ".", "../extensions", "probe/..", path.join(sb.home, ".ue-mcp")]) {
      const r = await runCli(sb, ["extension", "remove", name, "--json"]);
      expect(r.code, `${name}: ${r.stdout}`).toBe(1);
      expect(JSON.parse(r.stdout)).toMatchObject({ ok: false, error: expect.stringMatching(/not an extension name/) });
    }
    expect(fs.readFileSync(path.join(userDir, "keep.txt"), "utf8")).toBe("user state");
    expect(fs.readdirSync(extDir)).toEqual(["probe@0.3.0"]);
  });
});

describe("a daemon with extensions installed", () => {
  let project: string;
  let d: Discovery;
  let uiStore: string;

  beforeAll(async () => {
    project = sb.makeProject("Hosted");
    uiStore = path.join(sb.home, ".ue-mcp", "ui");
    // "probe" is installed by the CLI cases above; "shell" ships a signed UI bundle.
    writeExtension(extDir, { name: "shell", version: "2.0.0", bundle: { version: "1.0.0", marker: "trusted-ui-1" } });
    // Newer bundles the daemon must not serve: one signed by a key nobody trusts, one altered after signing.
    signedBundle(path.join(uiStore, "integ-ui-9.0.0"), "9.0.0", "untrusted-key", (await import("node:crypto")).generateKeyPairSync("ed25519").privateKey.export({ type: "pkcs8", format: "pem" }).toString());
    signedBundle(path.join(uiStore, "integ-ui-8.0.0"), "8.0.0", "tampered");
    fs.appendFileSync(path.join(uiStore, "integ-ui-8.0.0", "index.html"), "<script>alert(1)</script>");
    // One that targets another extension API, and so never loads.
    writeExtension(extDir, { name: "future", version: "9.0.0", pkg: { ueMcpExtension: { name: "future", api: { min: 99, max: 99 } } } });
    d = await startDaemonCli(sb, project);
  });

  it("loads the compatible extensions and serves their routes inside the daemon", async () => {
    covers("daemon.extension-host");
    const health = await call(d, "GET", "/v1/health");
    expect(health.body.extensions.sort((a: { name: string }, b: { name: string }) => a.name.localeCompare(b.name)))
      .toEqual([{ name: "probe", version: "0.3.0" }, { name: "shell", version: "2.0.0" }]);

    const who = await call(d, "GET", "/v1/ext/probe/whoami");
    expect(who.status).toBe(200);
    expect(who.body).toMatchObject({ name: "probe", pid: d.pid, projectDir: path.dirname(project) });
    expect(fs.existsSync(who.body.dataDir)).toBe(true);
    expect(who.body.dataDir.startsWith(path.join(sb.home, ".ue-mcp", "data"))).toBe(true);

    const echo = await call(d, "POST", "/v1/ext/probe/echo/42", { hello: "world" });
    expect(echo).toMatchObject({ status: 201, body: { id: "42", got: { hello: "world" } } });

    expect((await call(d, "GET", "/v1/ext/probe/missing")).status).toBe(404);
    expect((await call(d, "GET", "/v1/ext/future/whoami")).status).toBe(404);
    expect((await fetch(`${base(d)}/v1/ext/probe/whoami`)).status).toBe(401);

    const events = await readSse(`${base(d)}/v1/events?since=0`, bearer(d), (es) => es.some((e) => e.type === "probe.activated"));
    expect(events.find((e) => e.type === "probe.activated")!.data).toMatchObject({ name: "probe" });
  });

  it("serves only the bundle signed by a trusted key and intact, behind the token", async () => {
    covers("daemon.ui-bundles");
    const page = await fetch(`${base(d)}/ui/`, { headers: bearer(d) });
    expect(page.status).toBe(200);
    expect(page.headers.get("content-type")).toMatch(/text\/html/);
    expect(await page.text()).toContain("trusted-ui-1");
    const asset = await fetch(`${base(d)}/ui/assets/app.js`, { headers: bearer(d) });
    expect(await asset.text()).toContain("trusted-ui-1");
    expect((await fetch(`${base(d)}/ui/manifest.sig`, { headers: bearer(d) })).status).toBe(404);
    expect((await fetch(`${base(d)}/ui/`)).status).toBe(401);
    expect((await call(d, "GET", "/v1/health")).body.ui).toBe("1.0.0");

    // The link `daemon open` prints trades the token for a cookie scoped to this daemon.
    const login = await fetch(`${base(d)}/ui/?token=${d.token}`, { redirect: "manual" });
    expect(login.status).toBe(302);
    const cookie = login.headers.get("set-cookie")!;
    expect(cookie).toMatch(new RegExp(`^ue_mcp_token_${d.port}=${d.token};`));
    const byCookie = await fetch(`${base(d)}/ui/`, { headers: { cookie: cookie.split(";")[0] } });
    expect(await byCookie.text()).toContain("trusted-ui-1");
    expect((await fetch(`${base(d)}/ui/?token=${"0".repeat(64)}`, { redirect: "manual" })).status).toBe(401);
  });

  it("restarts the project's daemon when an extension is installed or removed for it", async () => {
    covers("cli.extension.install", "cli.extension.remove", "daemon.extension-host");
    const extra = zipExtension(work, { name: "late", version: "0.0.1" });
    const r = await runCli(sb, ["extension", "install", extra.zip, project, "--json"], { env: daemonEnv() });
    expect(r.code, r.stderr).toBe(0);
    expect(JSON.parse(r.stdout)).toMatchObject({ ok: true, name: "late", daemonRestarted: true });
    await waitGone(sb, project, d.pid);
    const next = readDiscovery(sb, project)!;
    expect(next.pid).not.toBe(d.pid);
    expect((await call(next, "GET", "/v1/ext/late/whoami")).body).toMatchObject({ name: "late", pid: next.pid });

    const rm = await runCli(sb, ["extension", "remove", "late", project, "--json"], { env: daemonEnv() });
    expect(JSON.parse(rm.stdout)).toEqual({ ok: true, removed: true, daemonRestarted: true });
    await waitGone(sb, project, next.pid);
    const last = readDiscovery(sb, project)!;
    expect((await call(last, "GET", "/v1/ext/late/whoami")).status).toBe(404);
    expect((await call(last, "GET", "/v1/ext/probe/whoami")).status).toBe(200);
  });
});

describe("a daemon no extension trusts a key for", () => {
  it("serves no UI, even with bundles in the store", async () => {
    covers("daemon.ui-bundles");
    const lone = new Sandbox();
    try {
      const project = lone.makeProject("Bare");
      signedBundle(path.join(lone.home, ".ue-mcp", "ui", "integ-ui-1.0.0"), "1.0.0", "nobody-trusts-me");
      const d = await startDaemonCli(lone, project);
      const res = await fetch(`${base(d)}/ui/`, { headers: bearer(d) });
      expect(res.status).toBe(503);
      expect(await res.text()).toContain("No client UI is installed");
      expect((await call(d, "GET", "/v1/health")).body).toMatchObject({ ui: null, extensions: [] });
    } finally {
      await stopSandboxDaemons(lone);
      lone.cleanup();
    }
  });
});

describe("a daemon with an older extension than its newest UI bundle", () => {
  it("serves the bundle at the loaded extension's version, never the newer one (#1618)", async () => {
    covers("daemon.ui-bundles");
    const older = new Sandbox();
    try {
      const project = older.makeProject("Older");
      // A newer Studio once installed its UI; this one is the older extension that ships 1.0.0.
      signedBundle(path.join(older.home, ".ue-mcp", "ui", "integ-ui-2.0.0"), "2.0.0", "newer-ui");
      writeExtension(path.join(older.home, ".ue-mcp", "extensions"), {
        name: "integ-ui", version: "1.0.0", bundle: { version: "1.0.0", marker: "matching-ui" },
        activate: `api.route("GET", "/active", () => ({ body: api.ui.active() }));`,
      });
      const d = await startDaemonCli(older, project);
      expect(fs.existsSync(path.join(older.home, ".ue-mcp", "ui", "integ-ui-2.0.0"))).toBe(true);
      const page = await fetch(`${base(d)}/ui/`, { headers: bearer(d) });
      expect(page.status).toBe(200);
      const html = await page.text();
      expect(html).toContain("matching-ui");
      expect(html).not.toContain("newer-ui");
      expect((await call(d, "GET", "/v1/health")).body).toMatchObject({ ui: "1.0.0", extensions: [{ name: "integ-ui", version: "1.0.0" }] });
      expect((await call(d, "GET", "/v1/ext/integ-ui/active")).body).toMatchObject({ name: "integ-ui", version: "1.0.0" });
    } finally {
      await stopSandboxDaemons(older);
      older.cleanup();
    }
  });
});

describe("projects on different versions of one extension (#1619)", () => {
  const versionRoute = `api.route("GET", "/versions", () => ({ body: api.versions ? api.versions() : null }));`;

  it("migrates the single install, keeps each project on its recorded version, and serves the matching UI", async () => {
    covers("daemon.extension-versions", "cli.extension.use", "cli.extension.install", "daemon.ui-bundles");
    const multi = new Sandbox();
    try {
      const ext = path.join(multi.home, ".ue-mcp", "extensions");
      const work2 = multi.path("work");
      fs.mkdirSync(work2, { recursive: true });
      const a = multi.makeProject("Alpha");
      const b = multi.makeProject("Bravo");
      const c = multi.makeProject("Charlie");
      // What an older ue-mcp installed: one unversioned folder every project loads.
      writeExtension(ext, { name: "integ-ui", version: "1.0.0", bundle: { version: "1.0.0", marker: "ui-one" }, activate: versionRoute });
      // Bravo ran it before, but its daemon has not started since this ue-mcp arrived.
      fs.mkdirSync(path.join(multi.home, ".ue-mcp", "data", projectKey(path.dirname(b)), "integ-ui"), { recursive: true });

      // Alpha's daemon migrates the install and records Alpha at it.
      const da = await startDaemonCli(multi, a);
      expect(fs.readdirSync(ext).sort()).toEqual(["integ-ui", "integ-ui@1.0.0"]);
      expect((await call(da, "GET", "/v1/ext/integ-ui/versions")).body).toEqual({ loaded: "1.0.0", recorded: "1.0.0", installed: ["1.0.0"] });

      // Charlie updates to 2.0.0. Alpha's record keeps 1.0.0 installed, and the unversioned folder is left for older daemons.
      const v2 = zipExtension(work2, { name: "integ-ui", version: "2.0.0", bundle: { version: "2.0.0", marker: "ui-two" }, activate: versionRoute });
      const r = await runCli(multi, ["extension", "install", v2.zip, c, "--json"], { env: daemonEnv() });
      expect(r.code, r.stderr).toBe(0);
      expect(JSON.parse(r.stdout)).toMatchObject({ ok: true, name: "integ-ui", version: "2.0.0" });
      expect(fs.readdirSync(ext).sort()).toEqual(["integ-ui", "integ-ui@1.0.0", "integ-ui@2.0.0"]);
      expect(JSON.parse(fs.readFileSync(path.join(ext, "integ-ui", "package.json"), "utf8")).version).toBe("1.0.0");

      const dc = await startDaemonCli(multi, c);
      expect((await call(dc, "GET", "/v1/ext/integ-ui/versions")).body).toEqual({ loaded: "2.0.0", recorded: "2.0.0", installed: ["1.0.0", "2.0.0"] });
      expect(await (await fetch(`${base(dc)}/ui/`, { headers: bearer(dc) })).text()).toContain("ui-two");

      // Alpha still runs 1.0.0 and its UI, and sees the newer version Charlie installed.
      expect((await call(da, "GET", "/v1/ext/integ-ui/versions")).body).toEqual({ loaded: "1.0.0", recorded: "1.0.0", installed: ["1.0.0", "2.0.0"] });
      const pageA = await (await fetch(`${base(da)}/ui/`, { headers: bearer(da) })).text();
      expect(pageA).toContain("ui-one");
      expect(pageA).not.toContain("ui-two");

      // Bravo had used the unversioned copy, so it is recorded at 1.0.0, not moved to the newest.
      const db = await startDaemonCli(multi, b);
      expect((await call(db, "GET", "/v1/ext/integ-ui/versions")).body).toMatchObject({ loaded: "1.0.0", recorded: "1.0.0" });
      // A project that never ran it starts on the newest.
      const fresh = multi.makeProject("Delta");
      const dd = await startDaemonCli(multi, fresh);
      expect((await call(dd, "GET", "/v1/ext/integ-ui/versions")).body).toMatchObject({ loaded: "2.0.0", recorded: "2.0.0" });

      // Alpha moves on only when asked, and its daemon restarts on the new version.
      const use = await runCli(multi, ["extension", "use", "integ-ui", "2.0.0", a, "--json"], { env: daemonEnv() });
      expect(use.code, use.stdout + use.stderr).toBe(0);
      expect(JSON.parse(use.stdout)).toMatchObject({ ok: true, name: "integ-ui", version: "2.0.0", daemonRestarted: true });
      // The old daemon has exited before the command returns, so the project never has two.
      expect(pidAlive(da.pid)).toBe(false);
      await waitGone(multi, a, da.pid);
      const da2 = readDiscovery(multi, a)!;
      expect((await call(da2, "GET", "/v1/ext/integ-ui/versions")).body).toMatchObject({ loaded: "2.0.0", recorded: "2.0.0" });
      expect(await (await fetch(`${base(da2)}/ui/`, { headers: bearer(da2) })).text()).toContain("ui-two");
      // Bravo did not move.
      expect((await call(db, "GET", "/v1/ext/integ-ui/versions")).body).toMatchObject({ loaded: "1.0.0", recorded: "1.0.0" });

      const list = await runCli(multi, ["extension", "list", b, "--json"]);
      expect(JSON.parse(list.stdout).extensions).toEqual([{ name: "integ-ui", version: "1.0.0", installed: ["1.0.0", "2.0.0"], recorded: "1.0.0" }]);
      const missing = await runCli(multi, ["extension", "use", "integ-ui", "9.9.9", b, "--json"], { env: daemonEnv() });
      expect(missing.code).toBe(1);
      expect(JSON.parse(missing.stdout).error).toMatch(/integ-ui 9\.9\.9 is not installed; installed: 1\.0\.0, 2\.0\.0/);
    } finally {
      await stopSandboxDaemons(multi);
      multi.cleanup();
    }
  });
});
