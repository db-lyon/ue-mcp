/**
 * `ue-mcp daemon` and the daemon's lifecycle, spawned: run, start, stop,
 * status and open; the idle exit; and a newer daemon taking over from an
 * older one. How a stdio server picks daemon or in process is in
 * daemon-server-mode.test.ts.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { normalizeProjectRoot } from "../../src/bridge/port.js";
import { covers } from "../helpers/covers.js";
import { FakeHttp, PACKAGE_VERSION, plain, runCli, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import {
  base,
  bearer,
  call,
  daemonDirOf,
  daemonEnv,
  discoveryFile,
  pidAlive,
  readDiscovery,
  readSse,
  spawnCli,
  startDaemonCli,
  waitGone,
} from "./daemon-fakes.js";

let sb: Sandbox;
beforeAll(() => {
  sb = new Sandbox();
});
afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

const daemon = (args: string[], env: Record<string, string | undefined> = {}) => runCli(sb, ["daemon", ...args], { env: daemonEnv(env) });

describe("ue-mcp daemon", () => {
  it("prints usage and exits 2 without a known subcommand or a project", async () => {
    covers("cli.daemon.usage");
    const project = sb.makeProject("Usage");
    const [none, unknown, noProject] = await Promise.all([
      daemon([project]),
      daemon(["frobnicate", project]),
      daemon(["status"]),
    ]);
    for (const r of [none, unknown]) {
      expect(r.code).toBe(2);
      expect(plain(r.stderr + r.stdout)).toContain("usage: ue-mcp daemon <run|start|stop|status|open>");
    }
    expect(noProject.code).toBe(2);
    expect(plain(noProject.stderr + noProject.stdout)).toContain("No .uproject found");
    expect(fs.existsSync(daemonDirOf(sb)) ? fs.readdirSync(daemonDirOf(sb)).filter((f) => f.endsWith(".json")) : []).toEqual([]);
  });

  it("starts one daemon, reports it, prints its UI address, and stops it", async () => {
    covers("cli.daemon.start", "cli.daemon.status", "cli.daemon.open", "cli.daemon.stop");
    const project = sb.makeProject("Life");

    const before = await daemon(["status", project, "--json"]);
    expect(before.code).toBe(0);
    expect(JSON.parse(before.stdout)).toEqual({ ok: true, running: false });

    const started = await daemon(["start", project, "--json"]);
    expect(started.code, started.stderr).toBe(0);
    const s = JSON.parse(started.stdout) as { ok: boolean; pid: number; port: number; version: string };
    expect(s).toMatchObject({ ok: true, version: PACKAGE_VERSION });
    expect(pidAlive(s.pid)).toBe(true);
    const d = readDiscovery(sb, project)!;
    expect(d).toMatchObject({ pid: s.pid, port: s.port });

    // A second start finds the running one rather than starting another.
    const again = await daemon(["start", project]);
    expect(again.code).toBe(0);
    expect(plain(again.stdout + again.stderr)).toContain(`running (pid ${s.pid}, port ${s.port})`);

    const status = await daemon(["status", project, "--json"]);
    expect(JSON.parse(status.stdout)).toMatchObject({
      ok: true,
      running: true,
      discovery: { pid: s.pid, port: s.port, version: PACKAGE_VERSION },
      health: { ok: true, pid: s.pid, clients: 0, editors: [{ name: "Life", connected: false }] },
    });
    const statusText = await daemon(["status", project]);
    expect(plain(statusText.stdout + statusText.stderr)).toContain(`pid ${s.pid} port ${s.port}, 0 client(s)`);

    const url = `http://127.0.0.1:${s.port}/ui/?token=${d.token}`;
    const open = await daemon(["open", project, "--json"]);
    expect(JSON.parse(open.stdout)).toEqual({ ok: true, url });
    const openText = await daemon(["open", project]);
    expect(openText.stdout.trim()).toBe(url);
    // The printed address logs the browser in.
    const login = await fetch(url, { redirect: "manual" });
    expect(login.status).toBe(302);
    expect(login.headers.get("set-cookie")).toContain(d.token);

    const stopped = await daemon(["stop", project, "--json"]);
    expect(JSON.parse(stopped.stdout)).toEqual({ ok: true, stopped: s.pid });
    // stop returns only once the process is gone.
    expect(pidAlive(s.pid)).toBe(false);
    await waitGone(sb, project, s.pid);
    expect(fs.existsSync(discoveryFile(sb, project))).toBe(false);
    expect(JSON.parse((await daemon(["status", project, "--json"])).stdout)).toEqual({ ok: true, running: false });
    const stopNone = await daemon(["stop", project]);
    expect(stopNone.code).toBe(0);
    expect(plain(stopNone.stdout + stopNone.stderr)).toContain("no daemon running for this project");
  });

  it("open starts a daemon when none is running", async () => {
    covers("cli.daemon.open");
    const project = sb.makeProject("OpenCold");
    const r = await daemon(["open", project, "--json"]);
    expect(r.code, r.stderr).toBe(0);
    const d = readDiscovery(sb, project)!;
    expect(JSON.parse(r.stdout).url).toBe(`http://127.0.0.1:${d.port}/ui/?token=${d.token}`);
    expect((await call(d, "GET", "/v1/health")).body.pid).toBe(d.pid);
  });

  it("run serves in the foreground until asked to stop, and defers to a running daemon", async () => {
    covers("cli.daemon.run");
    const project = sb.makeProject("Foreground");
    const fg = spawnCli(sb, ["daemon", "run", project], daemonEnv());
    const d = (await waitFor(() => {
      const rec = readDiscovery(sb, project);
      return rec?.pid === fg.child.pid ? rec : null;
    }, 30_000, "the foreground daemon to publish"))!;
    expect((await call(d, "GET", "/v1/health")).body).toMatchObject({ ok: true, pid: fg.child.pid, version: PACKAGE_VERSION });

    // A second run for the same project leaves the first in charge.
    const second = await daemon(["run", project], { UE_MCP_LOG_LEVEL: "info" });
    expect(second.code).toBe(0);
    expect(plain(second.stderr)).toContain(`already running (pid ${d.pid})`);
    expect(readDiscovery(sb, project)?.pid).toBe(d.pid);

    expect((await call(d, "POST", "/v1/shutdown")).status).toBe(202);
    expect(await fg.exited).toBe(0);
    expect(fs.existsSync(discoveryFile(sb, project))).toBe(false);
  });
});

describe("idle exit", () => {
  it("exits once nothing is attached for the idle window, and not while a stream is open", async () => {
    covers("daemon.idle-exit");
    const project = sb.makeProject("Idle");
    const d = await startDaemonCli(sb, project, { UE_MCP_DAEMON_IDLE_MS: "2000" });
    // An open event stream keeps it alive past the window.
    const held = readSse(`${base(d)}/v1/events?since=0`, bearer(d), () => false, 5000).catch(() => "closed");
    await new Promise((r) => setTimeout(r, 4500));
    expect(pidAlive(d.pid)).toBe(true);
    expect(readDiscovery(sb, project)?.pid).toBe(d.pid);
    await held;
    const t0 = Date.now();
    await waitGone(sb, project, d.pid, 15_000);
    expect(Date.now() - t0).toBeGreaterThanOrEqual(1000);
  });
});

describe("handoff", () => {
  /** A discovery record for a daemon this test process plays, served by `fake`. */
  function publishFake(project: string, fake: FakeHttp, version: string): void {
    const file = discoveryFile(sb, project);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify({
      pid: process.pid,
      port: Number(new URL(fake.origin).port),
      token: "older-daemon-token",
      version,
      apiVersion: 1,
      projectRoot: normalizeProjectRoot(path.dirname(project)),
      startedAt: new Date().toISOString(),
    }));
  }

  it("a newer daemon asks an older one to hand off, then publishes itself", async () => {
    covers("daemon.handoff");
    const project = sb.makeProject("Handoff");
    const fake = await FakeHttp.start((req) => {
      // The newer daemon first checks the record is a daemon answering for it (#1460).
      if (req.url === "/v1/health") return { json: { ok: true, pid: process.pid } };
      // The older daemon goes away when asked, taking its record with it.
      if (req.url === "/v1/handoff") setTimeout(() => fs.rmSync(discoveryFile(sb, project), { force: true }), 300);
      return { status: 202 };
    });
    try {
      publishFake(project, fake, "0.0.1");
      const d = await startDaemonCli(sb, project);
      expect(d.pid).not.toBe(process.pid);
      expect(d.version).toBe(PACKAGE_VERSION);
      const handoffs = fake.requests.filter((r) => r.url === "/v1/handoff");
      expect(handoffs).toHaveLength(1);
      expect(handoffs[0]).toMatchObject({ method: "POST", headers: { authorization: "Bearer older-daemon-token" } });
      expect((await call(d, "GET", "/v1/health")).body.pid).toBe(d.pid);

      // A real daemon told to hand off exits and withdraws its record.
      expect((await call(d, "POST", "/v1/handoff")).status).toBe(202);
      await waitGone(sb, project, d.pid);
      expect(fs.existsSync(discoveryFile(sb, project))).toBe(false);
    } finally {
      await fake.stop();
    }
  });

  it("a daemon whose record another live daemon took over stops itself", async () => {
    covers("daemon.supersede");
    const project = sb.makeProject("Superseded");
    const d = await startDaemonCli(sb, project);
    try {
      // What a successor publishes: a live process that is not this daemon.
      fs.writeFileSync(discoveryFile(sb, project), JSON.stringify({ ...readDiscovery(sb, project), pid: process.pid, token: "successor" }));
      await waitFor(() => !pidAlive(d.pid), 20_000, `daemon pid ${d.pid} to stop`);
      // It leaves the successor's record alone.
      expect(readDiscovery(sb, project)).toMatchObject({ pid: process.pid, token: "successor" });
    } finally {
      fs.rmSync(discoveryFile(sb, project), { force: true });
    }
  });

  it("a daemon starts in place of a record whose pid was reused (#1460)", async () => {
    covers("daemon.handoff");
    const project = sb.makeProject("Recycled");
    // A live pid (this test process) on a port where no daemon answers.
    const fake = await FakeHttp.start(() => ({ status: 404 }));
    try {
      publishFake(project, fake, "999.0.0");
      const status = await daemon(["status", project, "--json"]);
      expect(JSON.parse(status.stdout)).toEqual({ ok: true, running: false });
      expect(fs.existsSync(discoveryFile(sb, project))).toBe(false);

      publishFake(project, fake, "999.0.0");
      const d = await startDaemonCli(sb, project);
      expect(d.pid).not.toBe(process.pid);
      expect(fake.requests.map((q) => q.url)).toEqual(["/v1/health", "/v1/health"]);
      expect((await call(d, "GET", "/v1/health")).body.pid).toBe(d.pid);
      expect((await call(d, "POST", "/v1/handoff")).status).toBe(202);
      await waitGone(sb, project, d.pid);
    } finally {
      await fake.stop();
    }
  });

  it("a daemon defers to a newer one that is already running", async () => {
    covers("daemon.handoff");
    const project = sb.makeProject("Newer");
    const fake = await FakeHttp.start((req) => (req.url === "/v1/health" ? { json: { ok: true, pid: process.pid } } : { status: 202 }));
    try {
      publishFake(project, fake, "999.0.0");
      const r = await daemon(["run", project], { UE_MCP_LOG_LEVEL: "info" });
      expect(r.code).toBe(0);
      expect(plain(r.stderr)).toContain("daemon 999.0.0 already running");
      // It only asked the running daemon to answer for its record.
      expect(fake.requests.map((q) => q.url)).toEqual(["/v1/health"]);
      expect(readDiscovery(sb, project)).toMatchObject({ pid: process.pid, version: "999.0.0" });
    } finally {
      fs.rmSync(discoveryFile(sb, project), { force: true });
      await fake.stop();
    }
  });
});
