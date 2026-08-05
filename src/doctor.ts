#!/usr/bin/env node
/**
 * `ue-mcp doctor` - diagnose every ue-mcp version source and flag the failure
 * mode nothing else surfaces: a project-local `node_modules/ue-mcp` that
 * silently shadows the global install (npx prefers the local copy, so updating
 * the global one changes nothing the server actually runs). (#550)
 *
 * Read-only: never mutates anything.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { execSync } from "node:child_process";
import { createRequire } from "node:module";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { isNewer } from "./version-check.js";

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const DIM = "\x1b[2m";
const CYAN = "\x1b[36m";
const YELLOW = "\x1b[33m";

export interface DoctorReport {
  selfVersion: string;            // the ue-mcp currently executing this command
  registryLatest: string | null;
  npmGlobal: { version: string | null; dir: string | null };
  localShadow: { version: string; dir: string } | null; // nearest node_modules/ue-mcp from cwd up
  effectiveNpx: string | null;    // what `npx ue-mcp` would run: shadow ?? global
  runningServers: Array<{
    pid: number;
    version: string | null;
    script: string;
    /** First project positional; the one a single-editor server drives. */
    project: string | null;
    /** Every project positional, so a multi-editor server reports all of them (#817). */
    projects?: string[];
    servesTarget: boolean;
  }>;
  targetProjectDir: string | null;   // dir of the .uproject doctor is reporting for
  bridgePlugin: { version: string | null; project: string } | null;
  bareNpxConfigs: string[];       // .mcp.json paths using bare `npx ue-mcp`
}

function safeExec(cmd: string): string | null {
  try {
    return execSync(cmd, { encoding: "utf-8", stdio: ["pipe", "pipe", "pipe"] }).trim();
  } catch {
    return null;
  }
}

function readJsonVersion(pkgJsonPath: string): string | null {
  try {
    const raw = fs.readFileSync(pkgJsonPath, "utf-8");
    const v = JSON.parse(raw).version;
    return typeof v === "string" ? v : null;
  } catch {
    return null;
  }
}

function selfVersion(): string {
  try {
    const require = createRequire(import.meta.url);
    return require("../package.json").version;
  } catch {
    return "unknown";
  }
}

function registryLatest(): string | null {
  return safeExec("npm view ue-mcp version");
}

function npmGlobal(): { version: string | null; dir: string | null } {
  const root = safeExec("npm root -g");
  if (!root) return { version: null, dir: null };
  const dir = path.join(root, "ue-mcp");
  return { version: readJsonVersion(path.join(dir, "package.json")), dir };
}

/** Walk up from cwd; the first node_modules/ue-mcp is what npx resolves locally. */
export function findLocalShadow(startDir: string): { version: string; dir: string } | null {
  let dir = path.resolve(startDir);
  for (;;) {
    const candidate = path.join(dir, "node_modules", "ue-mcp");
    const pkg = path.join(candidate, "package.json");
    if (fs.existsSync(pkg)) {
      const version = readJsonVersion(pkg);
      if (version) return { version, dir: candidate };
    }
    const parent = path.dirname(dir);
    if (parent === dir) return null;
    dir = parent;
  }
}

/**
 * From a running script path, walk up to the ue-mcp package.json and read its
 * version. Only accepts a package.json whose name is "ue-mcp", so a deleted
 * copy (stale process) returns null rather than picking up an unrelated
 * package.json higher in the tree (e.g. the host project's own).
 */
function versionForScript(scriptPath: string): string | null {
  let dir = path.dirname(scriptPath);
  for (let i = 0; i < 4; i++) {
    const pkg = path.join(dir, "package.json");
    if (fs.existsSync(pkg)) {
      try {
        const parsed = JSON.parse(fs.readFileSync(pkg, "utf-8"));
        if (parsed && parsed.name === "ue-mcp") return typeof parsed.version === "string" ? parsed.version : null;
      } catch { /* keep walking */ }
    }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

// Subcommands of `ue-mcp <x>` that are NOT a running server (one-shot CLIs).
const NON_SERVER_ARGS = new Set([
  "doctor", "update", "deploy", "build", "init", "hook", "uninstall-hooks",
  "auth", "feedback", "resolve", "plugin", "version", "--version", "-v", "--help", "-h", "help",
  // These three were missing, so `ue-mcp context lean`, `ue-mcp login` and
  // `ue-mcp logout` each parsed as a running server whose project was the
  // subcommand's own name.
  "context", "login", "logout",
]);

/**
 * Parse a `node .../ue-mcp/dist/index.js <project> [<project>...]` command line
 * into the script path and every project argument. Returns null when the
 * command line is not a running server (a flag/subcommand invocation, or no
 * ue-mcp index.js). Exported for tests. (#550, #817)
 *
 * `project` is the first positional and is what a single-editor report reads.
 * `projects` is all of them, so a server driving several editors is reported
 * as driving several rather than as driving the first one only.
 */
export function parseServerInvocation(
  cmd: string,
): { script: string; project: string | null; projects: string[] } | null {
  const scriptMatch = cmd.match(/([A-Za-z]:[\\/].*?|\/.*?)ue-mcp[\\/]dist[\\/]index\.js/i);
  if (!scriptMatch) return null;
  const script = path.normalize((scriptMatch[1] ?? "") + path.join("ue-mcp", "dist", "index.js"));

  // First argument after index.js decides server-vs-CLI. The char right after
  // "index.js" may be the script path's own closing quote - drop it first.
  const idx = cmd.toLowerCase().indexOf("index.js");
  const rest = cmd.slice(idx + "index.js".length).replace(/^"/, "").trim();
  const tokens = tokenizeArgs(rest);
  const firstArg = (tokens[0] ?? "").trim();
  if (!firstArg || firstArg.startsWith("-") || NON_SERVER_ARGS.has(firstArg)) return null;
  const projects = tokens.filter((t) => !t.startsWith("-"));
  return { script: script.replace(/\\/g, "/"), project: projects[0] ?? null, projects };
}

/** Split a command-line tail into arguments, honouring double quotes. */
function tokenizeArgs(rest: string): string[] {
  const out: string[] = [];
  let current = "";
  let quoted = false;
  let started = false;
  for (const ch of rest) {
    if (ch === '"') {
      quoted = !quoted;
      started = true;
      continue;
    }
    if (!quoted && /\s/.test(ch)) {
      if (started) out.push(current);
      current = "";
      started = false;
      continue;
    }
    current += ch;
    started = true;
  }
  if (started) out.push(current);
  return out.map((t) => t.trim()).filter((t) => t !== "");
}

/** The directory that owns a project arg (a .uproject file or its containing dir). */
function projectDirOf(projectArg: string | null): string | null {
  if (!projectArg) return null;
  const resolved = path.resolve(projectArg);
  return resolved.toLowerCase().endsWith(".uproject") ? path.dirname(resolved) : resolved;
}

/** Best-effort scan of running node processes for a ue-mcp server. */
function findRunningServers(): Array<{ pid: number; version: string | null; script: string; project: string | null; projects: string[] }> {
  const out: Array<{ pid: number; version: string | null; script: string; project: string | null; projects: string[] }> = [];
  const selfPid = process.pid;
  let lines: string[] = [];

  if (process.platform === "win32") {
    // PowerShell CIM gives the full command line, which tasklist does not.
    const raw = safeExec(
      'powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \\"Name=\'node.exe\'\\" | ForEach-Object { $_.ProcessId.ToString() + \'|\' + $_.CommandLine }"',
    );
    if (raw) lines = raw.split(/\r?\n/);
  } else {
    const raw = safeExec("ps -eo pid=,args=");
    if (raw) lines = raw.split(/\n/).map((l) => l.trim().replace(/^(\d+)\s+/, "$1|"));
  }

  for (const line of lines) {
    if (!line) continue;
    const sep = line.indexOf("|");
    if (sep < 0) continue;
    const pid = parseInt(line.slice(0, sep), 10);
    const cmd = line.slice(sep + 1);
    if (!Number.isFinite(pid) || pid === selfPid) continue;
    // Only count actual servers (index.js <project>), not flag/subcommand
    // invocations. We do NOT require the script to exist on disk: a stale server
    // still running after its node_modules copy was deleted is the single most
    // important thing to surface (version comes back null = "deleted files").
    const parsed = parseServerInvocation(cmd);
    if (!parsed) continue;
    if (out.some((s) => s.pid === pid)) continue;
    out.push({ pid, version: versionForScript(parsed.script), script: parsed.script, project: parsed.project, projects: parsed.projects });
  }
  return out;
}

function findUproject(projectArg: string | undefined, cwd: string): string | null {
  if (projectArg && projectArg.endsWith(".uproject") && fs.existsSync(projectArg)) {
    return path.resolve(projectArg);
  }
  let dir = path.resolve(cwd);
  for (let i = 0; i < 4; i++) {
    try {
      const found = fs.readdirSync(dir).find((f) => f.endsWith(".uproject"));
      if (found) return path.join(dir, found);
    } catch { /* ignore */ }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

function bridgePluginVersion(projectArg: string | undefined, cwd: string): { version: string | null; project: string } | null {
  const uproject = findUproject(projectArg, cwd);
  if (!uproject) return null;
  const upluginPath = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge", "UE_MCP_Bridge.uplugin");
  if (!fs.existsSync(upluginPath)) return { version: null, project: uproject };
  try {
    const parsed = JSON.parse(fs.readFileSync(upluginPath, "utf-8"));
    return { version: typeof parsed.VersionName === "string" ? parsed.VersionName : null, project: uproject };
  } catch {
    return { version: null, project: uproject };
  }
}

/**
 * Scan cwd + parents for a .mcp.json that launches the server via bare
 * `npx ue-mcp` (an npx server whose args include "ue-mcp" with no @version pin
 * and no -y) - the configuration that lets a local copy shadow the global one.
 */
export function findBareNpxConfigs(cwd: string): string[] {
  const hits: string[] = [];
  let dir = path.resolve(cwd);
  for (let i = 0; i < 4; i++) {
    const cfg = path.join(dir, ".mcp.json");
    if (fs.existsSync(cfg)) {
      try {
        const parsed = JSON.parse(fs.readFileSync(cfg, "utf-8"));
        const servers = (parsed && typeof parsed === "object" && parsed.mcpServers) || {};
        for (const key of Object.keys(servers)) {
          const s = servers[key] || {};
          const command = String(s.command ?? "");
          const args: string[] = Array.isArray(s.args) ? s.args.map((a: unknown) => String(a)) : [];
          const isNpx = /(^|[\\/])npx(\.cmd)?$/i.test(command) || command.toLowerCase() === "npx";
          const targetsUeMcp = args.some((a) => a === "ue-mcp");
          const pinned = args.some((a) => a.startsWith("ue-mcp@"));
          const selfHealing = args.includes("-y") || args.includes("--yes");
          if (isNpx && targetsUeMcp && !pinned && !selfHealing) { hits.push(cfg); break; }
        }
      } catch { /* ignore malformed config */ }
    }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return hits;
}

export function collectDoctor(projectArg?: string, cwd: string = process.cwd()): DoctorReport {
  const global = npmGlobal();
  const shadow = findLocalShadow(cwd);
  const uproject = findUproject(projectArg, cwd);
  const targetProjectDir = projectDirOf(uproject);
  const servers = findRunningServers().map((s) => {
    // A server serves the target when ANY of its projects is the target: a
    // multi-editor server driving the project doctor was asked about is
    // serving it, and reporting otherwise reads as "no server is running".
    const candidates = s.projects.length > 0 ? s.projects : [s.project];
    const servesTarget = !!(
      targetProjectDir &&
      candidates.some((p) => {
        const dir = projectDirOf(p);
        return dir !== null && dir.toLowerCase() === targetProjectDir.toLowerCase();
      })
    );
    return { ...s, servesTarget };
  });
  return {
    selfVersion: selfVersion(),
    registryLatest: registryLatest(),
    npmGlobal: global,
    localShadow: shadow,
    effectiveNpx: shadow ? shadow.version : global.version,
    runningServers: servers,
    targetProjectDir,
    bridgePlugin: uproject
      ? { version: bridgePluginVersion(projectArg, cwd)?.version ?? null, project: uproject }
      : null,
    bareNpxConfigs: findBareNpxConfigs(cwd),
  };
}

function row(label: string, value: string): string {
  return `  ${label.padEnd(16)}${value}`;
}

/**
 * True when `version` is older than the registry's stable `latest`.
 *
 * Every alignment verdict below asks this rather than "differs from latest".
 * A prerelease install is ahead of the stable line, not behind it, so plain
 * inequality would flag a beta tester's entire setup as stale and tell them to
 * update onto an older release.
 */
function behindLatest(version: string | null | undefined, latest: string | null): boolean {
  return !!latest && !!version && isNewer(latest, version);
}

export function formatDoctor(d: DoctorReport): string {
  const latest = d.registryLatest;
  const lines: string[] = [];
  lines.push("");
  lines.push(`  ${BOLD}${CYAN}ue-mcp doctor${RESET}`);
  lines.push("");

  lines.push(row("registry latest:", latest ? `${BOLD}${latest}${RESET}` : `${DIM}(offline)${RESET}`));
  lines.push(row("npm global:", d.npmGlobal.version ?? `${DIM}(not installed)${RESET}`));

  if (d.localShadow) {
    const rel = path.relative(process.cwd(), d.localShadow.dir).replace(/\\/g, "/") || d.localShadow.dir;
    const warn = `${RED}<-- WARN npx runs THIS, not global${RESET}`;
    lines.push(row("local shadow:", `${YELLOW}./${rel} @ ${d.localShadow.version}${RESET}  ${warn}`));
  } else {
    lines.push(row("local shadow:", `${GREEN}none${RESET}`));
  }

  const effLabel = d.effectiveNpx ?? "unknown";
  const effMismatch = behindLatest(d.effectiveNpx, latest);
  lines.push(row("effective (npx):", effMismatch ? `${RED}${effLabel}  (behind latest ${latest})${RESET}` : `${GREEN}${effLabel}${RESET}`));

  // Show servers, putting the one serving the target project first and naming
  // the project each serves so an unrelated dev server can't be mistaken for it.
  const servers = [...d.runningServers].sort((a, b) => Number(b.servesTarget) - Number(a.servesTarget));
  if (servers.length === 0) {
    lines.push(row("running server:", `${DIM}none detected${RESET}`));
  } else {
    for (const s of servers) {
      const deleted = s.version === null;
      const v = deleted ? `${RED}deleted files${RESET}` : (s.version as string);
      const names = (s.projects && s.projects.length > 0 ? s.projects : [s.project])
        .filter((p): p is string => !!p)
        .map((p) => path.basename(p.replace(/[\\/]+$/, "")) || p);
      const proj = names.length > 0 ? names.join(", ") : "?";
      const scope = s.servesTarget
        ? names.length > 1
          ? `${BOLD}this project${RESET} ${DIM}(with ${names.length - 1} more: ${proj})${RESET}`
          : `${BOLD}this project${RESET}`
        : `${DIM}${proj}${RESET}`;
      const mismatch = behindLatest(s.version, latest);
      const tag = deleted
        ? `${RED}(running pruned files - relaunch)${RESET}`
        : mismatch
          ? `${RED}(MISMATCH with latest ${latest})${RESET}`
          : `${GREEN}ok${RESET}`;
      lines.push(row("running server:", `${v}  ${DIM}pid ${s.pid}${RESET}  ${scope}  ${tag}`));
    }
  }

  if (d.bridgePlugin) {
    const v = d.bridgePlugin.version ?? `${DIM}(not deployed)${RESET}`;
    lines.push(row("bridge plugin:", `${v}  ${DIM}${path.basename(d.bridgePlugin.project)}${RESET}`));
  }

  lines.push("");

  const problems: string[] = [];
  if (d.localShadow && behindLatest(d.localShadow.version, latest)) {
    problems.push(
      `A project-local node_modules/ue-mcp@${d.localShadow.version} shadows the global install. ` +
      `npx runs it, so global updates do nothing. Fix: remove the dependency from package.json and delete node_modules/ue-mcp, ` +
      `or pin .mcp.json to \`npx -y ue-mcp@latest\`. Then run \`ue-mcp update --build\`.`,
    );
  } else if (behindLatest(d.npmGlobal.version, latest)) {
    // No shadow, but the global package is behind latest: a bare `npx ue-mcp`
    // (or any fresh launch) would run an old version next time. This must keep
    // the verdict from claiming "aligned" even when the running server happens
    // to be current (e.g. launched via `npx -y ue-mcp@latest`).
    problems.push(
      `npm global is ${d.npmGlobal.version}, latest is ${latest}. The running server is fine, but the next launch via bare \`npx ue-mcp\` would be stale. ` +
      `Run \`ue-mcp update\` (or npm i -g ue-mcp@latest).`,
    );
  }
  for (const cfg of d.bareNpxConfigs) {
    const rel = path.relative(process.cwd(), cfg).replace(/\\/g, "/") || cfg;
    problems.push(`${rel} launches with bare \`npx ue-mcp\`. Use \`npx -y ue-mcp@latest\` so the server self-heals to latest on each launch.`);
  }

  // The verdict on running servers is scoped to the target project. A server
  // serving a *different* project being on latest tells us nothing about this
  // one, and must not produce a false "aligned".
  const targetServers = d.targetProjectDir ? d.runningServers.filter((s) => s.servesTarget) : d.runningServers;
  for (const s of targetServers) {
    const label = s.servesTarget && d.bridgePlugin ? `for ${path.basename(d.bridgePlugin.project)}` : "(this project)";
    if (s.version === null) {
      problems.push(`Running server (pid ${s.pid}) ${label} is executing pruned/old files - a stale process the file deletion did not kill. Quit your MCP client fully (not --resume) and relaunch so a fresh ${latest ?? "latest"} server spawns.`);
    } else if (behindLatest(s.version, latest)) {
      problems.push(`Running server (pid ${s.pid}) ${label} is ${s.version}, not ${latest}. Quit your MCP client and relaunch to swap it.`);
    }
  }
  if (d.targetProjectDir && targetServers.length === 0 && d.runningServers.length > 0) {
    problems.push(`No running server detected for ${d.bridgePlugin ? path.basename(d.bridgePlugin.project) : "this project"} (other ue-mcp servers are running for different projects). If your client says it's connected, it may be a stale process - quit and relaunch.`);
  }

  if (problems.length === 0) {
    lines.push(`  ${GREEN}✓ Everything aligned.${RESET}`);
  } else {
    lines.push(`  ${BOLD}${YELLOW}Findings${RESET}`);
    for (const p of problems) lines.push(`  ${RED}!${RESET} ${p}`);
  }
  lines.push("");
  return lines.join("\n");
}

/** Entry point for `ue-mcp doctor [project.uproject] [--editor <name-or-path>]`. */
export function runDoctorCli(): void {
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(process.argv.slice(2));
  } catch (e) {
    console.error(`  ${RED}${e instanceof EditorFlagError ? e.message : String(e)}${RESET}`);
    process.exit(1);
  }
  const projectArg = target.projectPath ?? target.rest.find((a) => !a.startsWith("-"));
  console.log(formatDoctor(collectDoctor(projectArg)));
}
