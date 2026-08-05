/**
 * Records the editor-down half of the #817 plan item 1.10 golden corpus.
 *
 * The surface a client sees at startup has two legitimate shapes, because
 * Epic-toolset enrichment picks a live editor, then the project cache, then
 * the snapshot baked into the package (`src/index.ts`, `buildSessionLoad`).
 * One baseline therefore cannot tell a regression from a cold start, so the
 * corpus is recorded twice. This module records the cold half: a real server
 * process, a real project, and no editor listening anywhere.
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
 *   - `UE_MCP_PORT=1` guarantees the editor-down branch. Port 1 is privileged
 *     on every platform we run on, so nothing can be listening there, and the
 *     connection is refused immediately instead of burning the connect budget;
 *   - every other `UE_MCP_*` variable inherited from the recording shell is
 *     dropped, and the three user-scoped files the server reads (global
 *     config, user state, auth) are redirected into the temp directory so a
 *     developer's own settings never reach the baseline;
 *   - the update check is off, so no network call can change what is recorded.
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

/** Where the committed baseline lives. */
export const GOLDEN_EDITOR_DOWN = path.join(here, "editor-down.json");

/**
 * Bumped whenever the shape of the snapshot document itself changes (not its
 * contents). A mismatch means the recorder and the file disagree about the
 * format, which is a different problem from a surface regression.
 */
export const GOLDEN_SCHEMA_VERSION = 1;

/** The project name every recording uses, so it is never machine-derived. */
const PROJECT_NAME = "GoldenProject";

export interface GoldenTool {
  name: string;
  description: string;
  inputSchema: unknown;
}

export interface GoldenSurface {
  schemaVersion: number;
  scenario: "editor-down";
  server: { name: string; version: string };
  instructions: string;
  toolCount: number;
  tools: GoldenTool[];
}

/** Environment variables the recording pins rather than inherits. */
function recordingEnv(sandbox: string): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = {};
  for (const [key, value] of Object.entries(process.env)) {
    // Anything UE_MCP_* from the recording shell is a machine setting, and a
    // baseline that carries one is a baseline only that machine can verify.
    if (key.startsWith("UE_MCP_")) continue;
    env[key] = value;
  }
  env.HOME = sandbox;
  env.USERPROFILE = sandbox;
  env.UE_MCP_HOST = "127.0.0.1";
  env.UE_MCP_PORT = "1";
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

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
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
}

/**
 * Start the shipped server against a throwaway project with no editor
 * running, and return its `initialize` instructions plus every tool from
 * `tools/list` with its full input schema.
 */
export async function captureEditorDownSurface(): Promise<GoldenRecording> {
  const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-golden-"));
  const uproject = writeFixtureProject(sandbox);
  const projectDir = path.dirname(uproject);

  const client = new Client({ name: "ue-mcp-golden-recorder", version: "1.0.0" }, { capabilities: {} });
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
    cwd: REPO_ROOT,
    env: recordingEnv(sandbox) as Record<string, string>,
    stderr: "ignore",
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
      scenario: "editor-down",
      server: { name: version?.name ?? "", version: version?.version ?? "" },
      instructions,
      toolCount: tools.length,
      tools,
    };

    // Normalize once, over the serialized form, so no field is missed.
    return {
      surface: JSON.parse(normalizePaths(JSON.stringify(surface), projectDir, sandbox)) as GoldenSurface,
      sandbox,
      projectDir,
      repoRoot: REPO_ROOT,
    };
  } finally {
    await client.close().catch(() => undefined);
    // The transport owns the child; close() above kills it. Belt and braces
    // for the case where connect() itself threw.
    await transport.close().catch(() => undefined);
    fs.rmSync(sandbox, { recursive: true, force: true });
  }
}

/**
 * Read the committed baseline. Returns null when it has never been recorded.
 *
 * CRLF is folded to LF on the way in. `.gitattributes` already pins `*.json`
 * to `eol=lf`, so this only covers a checkout that arrived some other way; a
 * line-ending difference is not a surface regression and should not be
 * reported as one.
 */
export function readGoldenBaseline(): string | null {
  if (!fs.existsSync(GOLDEN_EDITOR_DOWN)) return null;
  return fs.readFileSync(GOLDEN_EDITOR_DOWN, "utf-8").replace(/\r\n/g, "\n");
}

/** Write the baseline. Used by `npm run golden:record`. */
export function writeGoldenBaseline(contents: string): void {
  fs.mkdirSync(path.dirname(GOLDEN_EDITOR_DOWN), { recursive: true });
  fs.writeFileSync(GOLDEN_EDITOR_DOWN, contents, "utf-8");
}
