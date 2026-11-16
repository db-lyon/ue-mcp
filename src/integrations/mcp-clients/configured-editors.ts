// @session per-session: Turns an editor name into the project the client configs say the server drives.
/**
 * The editors the MCP client configs on this machine start the server with,
 * and the `--editor <name-or-path>` resolution over them (#817, plan 6.2).
 *
 * One-shot CLI commands and hooks run in their own process with no session
 * registry: the registry only exists inside a running server. So a session
 * NAME has to be resolved from the same place the server got its sessions
 * from, which is the argv recorded in the client config, and the naming rule
 * has to match SessionRegistry's exactly or `--editor beta` would mean one
 * project to the server and another to the CLI.
 *
 * Resolution order is name first, path second. A name is the more specific
 * claim: a bare project name that also happens to be a directory in cwd
 * should address the registered editor, not the directory.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { isUProjectPath, projectDirOf } from "../../config/uproject-path.js";
import { clientsNearestFirst, readUeMcpEntry } from "./mcp-client-config.js";

export const EDITOR_FLAG = "--editor";

/** One editor the server is configured to drive. */
export interface ConfiguredEditor {
  /** The handle the running server answers to, from the same rule it uses. */
  name: string;
  /** Absolute .uproject path, or the directory argument when no file resolves. */
  projectPath: string;
  /** Which client config this came from, for error messages. */
  source: string;
}

/**
 * The session names a server started with these positionals would assign.
 *
 * Mirrors SessionRegistry: the project's base name, de-duplicated with a
 * numeric suffix in registration order. Kept here rather than imported so the
 * CLI does not construct a ProjectContext (and therefore does not need the
 * project to be loadable) just to learn what it would be called.
 */
export function namesForProjects(projectPaths: string[]): string[] {
  const taken = new Set<string>();
  const names: string[] = [];
  for (const p of projectPaths) {
    const base = projectBaseName(p);
    let name = base;
    for (let i = 2; taken.has(name.toLowerCase()); i++) name = `${base}-${i}`;
    taken.add(name.toLowerCase());
    names.push(name);
  }
  return names;
}

function projectBaseName(projectPath: string): string {
  const resolved = path.resolve(projectPath);
  if (isUProjectPath(resolved)) return path.basename(resolved, path.extname(resolved));
  const found = listUprojects(resolved)[0];
  return found ? path.basename(found, path.extname(found)) : path.basename(resolved);
}

function listUprojects(dir: string): string[] {
  try {
    if (!fs.statSync(dir).isDirectory()) return [];
    return fs
      .readdirSync(dir)
      .filter(isUProjectPath)
      .map((f) => path.join(dir, f));
  } catch {
    return [];
  }
}

/**
 * Every editor the MCP client configs on this machine say the server drives,
 * reading the configs nearest first.
 *
 * Read-only and best-effort: a malformed or absent config contributes nothing
 * rather than failing the subcommand that only wanted to deploy to cwd.
 */
export function discoverConfiguredEditors(cwd: string = process.cwd()): ConfiguredEditor[] {
  const out: ConfiguredEditor[] = [];
  const seen = new Set<string>();
  for (const client of clientsNearestFirst(cwd)) {
    if (!client.detected) continue;
    const positionals = positionalsFromArgs(readUeMcpEntry(client).args);
    const names = namesForProjects(positionals);
    positionals.forEach((projectPath, i) => {
      const key = path.resolve(projectPath).toLowerCase();
      if (seen.has(key)) return;
      seen.add(key);
      out.push({ name: names[i], projectPath: path.resolve(projectPath), source: client.configPath });
    });
  }
  return out;
}

/**
 * Drop the launcher words and flags, leaving the project positionals.
 * `npx ue-mcp <project> [...]` and `node .../dist/index.js <project> [...]`
 * are the two shapes init writes and doctor recognises.
 */
function positionalsFromArgs(args: readonly string[]): string[] {
  const out: string[] = [];
  for (const raw of args) {
    const arg = raw.trim();
    if (!arg || arg.startsWith("-")) continue;
    const lower = arg.toLowerCase();
    if (lower === "ue-mcp" || lower.startsWith("ue-mcp@") || lower === "npx" || lower === "node") continue;
    if (lower.endsWith("index.js") || lower.endsWith(".mjs") || lower.endsWith(".cjs")) continue;
    out.push(arg);
  }
  return out;
}

export class EditorFlagError extends Error {}

/**
 * Turn `--editor <value>` into a project path.
 *
 * Name first: a registered session name, then its project name. Path second: a
 * .uproject file, or a directory holding exactly one. Anything else is an
 * error naming what IS registered, because silently falling back to cwd would
 * run a deploy or a build against the wrong project.
 */
export function resolveEditorFlag(editor: string, cwd: string = process.cwd()): string {
  const needle = editor.trim();
  if (!needle) throw new EditorFlagError(`${EDITOR_FLAG} needs a session name or a project path.`);

  const configured = discoverConfiguredEditors(cwd);
  const lower = needle.toLowerCase();
  const byName = configured.find((e) => e.name.toLowerCase() === lower);
  if (byName) return byName.projectPath;
  const byProjectName = configured.find(
    (e) => projectBaseName(e.projectPath).toLowerCase() === lower,
  );
  if (byProjectName) return byProjectName.projectPath;

  const asPath = path.resolve(cwd, needle);
  if (fs.existsSync(asPath)) {
    if (isUProjectPath(asPath)) return asPath;
    const found = listUprojects(asPath);
    if (found.length === 1) return found[0];
    if (found.length > 1) {
      throw new EditorFlagError(
        `${EDITOR_FLAG} '${needle}' holds ${found.length} .uproject files; name one of them instead.`,
      );
    }
  }
  // A path that resolved to a registered project counts as that editor even
  // when the config recorded it in the other form (file vs directory).
  const byResolvedPath = configured.find(
    (e) => sameProject(e.projectPath, asPath),
  );
  if (byResolvedPath) return byResolvedPath.projectPath;

  const known = configured.length > 0
    ? ` Registered editors: ${configured.map((e) => e.name).join(", ")}.`
    : " No editors are registered in any MCP client config on this machine.";
  throw new EditorFlagError(
    `${EDITOR_FLAG} '${needle}' is neither a registered editor nor a project path.${known}`,
  );
}

function sameProject(a: string, b: string): boolean {
  const dirOf = (p: string) => projectDirOf(p).replace(/[\\/]+$/, "").toLowerCase();
  return dirOf(a) === dirOf(b);
}
