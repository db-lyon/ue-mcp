import { EditorBridge } from "../src/bridge.js";
import {
  assertLoopbackHost,
  bridgePortCandidates,
  describeMissingBridge,
  verifyTestProjectTarget,
} from "../scripts/bridge-target.mjs";

let _bridge: EditorBridge | null = null;
let _targetVerified = false;

// 127.0.0.1 rather than "localhost": on hosts where the IPv6 stack wins DNS,
// "localhost" resolves to ::1 and the client waits on an empty socket while the
// bridge owns the IPv4 loopback.
const TEST_BRIDGE_HOST = process.env.UE_MCP_TEST_HOST ?? "127.0.0.1";
const ENV_PORT = Number.parseInt(process.env.UE_MCP_TEST_PORT ?? "", 10);

/**
 * Connect to the bridge for tests/ue_mcp. The bridge binds a per-project port
 * and publishes it to that project's Saved/UE_MCP_Bridge/port.json, so the port
 * is discovered rather than assumed; UE_MCP_TEST_PORT still pins it when set.
 * The connected editor is then challenged for its project directory, and any
 * project other than tests/ue_mcp aborts the run before a single mutation.
 */
export async function getBridge(): Promise<EditorBridge> {
  if (_bridge?.isConnected) return _bridge;

  assertLoopbackHost(TEST_BRIDGE_HOST);
  const { candidates, lockfile } = bridgePortCandidates({
    explicitPort: Number.isInteger(ENV_PORT) ? ENV_PORT : null,
  });

  let lastError: string | null = null;
  for (const candidate of candidates) {
    const bridge = new EditorBridge(TEST_BRIDGE_HOST, candidate.port);
    try {
      await bridge.connect(5000);
    } catch (e: unknown) {
      lastError = e instanceof Error ? e.message : String(e);
      bridge.disconnect();
      continue;
    }
    _bridge = bridge;
    if (!_targetVerified) {
      await verifyTestProjectTarget((method, params) => bridge.call(method, params));
      _targetVerified = true;
    }
    return bridge;
  }

  throw new Error(
    describeMissingBridge({ host: TEST_BRIDGE_HOST, candidates, lockfile, lastError }),
  );
}

export function disconnectBridge(): void {
  _bridge?.disconnect();
  _bridge = null;
}

export interface TestResult {
  method: string;
  ok: boolean;
  ms: number;
  error?: string;
  result?: unknown;
}

export async function callBridge(
  bridge: EditorBridge,
  method: string,
  params: Record<string, unknown> = {},
): Promise<TestResult> {
  const start = performance.now();
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      const result = await bridge.call(method, params);
      const str = JSON.stringify(result);
      if (str.includes("not ready") || str.includes("still initializing")) {
        await new Promise((r) => setTimeout(r, 2000));
        continue;
      }
      return { method, ok: true, ms: Math.round(performance.now() - start), result };
    } catch (e: unknown) {
      const message = e instanceof Error ? e.message : String(e);
      if (message.includes("connection lost") && attempt < 2) {
        await new Promise((r) => setTimeout(r, 2000));
        try { await _bridge?.connect(5000); } catch { /* retry */ }
        continue;
      }
      return { method, ok: false, ms: Math.round(performance.now() - start), error: message };
    }
  }
  return { method, ok: false, ms: Math.round(performance.now() - start), error: "Failed after retries" };
}

export const TEST_PREFIX = "/Game/MCPTest";

/** Safely extract an array field from an untyped bridge result. */
export function resultArray(
  result: unknown,
  ...keys: string[]
): unknown[] | undefined {
  if (Array.isArray(result)) return result;
  if (result && typeof result === "object") {
    const obj = result as Record<string, unknown>;
    for (const key of keys) {
      const val = obj[key];
      if (Array.isArray(val)) return val;
    }
  }
  return undefined;
}

/** Safely extract a field from an untyped bridge result. */
export function resultField(result: unknown, key: string): unknown {
  if (result && typeof result === "object") {
    return (result as Record<string, unknown>)[key];
  }
  return undefined;
}

// ---------------------------------------------------------------------------
// Feature gating - skip tests when required UE plugins are not loaded
// ---------------------------------------------------------------------------

export type Feature =
  | "GameplayAbilities"
  | "Niagara"
  | "PCG"
  | "MetaSounds"
  | "EQS"
  | "StateTree"
  | "SmartObjects";

const FEATURE_CLASS: Record<Feature, string> = {
  GameplayAbilities: "GameplayAbility",
  Niagara: "NiagaraSystem",
  PCG: "PCGComponent",
  MetaSounds: "MetaSoundSource",
  EQS: "EnvironmentQuery",
  StateTree: "StateTree",
  SmartObjects: "SmartObjectDefinition",
};

const _cache = new Map<Feature, boolean>();

export async function checkFeature(
  bridge: EditorBridge,
  feature: Feature,
): Promise<boolean> {
  if (_cache.has(feature)) return _cache.get(feature)!;
  const cls = FEATURE_CLASS[feature];
  try {
    const r = await callBridge(bridge, "execute_python", {
      code: `import unreal\nprint("FEATURE_CHECK:" + str(hasattr(unreal, "${cls}")))`,
    });
    const available =
      r.ok && JSON.stringify(r.result).includes("FEATURE_CHECK:True");
    _cache.set(feature, available);
    return available;
  } catch {
    _cache.set(feature, false);
    return false;
  }
}
