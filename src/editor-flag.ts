/**
 * `--editor <name-or-path>` for the one-shot CLI subcommands (#817, plan 6.2).
 *
 * `deploy`, `build`, `init`, `plugin`, `context`, `doctor`, `resolve`,
 * `update`, `feedback` and `uninstall-hooks` each run in their own process
 * with no session registry: the registry only exists inside a running server.
 * So a session NAME has to be resolved from the same place the server got its
 * sessions from, which is the argv recorded in the MCP client config, and the
 * naming rule has to match SessionRegistry's exactly or `--editor beta` would
 * mean one project to the server and another to the CLI.
 *
 * Resolution order is name first, path second, as the plan specifies. A name
 * is the more specific claim: a bare project name that also happens to be a
 * directory in cwd should address the registered editor, not the directory.
 */
import * as fs from "node:fs";
import * as path from "node:path";

export const EDITOR_FLAG = "--editor";

export interface EditorFlagParse {
  /** The value passed to --editor, or undefined when the flag was absent. */
  editor?: string;
  /** argv with the flag and its value removed, so existing parsing is untouched. */
  rest: string[];
}

/**
 * Pull `--editor <value>` (or `--editor=<value>`) out of an argument list.
 *
 * Every other argument is returned untouched and in order, including other
 * flags: this must not become a general-purpose argument parser, because
 * several of these subcommands forward the remainder verbatim.
 */
export function parseEditorFlag(argv: string[]): EditorFlagParse {
  const rest: string[] = [];
  let editor: string | undefined;
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === EDITOR_FLAG) {
      const value = argv[i + 1];
      if (value !== undefined && !value.startsWith("-")) {
        editor = value;
        i++;
      }
      continue;
    }
    if (arg.startsWith(`${EDITOR_FLAG}=`)) {
      editor = arg.slice(EDITOR_FLAG.length + 1);
      continue;
    }
    rest.push(arg);
  }
  return { editor, rest };
}

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
  if (resolved.toLowerCase().endsWith(".uproject")) return path.basename(resolved, path.extname(resolved));
  const found = listUprojects(resolved)[0];
  return found ? path.basename(found, ".uproject") : path.basename(resolved);
}

function listUprojects(dir: string): string[] {
  try {
    if (!fs.statSync(dir).isDirectory()) return [];
    return fs
      .readdirSync(dir)
      .filter((f) => f.toLowerCase().endsWith(".uproject"))
      .map((f) => path.join(dir, f));
  } catch {
    return [];
  }
}

/**
 * Every editor the MCP client configs on this machine say the server drives.
 *
 * Read-only and best-effort: a malformed or absent config contributes nothing
 * rather than failing the subcommand that only wanted to deploy to cwd.
 */
export function discoverConfiguredEditors(cwd: string = process.cwd()): ConfiguredEditor[] {
  const out: ConfiguredEditor[] = [];
  const seen = new Set<string>();
  for (const { file, positionals } of readServerInvocations(cwd)) {
    const names = namesForProjects(positionals);
    positionals.forEach((projectPath, i) => {
      const key = path.resolve(projectPath).toLowerCase();
      if (seen.has(key)) return;
      seen.add(key);
      out.push({ name: names[i], projectPath: path.resolve(projectPath), source: file });
    });
  }
  return out;
}

interface ServerInvocation {
  file: string;
  positionals: string[];
}

/** The config files that can name a ue-mcp server invocation, nearest first. */
function candidateConfigFiles(cwd: string): string[] {
  const home = process.env.HOME || process.env.USERPROFILE || "";
  const appData = process.env.APPDATA || (home ? path.join(home, "AppData", "Roaming") : "");
  const files = [
    path.join(cwd, ".mcp.json"),
    path.join(cwd, ".cursor", "mcp.json"),
  ];
  if (home) {
    files.push(path.join(home, ".claude", ".mcp.json"));
    files.push(path.join(home, ".codex", "config.toml"));
  }
  if (appData) files.push(path.join(appData, "Claude", "claude_desktop_config.json"));
  return files;
}

function readServerInvocations(cwd: string): ServerInvocation[] {
  const out: ServerInvocation[] = [];
  for (const file of candidateConfigFiles(cwd)) {
    if (!fs.existsSync(file)) continue;
    let raw: string;
    try {
      raw = fs.readFileSync(file, "utf-8");
    } catch {
      continue;
    }
    const positionals = file.toLowerCase().endsWith(".toml")
      ? positionalsFromToml(raw)
      : positionalsFromJson(raw);
    if (positionals.length > 0) out.push({ file, positionals });
  }
  return out;
}

function positionalsFromJson(raw: string): string[] {
  try {
    const parsed = JSON.parse(raw) as { mcpServers?: Record<string, { args?: unknown }> };
    const args = parsed?.mcpServers?.["ue-mcp"]?.args;
    return positionalsFromArgs(Array.isArray(args) ? args : []);
  } catch {
    return [];
  }
}

function positionalsFromToml(raw: string): string[] {
  // Only the [mcp_servers.ue-mcp] table's `args` line is of interest, and it is
  // emitted by upsertCodexMcpServer as a single-line array of quoted strings.
  const lines = raw.split(/\r?\n/);
  let inTable = false;
  for (const line of lines) {
    const trimmed = line.trim();
    if (trimmed.startsWith("[")) {
      inTable = trimmed === "[mcp_servers.ue-mcp]" || trimmed === '[mcp_servers."ue-mcp"]';
      continue;
    }
    if (!inTable || !trimmed.startsWith("args")) continue;
    const open = trimmed.indexOf("[");
    const close = trimmed.lastIndexOf("]");
    if (open < 0 || close <= open) continue;
    const items = [...trimmed.slice(open + 1, close).matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) =>
      m[1].replace(/\\(.)/g, "$1"),
    );
    return positionalsFromArgs(items);
  }
  return [];
}

/**
 * Drop the launcher words and flags, leaving the project positionals.
 * `npx ue-mcp <project> [...]` and `node .../dist/index.js <project> [...]`
 * are the two shapes init writes and doctor recognises.
 */
function positionalsFromArgs(args: unknown[]): string[] {
  const out: string[] = [];
  for (const raw of args) {
    if (typeof raw !== "string") continue;
    const arg = raw.trim();
    if (!arg || arg.startsWith("-")) continue;
    const lower = arg.toLowerCase();
    if (lower === "ue-mcp" || lower === "npx" || lower === "node") continue;
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
    if (asPath.toLowerCase().endsWith(".uproject")) return asPath;
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
  const dirOf = (p: string) =>
    (p.toLowerCase().endsWith(".uproject") ? path.dirname(p) : p).replace(/[\\/]+$/, "").toLowerCase();
  return dirOf(path.resolve(a)) === dirOf(path.resolve(b));
}

/**
 * Read `--editor` out of argv and turn it into a project path.
 *
 * Returns the remaining argv either way, so a subcommand keeps parsing exactly
 * what it parsed before when the flag is absent.
 */
export function takeEditorTarget(
  argv: string[],
  cwd: string = process.cwd(),
): { projectPath?: string; rest: string[] } {
  const { editor, rest } = parseEditorFlag(argv);
  if (editor === undefined) return { rest };
  return { projectPath: resolveEditorFlag(editor, cwd), rest };
}
