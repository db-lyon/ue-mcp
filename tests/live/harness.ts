/**
 * Shared plumbing for the live tests (#817, plan items 1.10 and 7.3).
 *
 * The live tests is the only suite that talks to a real Unreal editor. Two rules
 * follow from that and both are enforced here rather than left to each test:
 *
 *  1. **It only ever drives `tests/ue_mcp`.** The editor is asked which project
 *     it has open before anything else is sent, and any other answer aborts the
 *     run. This is the same guard `scripts/bridge-target.mjs` applies to the
 *     smoke harness, for the same reason: these runs execute real calls against
 *     a real editor.
 *  2. **No editor is started or stopped.** The suite attaches to an editor that
 *     is already up and leaves it exactly as it found it. Every case that would
 *     need a launch or a shutdown is asserted read-only, or against a fabricated
 *     bridge, and says so where it is written.
 *
 * When no editor is running, discovery fails with the ports it tried and the
 * lockfile it read, rather than hanging or quietly passing.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { EditorBridge } from "../../src/bridge.js";
import {
  assertLiveTestProjectDir,
  assertLoopbackHost,
  bridgePortCandidates,
  describeMissingBridge,
  extractReportedProjectDir,
  liveTestProjectDirs,
  PROJECT_IDENTITY_PYTHON,
} from "../../scripts/bridge-target.mjs";

/** The editor this suite is attached to. */
export interface LiveTarget {
  /** The verified project root. Always a `tests/ue_mcp` of this repository. */
  projectDir: string;
  uproject: string;
  host: string;
  port: number;
  /** Raw `get_bridge_capabilities` payload from the running binary. */
  capabilities: LiveCapabilities;
}

/** What the running bridge reports about itself. */
export interface LiveCapabilities {
  protocolVersion: number;
  handlerApiVersion?: number;
  builtAt?: string;
  engineVersion?: string;
  projectName?: string;
  instanceId?: string;
  pid?: number;
  port?: number;
  startedAt?: string;
  features?: string[];
  actions?: string[];
  actionCount?: number;
  /** True when the editor was launched with the parameter echo switched on. */
  paramEcho?: boolean;
}

const HOST = process.env.UE_MCP_LIVE_HOST ?? process.env.UE_MCP_TEST_HOST ?? "127.0.0.1";

/** Port hint published by `scripts/live-tests.mjs` after its own preflight. */
const PORT_HINT = Number.parseInt(process.env.UE_MCP_LIVE_PORT ?? "", 10);

let discovered: LiveTarget | null = null;
let discovering: Promise<LiveTarget> | null = null;
const bridges: EditorBridge[] = [];

/**
 * Find the editor, prove it has the test project open, and cache the answer.
 *
 * Never returns a target it could not verify: a bridge that answers on a
 * candidate port with some other project open aborts the run outright, since a
 * live tests that quietly retargets is exactly the failure the guard exists for.
 */
export async function liveTarget(): Promise<LiveTarget> {
  if (discovered) return discovered;
  if (discovering) return discovering;
  discovering = discover().finally(() => { discovering = null; });
  return discovering;
}

async function discover(): Promise<LiveTarget> {
  assertLoopbackHost(HOST);
  const allowed = liveTestProjectDirs();
  if (allowed.length === 0) {
    throw new Error(
      "No tests/ue_mcp project found in this checkout. The live tests drives that project and nothing else.",
    );
  }

  let lastError: string | null = null;
  let lastCandidates: Array<{ port: number; source: string }> = [];
  let lastLockfile: unknown = null;

  for (const projectDir of allowed) {
    const { candidates, lockfile } = bridgePortCandidates({
      projectDir,
      explicitPort: Number.isInteger(PORT_HINT) && PORT_HINT > 0 ? PORT_HINT : null,
    });
    lastCandidates = candidates;
    lastLockfile = lockfile;

    for (const candidate of candidates) {
      const bridge = new EditorBridge(HOST, candidate.port);
      try {
        await bridge.connect(5000);
      } catch (e) {
        lastError = e instanceof Error ? e.message : String(e);
        bridge.disconnect();
        continue;
      }

      try {
        // The identity challenge, before a single other call. Python is the
        // only thing that can answer "which project is this" from inside the
        // editor without trusting a file this harness also reads.
        const reported = extractReportedProjectDir(
          await bridge.call("execute_python", { code: PROJECT_IDENTITY_PYTHON }, 60_000),
        );
        assertLiveTestProjectDir(reported, allowed);
        const capabilities = (await bridge.call("get_bridge_capabilities", {}, 15_000)) as LiveCapabilities;

        discovered = {
          projectDir: reported as string,
          uproject: path.join(reported as string, "ue_mcp.uproject"),
          host: HOST,
          port: candidate.port,
          capabilities,
        };
        bridges.push(bridge);
        return discovered;
      } catch (e) {
        bridge.disconnect();
        throw e;
      }
    }
  }

  throw new Error(
    describeMissingBridge({
      host: HOST,
      candidates: lastCandidates,
      lockfile: lastLockfile as never,
      lastError,
    }) +
      "\n\nThe live tests needs an editor already running on tests/ue_mcp. Start one, then re-run" +
      "\n    npm run test:live",
  );
}

/**
 * A connected bridge to the live editor. Each call returns a new connection,
 * so a test that needs to observe connect behaviour is not handed a socket
 * some earlier test opened. All of them are closed by `closeLiveBridges()`.
 */
export async function liveBridge(timeoutMs = 5000): Promise<EditorBridge> {
  const target = await liveTarget();
  const bridge = new EditorBridge(target.host, target.port);
  bridge.setProjectContext(target.uproject);
  await bridge.connect(timeoutMs);
  bridges.push(bridge);
  return bridge;
}

/** Drop every connection this suite opened. Never touches the editor process. */
export function closeLiveBridges(): void {
  for (const bridge of bridges.splice(0)) bridge.disconnect();
}

// ---------------------------------------------------------------------------
// Parameter echo (plan item 0.8) - the oracle for leak assertions
// ---------------------------------------------------------------------------

/**
 * Why the leak assertions cannot run, or null when they can.
 *
 * The echo is a test facility that can only be armed when the editor process
 * starts, deliberately: there is no way to switch it on over the socket. So a
 * suite that cannot start editors either finds it on or reports why it skipped,
 * which is what plan item 3.3 asks for.
 */
export async function paramEchoUnavailable(): Promise<string | null> {
  const { capabilities } = await liveTarget();
  if (!capabilities.features?.includes("param-echo")) {
    return (
      "the running bridge does not have the parameter echo compiled in " +
      `(built ${capabilities.builtAt ?? "unknown"}); rebuild the plugin to include it`
    );
  }
  if (!capabilities.paramEcho) {
    return (
      "the parameter echo is compiled in but not armed. Relaunch the editor with " +
      "UE_MCP_PARAM_ECHO=1 in its environment (or -MCPParamEcho on its command line) " +
      "to include the leak assertions"
    );
  }
  return null;
}

export interface ParamEchoEntry {
  method: string;
  paramNames: string[];
}

/** Forget every dispatch the bridge has recorded so far. */
export async function clearParamEcho(bridge: EditorBridge): Promise<void> {
  await bridge.call("clear_param_echo", {}, 15_000);
}

/** Every dispatch the bridge has seen since the last clear, oldest first. */
export async function readParamEcho(bridge: EditorBridge): Promise<ParamEchoEntry[]> {
  const payload = (await bridge.call("get_param_echo", {}, 15_000)) as {
    entries?: Array<{ method?: string; params?: string[] }>;
  };
  return (payload.entries ?? []).map((e) => ({
    method: String(e.method ?? ""),
    paramNames: [...(e.params ?? [])],
  }));
}

/** The parameter names one method arrived with, across every recorded dispatch. */
export async function paramNamesFor(bridge: EditorBridge, method: string): Promise<string[]> {
  const entries = await readParamEcho(bridge);
  const names = new Set<string>();
  for (const entry of entries) {
    if (entry.method !== method) continue;
    for (const name of entry.paramNames) names.add(name);
  }
  return [...names];
}

// ---------------------------------------------------------------------------
// Fabricated projects
// ---------------------------------------------------------------------------

/**
 * A throwaway project directory, for the cases that need a SECOND session.
 *
 * One machine has one editor, and this suite does not start another. A session
 * whose editor is not running is still a registered session (plan item 4.4),
 * which is exactly what the multi-editor cases need: the count is what arms
 * gating, addressing and the union refusal, and the live editor is one of the
 * two. Nothing here is ever connected to.
 */
export interface TempProject {
  dir: string;
  uproject: string;
  cleanup: () => void;
}

export function makeTempProject(name: string, config?: string): TempProject {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-live-"));
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(
    uproject,
    JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6", Category: "", Description: "" }, null, 2),
    "utf-8",
  );
  if (config !== undefined) fs.writeFileSync(path.join(dir, "ue-mcp.yml"), config, "utf-8");
  return {
    dir,
    uproject,
    cleanup: () => fs.rmSync(root, { recursive: true, force: true }),
  };
}
