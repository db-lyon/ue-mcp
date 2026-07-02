import * as net from "node:net";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import type { ProjectContext } from "./project.js";
import type { UeMcpConfig } from "./project.js";
import { proxyPortForProject, readProxyLockfile } from "./proxy.js";

/**
 * Client-side glue for the relay daemon. The stdio MCP server calls
 * ensureProxy() at startup; if it returns a target, the server points its
 * EditorBridge at the daemon instead of connecting to the editor directly.
 *
 * The daemon is ON by default (opt-out). Every failure path degrades to
 * "connect directly to the editor" (returns null) so enabling the daemon can
 * never leave a user worse off than the pre-daemon behaviour.
 */

export interface ProxyTarget {
  host: string;
  port: number;
}

function isProxyDisabled(cfg?: UeMcpConfig["proxy"]): boolean {
  if (process.env.UE_MCP_PROXY === "0" || process.env.UE_MCP_PROXY === "false") return true;
  return cfg?.enabled === false;
}

/** Resolve the configured host/port for a project's daemon (no I/O). */
export function proxyTargetFor(project: ProjectContext): ProxyTarget | null {
  if (!project.projectPath) return null;
  const cfg = project.config.proxy;
  const host = cfg?.host ?? process.env.UE_MCP_PROXY_HOST ?? "127.0.0.1";
  const envPort = Number.parseInt(process.env.UE_MCP_PROXY_PORT ?? "", 10);
  const port = proxyPortForProject(project.projectPath, cfg?.port ?? (envPort > 0 ? envPort : undefined));
  return { host, port };
}

/** Is something accepting connections at host:port right now? */
export function isPortOpen(host: string, port: number, timeoutMs = 500): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = net.connect({ host, port });
    let settled = false;
    const done = (open: boolean) => {
      if (settled) return;
      settled = true;
      socket.destroy();
      resolve(open);
    };
    socket.setTimeout(timeoutMs);
    socket.once("connect", () => done(true));
    socket.once("timeout", () => done(false));
    socket.once("error", () => done(false));
  });
}

async function waitForPort(host: string, port: number, timeoutMs: number): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await isPortOpen(host, port, 500)) return true;
    await new Promise((r) => setTimeout(r, 250));
  }
  return false;
}

/** Path to the compiled daemon entrypoint that sits beside this module. */
function proxyCliPath(): string {
  return fileURLToPath(new URL("./proxy-cli.js", import.meta.url));
}

function spawnDaemon(projectPath: string): void {
  const child = spawn(process.execPath, [proxyCliPath(), projectPath], {
    detached: true,
    stdio: "ignore",
    windowsHide: true,
  });
  child.unref();
}

/**
 * Ensure a daemon is reachable for this project and return its target, or null
 * to signal "fall back to a direct editor connection".
 *
 * 1. Disabled (config/env)         -> null
 * 2. Daemon already listening       -> use it
 * 3. Otherwise spawn one, wait for  -> use it once reachable
 * 4. Spawn never came up            -> null (direct fallback)
 */
export async function ensureProxy(project: ProjectContext): Promise<ProxyTarget | null> {
  const cfg = project.config.proxy;
  if (isProxyDisabled(cfg)) {
    console.error("[ue-mcp] Relay daemon disabled (proxy.enabled=false) - connecting directly to the editor");
    return null;
  }
  const target = proxyTargetFor(project);
  if (!target) return null;

  // Prefer a lockfile-advertised port if the daemon happens to run elsewhere.
  const lock = readProxyLockfile(project.projectPath);
  if (lock && (await isPortOpen(lock.host, lock.port))) {
    return { host: lock.host, port: lock.port };
  }

  if (await isPortOpen(target.host, target.port)) {
    return target;
  }

  spawnDaemon(project.projectPath!);
  if (await waitForPort(target.host, target.port, 8000)) {
    console.error(`[ue-mcp] Started relay daemon at ${target.host}:${target.port}`);
    return target;
  }

  console.error("[ue-mcp] Relay daemon did not come up in time - connecting directly to the editor");
  return null;
}
