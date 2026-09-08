/**
 * Records the #817 plan item 1.10 golden corpus, both halves.
 *
 * The surface a client sees at startup has two legitimate shapes, because
 * Epic-toolset enrichment picks a live editor, then the project cache, then
 * the snapshot baked into the package (`src/index.ts`, `buildSessionLoad`).
 * One baseline therefore cannot tell a regression from a cold start, so the
 * corpus is recorded twice:
 *
 *   - `editor-down.json`, with nothing listening. Needs only Node, so it is
 *     guarded by `tests/unit/golden-editor-down.test.ts` in the unit tests.
 *   - `editor-connected.json`, with a real editor answering. Guarded by
 *     `tests/live/golden-connected.test.ts` in the live tests.
 *
 * One recorder produces both. A second implementation of the capture would
 * make the two baselines prove that the two recorders agree with each other
 * rather than that the server still hands a client the same thing.
 *
 * It drives the shipped entry point over stdio rather than reassembling the
 * surface in-process. A baseline built from a copy of the construction code
 * only ever proves the copy still agrees with itself; going through
 * `initialize` and `tools/list` proves what a client is actually handed.
 *
 * Everything that varies by machine is either pinned or normalized:
 *
 *   - the project lives in a fresh temp directory, and its path is rewritten
 *     to `<PROJECT_DIR>` wherever it appears;
 *   - the repository root is rewritten to `<REPO>`;
 *   - the bridge port is rewritten to `<PORT>` and any ISO timestamp to
 *     `<TIMESTAMP>`, so the connected half does not churn every time the
 *     editor rebinds or restarts;
 *   - the editor-down half pins `UE_MCP_PORT=1`, which guarantees its branch.
 *     Port 1 is privileged on every platform we run on, so nothing can be
 *     listening there and the connection is refused immediately instead of
 *     burning the connect budget;
 *   - every other `UE_MCP_*` variable inherited from the recording shell is
 *     dropped, and the three user-scoped files the server reads (global
 *     config, user state, auth) are redirected into the temp directory so a
 *     developer's own settings never reach the baseline;
 *   - the update check is off, so no network call can change what is recorded;
 *   - the actions enrichment injected from the editor's toolset registry are
 *     sorted, because the registry does not promise one enumeration order from
 *     one editor session to the next. See `canonicalizeActionOrder`. This
 *     normalizes the recording only: what the server advertises is untouched.
 *
 * The connected half deliberately keeps the throwaway project rather than
 * recording against the editor's own project directory. The only difference
 * between the two recordings is then that a live bridge answers: same project,
 * same config, same plugin set, and a temp project that has never been
 * enriched cannot serve a cached catalog. That is what makes the connected
 * baseline evidence about the live-editor path specifically, and it is
 * asserted rather than assumed, because the recorder reads back the
 * enrichment source the server logged.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const here = path.dirname(fileURLToPath(import.meta.url));

/** Repository root, two levels up from `tests/golden/`. */
export const REPO_ROOT = path.resolve(here, "..", "..");

/** Where the committed baselines live. */
export const GOLDEN_EDITOR_DOWN = path.join(here, "editor-down.json");
export const GOLDEN_EDITOR_CONNECTED = path.join(here, "editor-connected.json");

/**
 * Bumped whenever the shape of the snapshot document itself changes (not its
 * contents). A mismatch means the recorder and the file disagree about the
 * format, which is a different problem from a surface regression.
 */
export const GOLDEN_SCHEMA_VERSION = 1;

/** The project name every recording uses, so it is never machine-derived. */
const PROJECT_NAME = "GoldenProject";

/** Which half of the corpus a recording is. */
export type GoldenScenario = "editor-down" | "editor-connected";

/** The baseline file for each half. */
export function goldenBaselinePath(scenario: GoldenScenario): string {
  return scenario === "editor-down" ? GOLDEN_EDITOR_DOWN : GOLDEN_EDITOR_CONNECTED;
}

export interface GoldenTool {
  name: string;
  description: string;
  inputSchema: unknown;
}

export interface GoldenSurface {
  schemaVersion: number;
  scenario: GoldenScenario;
  server: { name: string; version: string };
  instructions: string;
  toolCount: number;
  tools: GoldenTool[];
}

/** Environment variables the recording pins rather than inherits. */
function recordingEnv(sandbox: string, host: string, port: number): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = {};
  for (const [key, value] of Object.entries(process.env)) {
    // Anything UE_MCP_* from the recording shell is a machine setting, and a
    // baseline that carries one is a baseline only that machine can verify.
    if (key.startsWith("UE_MCP_")) continue;
    env[key] = value;
  }
  env.HOME = sandbox;
  env.USERPROFILE = sandbox;
  env.UE_MCP_HOST = host;
  env.UE_MCP_PORT = String(port);
  env.UE_MCP_GLOBAL_CONFIG = path.join(sandbox, "global-config.yml");
  env.UE_MCP_USER_STATE = path.join(sandbox, "state.json");
  env.UE_MCP_AUTH_DIR = path.join(sandbox, "auth");
  env.UE_MCP_DISABLE_UPDATE_CHECK = "1";
  env.UE_MCP_LOG_LEVEL = "error";
  return env;
}

/** A minimal but real `.uproject`, written fresh for every recording. */
function writeFixtureProject(sandbox: string): string {
  const projectDir = path.join(sandbox, PROJECT_NAME);
  fs.mkdirSync(projectDir, { recursive: true });
  const uproject = path.join(projectDir, `${PROJECT_NAME}.uproject`);
  fs.writeFileSync(
    uproject,
    JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6", Category: "", Description: "" }, null, 2),
    "utf-8",
  );
  return uproject;
}

/**
 * Rewrite the two absolute paths that could otherwise be baked in, in every
 * spelling they can appear in: native separators, forward slashes, and the
 * JSON-escaped form. Case-insensitive because Windows reports drive letters
 * both ways.
 */
function normalizePaths(text: string, projectDir: string, sandbox: string): string {
  const substitutions: Array<[string, string]> = [
    [projectDir, "<PROJECT_DIR>"],
    [sandbox, "<SANDBOX>"],
    [REPO_ROOT, "<REPO>"],
  ];
  let out = text;
  for (const [from, to] of substitutions) {
    for (const spelling of [from, from.replace(/\\/g, "/"), from.replace(/\\/g, "\\\\")]) {
      out = out.replace(new RegExp(escapeRegExp(spelling), "gi"), to);
    }
  }
  return out;
}

/**
 * Rewrite the values that move on their own between runs against one
 * unchanged server: the port the editor happened to bind, any wall-clock
 * timestamp, and the server's own version.
 *
 * The version is here because the server reports package.json rather than a
 * literal, which is the fix for it having told every client 0.6.4 for nine
 * minor releases. That made both baselines move on every bump, and the
 * CONNECTED one can only be re-recorded with an editor running, so a release
 * would otherwise have required standing an editor up to satisfy a diff that
 * carries no information about the advertised surface.
 *
 * The port substitution is skipped for a privileged port number, which only
 * the editor-down recording uses. `1` as a bare number occurs throughout a
 * JSON schema document, and rewriting those would corrupt the file to remove
 * a value that cannot vary anyway.
 */
function normalizeVolatileValues(text: string, port: number, host: string): string {
  let out = text.replace(/\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?/g, "<TIMESTAMP>");
  out = out.replace(/("version":\s*)"[^"]+"/g, '$1"<VERSION>"');
  if (port >= 1024) {
    out = out.replace(new RegExp(`${escapeRegExp(host)}:${port}\\b`, "g"), `${host}:<PORT>`);
    out = out.replace(new RegExp(`\\b${port}\\b`, "g"), "<PORT>");
  }
  return out;
}

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

// ── Ordering of enrichment-injected actions ───────────────────────────────────
//
// A category's own actions are declared in TypeScript, so their order is
// authored, reproducible, and meaningful: it is the order the tool description
// introduces them in. Enrichment then appends one action per tool in Unreal's
// toolset registry, each prefixed `epic_`, in whatever order the live editor
// enumerated the registry. That order is not promised to survive an editor
// restart: the same editor over the same catalog can hand back the same tools
// in a different sequence, and the connected baseline then reports a surface
// change on a healthy editor.
//
// The sequence carries no meaning of its own (the enum is the set of accepted
// values, not a reading order), so the recording sorts it, the same way it
// already rewrites paths, ports and timestamps. Only the recorded snapshot is
// normalized; what the server advertises at runtime is untouched.

/** Prefix every enrichment-injected action carries. */
const ENRICHED_ACTION_PREFIX = "epic_";

/** Locale-independent order, so the file does not depend on where it was recorded. */
function byCodeUnit(a: string, b: string): number {
  return a < b ? -1 : a > b ? 1 : 0;
}

/**
 * The action enum of one tool, as a live reference into its input schema, or
 * null when the tool has none. Mutating the returned array edits the schema.
 */
function actionEnumOf(tool: GoldenTool): string[] | null {
  const schema = tool.inputSchema as
    | { properties?: { action?: { enum?: unknown; anyOf?: Array<{ enum?: unknown }> } } }
    | undefined;
  const action = schema?.properties?.action;
  // `action` is advertised as an enum but parsed as a string, so its schema is
  // an anyOf of the enum and a bare string. The enum is still the thing this
  // file canonicalises; it just sits one level down. Reading only the top
  // level silently returned null for every tool, which turned the enriched-
  // action sort into a no-op without failing anything that said so.
  const values = Array.isArray(action?.enum)
    ? action.enum
    : action?.anyOf?.find((branch) => Array.isArray(branch.enum))?.enum;
  if (!Array.isArray(values)) return null;
  if (!values.every((v) => typeof v === "string")) return null;
  return values as string[];
}

/**
 * Where the trailing run of enrichment-injected actions begins, or the length
 * of the list when there is none.
 */
function enrichedActionStart(values: string[]): number {
  let start = values.length;
  while (start > 0 && values[start - 1].startsWith(ENRICHED_ACTION_PREFIX)) start--;
  return start;
}

/**
 * Everything about a recorded surface that would make sorting the injected
 * actions the wrong thing to do, or that sorting would paper over. Returned
 * rather than thrown so both the recorder and the guards can report it.
 *
 * Two shapes are refused:
 *
 *   - an injected action ahead of a category's own actions, which would mean
 *     enrichment no longer appends and the trailing run is not the injected
 *     set. Sorting the tail would then move an authored action.
 *   - the same action advertised twice by one category. Sorting keeps both
 *     copies, but it puts them next to each other where they read as one, and
 *     a category with a duplicated action is a registration bug rather than an
 *     ordering one. The same action name under two different categories is
 *     expected and allowed: unrelated toolsets ship tools of the same short
 *     name, and routing sends them to different categories.
 */
export function actionOrderProblems(surface: GoldenSurface): string[] {
  const problems: string[] = [];
  for (const tool of surface.tools) {
    const values = actionEnumOf(tool);
    if (!values) continue;
    const start = enrichedActionStart(values);
    for (let i = 0; i < start; i++) {
      if (values[i].startsWith(ENRICHED_ACTION_PREFIX)) {
        problems.push(
          `${tool.name}: '${values[i]}' sits at index ${i}, ahead of '${values[start - 1] ?? ""}', ` +
            `so the injected actions are no longer the trailing run of the list`,
        );
      }
    }
    const seen = new Set<string>();
    for (const value of values) {
      if (seen.has(value)) problems.push(`${tool.name}: advertises '${value}' twice`);
      seen.add(value);
    }
  }
  return problems;
}

/** Every tool whose injected actions are not in sorted order. */
export function unsortedEnrichedActions(surface: GoldenSurface): string[] {
  const out: string[] = [];
  for (const tool of surface.tools) {
    const values = actionEnumOf(tool);
    if (!values) continue;
    const tail = values.slice(enrichedActionStart(values));
    for (let i = 1; i < tail.length; i++) {
      if (byCodeUnit(tail[i - 1], tail[i]) > 0) {
        out.push(`${tool.name}: '${tail[i]}' follows '${tail[i - 1]}'`);
        break;
      }
    }
  }
  return out;
}

/**
 * Sort the injected actions of every tool in place, leaving the category's own
 * actions where they were declared. Refuses a surface `actionOrderProblems`
 * has something to say about, because both of those are real defects and
 * sorting is how you stop seeing them.
 */
export function canonicalizeActionOrder(surface: GoldenSurface): void {
  const problems = actionOrderProblems(surface);
  if (problems.length > 0) {
    throw new Error(
      "The recorded surface cannot be normalized, because sorting it would hide this:\n  " +
        problems.join("\n  "),
    );
  }
  for (const tool of surface.tools) {
    const values = actionEnumOf(tool);
    if (!values) continue;
    const start = enrichedActionStart(values);
    const tail = values.slice(start).sort(byCodeUnit);
    for (let i = 0; i < tail.length; i++) values[start + i] = tail[i];
  }
}

/**
 * A copy of a surface with each tool's injected actions in a different order,
 * standing in for the same catalog enumerated by a second editor session.
 * Deterministic for a given seed, so a failure reproduces exactly.
 */
export function permuteEnrichedActions(surface: GoldenSurface, seed = 1): GoldenSurface {
  const copy = JSON.parse(JSON.stringify(surface)) as GoldenSurface;
  let state = seed >>> 0 || 1;
  const nextInt = (bound: number): number => {
    // xorshift32: small, dependency-free, and the same sequence everywhere.
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    state >>>= 0;
    return state % bound;
  };
  for (const tool of copy.tools) {
    const values = actionEnumOf(tool);
    if (!values) continue;
    const start = enrichedActionStart(values);
    for (let i = values.length - 1; i > start; i--) {
      const j = start + nextInt(i - start + 1);
      [values[i], values[j]] = [values[j], values[i]];
    }
  }
  return copy;
}

/** Deep clone with every object key sorted, so map ordering cannot churn the file. */
export function sortKeysDeep(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(sortKeysDeep);
  if (value === null || typeof value !== "object") return value;
  const source = value as Record<string, unknown>;
  const out: Record<string, unknown> = {};
  for (const key of Object.keys(source).sort()) out[key] = sortKeysDeep(source[key]);
  return out;
}

/** The exact bytes the baseline file holds for a given surface. */
export function serializeGolden(surface: GoldenSurface): string {
  return JSON.stringify(sortKeysDeep(surface), null, 2) + "\n";
}

/**
 * One recording, plus the machine-specific directories it used. The caller
 * needs those to assert that none of them survived into the snapshot; a
 * baseline that quietly baked one in would pass on the machine that recorded
 * it and fail everywhere else.
 */
export interface GoldenRecording {
  surface: GoldenSurface;
  sandbox: string;
  projectDir: string;
  repoRoot: string;
  /** Everything the recorded server wrote to stderr. Kept for diagnostics. */
  log: string;
}

export interface CaptureOptions {
  scenario: GoldenScenario;
  /** Bridge port the recorded server is pointed at. */
  port: number;
  host?: string;
}

/** What the startup log says about Epic enrichment, if anything. */
function readEnrichment(log: string): { source: string | null; count: number } {
  const match = /Epic 5\.8 toolsets \(([^)]+)\): surfaced (\d+) tools/.exec(log);
  if (!match) return { source: null, count: 0 };
  return { source: match[1], count: Number(match[2]) };
}

/**
 * Start the shipped server against a throwaway project, and return its
 * `initialize` instructions plus every tool from `tools/list` with its full
 * input schema.
 */
export async function captureSurface(options: CaptureOptions): Promise<GoldenRecording> {
  const host = options.host ?? "127.0.0.1";
  const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-golden-"));
  const uproject = writeFixtureProject(sandbox);
  const projectDir = path.dirname(uproject);

  // The server's own startup narration is the only place the enrichment
  // source is stated, and it is written before the client can ask anything,
  // so it goes to a file the child inherits rather than a stream attached
  // after the process is already running.
  const logPath = path.join(sandbox, "server.log");
  const logFd = fs.openSync(logPath, "a");

  const client = new Client({ name: "ue-mcp-golden-recorder", version: "1.0.0" }, { capabilities: {} });
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
    cwd: REPO_ROOT,
    env: recordingEnv(sandbox, host, options.port) as Record<string, string>,
    stderr: logFd,
  });

  try {
    await client.connect(transport);

    const version = client.getServerVersion();
    const instructions = client.getInstructions() ?? "";
    const listed = await client.listTools();

    const tools: GoldenTool[] = listed.tools
      .map((t) => ({
        name: t.name,
        description: t.description ?? "",
        inputSchema: t.inputSchema as unknown,
      }))
      .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));

    const surface: GoldenSurface = {
      schemaVersion: GOLDEN_SCHEMA_VERSION,
      scenario: options.scenario,
      server: { name: version?.name ?? "", version: version?.version ?? "" },
      instructions,
      toolCount: tools.length,
      tools,
    };

    // Normalize once, over the serialized form, so no field is missed.
    const normalized = normalizeVolatileValues(
      normalizePaths(JSON.stringify(surface), projectDir, sandbox),
      options.port,
      host,
    );

    const log = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";

    // The one ordering in the document that the recorded server did not choose:
    // the actions enrichment took from the editor's toolset registry.
    const recorded = JSON.parse(normalized) as GoldenSurface;
    canonicalizeActionOrder(recorded);

    return {
      surface: recorded,
      sandbox,
      projectDir,
      repoRoot: REPO_ROOT,
      log,
    };
  } finally {
    await client.close().catch(() => undefined);
    // The transport owns the child; close() above kills it. Belt and braces
    // for the case where connect() itself threw.
    await transport.close().catch(() => undefined);
    try { fs.closeSync(logFd); } catch { /* already closed with the child */ }
    fs.rmSync(sandbox, { recursive: true, force: true });
  }
}

/** The cold half: a real server process, and no editor listening anywhere. */
export async function captureEditorDownSurface(): Promise<GoldenRecording> {
  return captureSurface({ scenario: "editor-down", port: 1 });
}

/**
 * The connected half. `port` is the port a verified editor is answering on;
 * the live tests discovers it and refuses anything but the test project before
 * this is called.
 */
export async function captureEditorConnectedSurface(
  port: number,
  host = "127.0.0.1",
): Promise<GoldenRecording> {
  return captureSurface({ scenario: "editor-connected", port, host });
}

/**
 * Read a committed baseline. Returns null when it has never been recorded.
 *
 * CRLF is folded to LF on the way in. `.gitattributes` already pins `*.json`
 * to `eol=lf`, so this only covers a checkout that arrived some other way; a
 * line-ending difference is not a surface regression and should not be
 * reported as one.
 */
export function readGoldenBaseline(scenario: GoldenScenario = "editor-down"): string | null {
  const file = goldenBaselinePath(scenario);
  if (!fs.existsSync(file)) return null;
  return fs.readFileSync(file, "utf-8").replace(/\r\n/g, "\n");
}

/** Write a baseline. Used by `npm run golden:record`. */
export function writeGoldenBaseline(contents: string, scenario: GoldenScenario = "editor-down"): void {
  const file = goldenBaselinePath(scenario);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, contents, "utf-8");
}

/** Point at the first differing line, so a large diff names its own cause. */
export function firstDifference(expected: string, actual: string): string {
  const a = expected.split("\n");
  const b = actual.split("\n");
  const clip = (line: string | undefined) =>
    line === undefined ? "<end of file>" : line.length > 200 ? `${line.slice(0, 200)}...` : line;
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if (a[i] === b[i]) continue;
    return `First difference at line ${i + 1}:\n  baseline: ${clip(a[i])}\n  recorded: ${clip(b[i])}`;
  }
  return `Files differ in length only: baseline ${a.length} lines, recorded ${b.length} lines.`;
}
