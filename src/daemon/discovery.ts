/**
 * Where a project's daemon publishes itself: one file per project root under
 * the user config dir, readable only by the user, holding the address and the
 * bearer token every request must carry.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { normalizeProjectRoot } from "../bridge/port.js";
import { isPidAlive } from "../bridge/editor-target.js";
import { readEnv } from "../core/env.js";
import { userDir } from "../core/user-dir.js";

/** Version of the daemon's HTTP and shim protocol. Bump on a breaking change. */
export const DAEMON_API_VERSION = 1;

export interface DaemonDiscovery {
  pid: number;
  port: number;
  token: string;
  /** ue-mcp package version the daemon runs. */
  version: string;
  apiVersion: number;
  /** Normalized project root (see normalizeProjectRoot). */
  projectRoot: string;
  startedAt: string;
}

export function daemonDir(): string {
  return readEnv("daemonDir") ?? path.join(userDir(), "daemons");
}

export function projectKey(projectDir: string): string {
  return crypto.createHash("sha1").update(normalizeProjectRoot(projectDir)).digest("hex").slice(0, 16);
}

export function discoveryPath(projectDir: string): string {
  return path.join(daemonDir(), `${projectKey(projectDir)}.json`);
}

export function readDiscovery(projectDir: string): DaemonDiscovery | null {
  try {
    const raw = JSON.parse(fs.readFileSync(discoveryPath(projectDir), "utf-8")) as Partial<DaemonDiscovery>;
    if (typeof raw.pid !== "number" || typeof raw.port !== "number" || typeof raw.token !== "string") return null;
    if (typeof raw.projectRoot !== "string" || raw.projectRoot !== normalizeProjectRoot(projectDir)) return null;
    return {
      pid: raw.pid,
      port: raw.port,
      token: raw.token,
      version: typeof raw.version === "string" ? raw.version : "0.0.0",
      apiVersion: typeof raw.apiVersion === "number" ? raw.apiVersion : 0,
      projectRoot: raw.projectRoot,
      startedAt: typeof raw.startedAt === "string" ? raw.startedAt : "",
    };
  } catch {
    return null;
  }
}

/** The discovery record when its process is still alive, else null. */
export function readLiveDiscovery(projectDir: string, isAlive: (pid: number) => boolean = isPidAlive): DaemonDiscovery | null {
  const d = readDiscovery(projectDir);
  return d && isAlive(d.pid) ? d : null;
}

/** Publish by write-then-rename so a reader never sees half a file. Mode 600. */
export function writeDiscovery(projectDir: string, d: DaemonDiscovery): void {
  const file = discoveryPath(projectDir);
  fs.mkdirSync(path.dirname(file), { recursive: true, mode: 0o700 });
  const tmp = `${file}.${process.pid}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(d, null, 2), { mode: 0o600 });
  fs.renameSync(tmp, file);
}

/** Remove the record, but only if it is this process's. */
export function removeDiscovery(projectDir: string, pid: number = process.pid): void {
  const current = readDiscovery(projectDir);
  if (current && current.pid !== pid) return;
  fs.rmSync(discoveryPath(projectDir), { force: true });
}

export function newToken(): string {
  return crypto.randomBytes(32).toString("hex");
}
