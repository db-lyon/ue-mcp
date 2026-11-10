// @live-editor headless
/**
 * asset(semantic_search), asset(semantic_search_status) and
 * asset(semantic_search_index) (#1829): Epic's Semantic Search plugin through
 * its module interface.
 *
 * A query has to be embedded by the configured provider (Epic's internal Loci
 * service, or an OpenAI-compatible endpoint with its key) before the index can
 * be searched. Where none is configured, as on a machine without those
 * credentials, the status says why and every provider-backed action refuses
 * with the same reason by name: that exact refusal is what is asserted. With a
 * provider configured, the index is fed a real asset and the search returns
 * ranked asset rows.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let status: Json = {};

async function fetchStatus(): Promise<Json> {
  const r = await callBridge(bridge, "semantic_search_status", {});
  expect(succeeded(r), JSON.stringify(r.result ?? r.error)).toBe(true);
  return body(r);
}

/** The refusal a provider-backed action gives while the status reports the search unavailable. */
async function expectRefusedAsStatusSays(method: string, params: Json): Promise<void> {
  const r = await callBridge(bridge, method, params);
  const refused = body(r);
  expect(succeeded(r), JSON.stringify(refused)).toBe(false);
  expect(refused.errorCode).toBe(status.errorCode);
  if (status.errorCode === "semantic_search_provider_not_configured") {
    expect(refused.provider).toBe(status.provider);
    expect(String(refused.error)).toContain("Semantic Search has no usable embedding provider");
    if (status.providerNote) expect(String(refused.error)).toContain(String(status.providerNote));
  } else if (status.errorCode === "PLUGIN_NOT_AVAILABLE") {
    expect(String(refused.error)).toContain("Semantic Search plugin is not loaded");
  }
}

beforeAll(async () => {
  bridge = await getBridge();
  status = await fetchStatus();
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("asset semantic search", () => {
  it("semantic_search_status reports the plugin, its provider and its index", async () => {
    const s = await fetchStatus();
    expect(typeof s.available).toBe("boolean");
    if (s.pluginLoaded === false) {
      expect(s.available).toBe(false);
      expect(s.errorCode).toBe("PLUGIN_NOT_AVAILABLE");
      return;
    }
    expect(s.pluginLoaded).toBe(true);
    expect(["OpenAI", "Loci"]).toContain(s.provider);
    expect(typeof s.providerConfigured).toBe("boolean");
    expect(Array.isArray(s.indexedPaths)).toBe(true);
    const stats = s.stats as Json;
    for (const key of ["vectorCount", "keywordCount", "dimension", "supportedAssetCount", "failedCount"]) {
      expect(typeof stats[key], key).toBe("number");
    }
    expect(["PQ", "Flat"]).toContain(stats.indexType);
    if (s.providerConfigured === false) {
      expect(s.available).toBe(false);
      expect(s.errorCode).toBe("semantic_search_provider_not_configured");
      expect(String(s.reason)).toBe(String(s.providerNote));
    }
  }, 60_000);

  it("semantic_search answers ranked assets with a provider, and names the missing provider without one", async () => {
    const query = { query: "a simple cube mesh", limit: 5 };
    if (status.available !== true) {
      await expectRefusedAsStatusSays("semantic_search", query);
      return;
    }
    const r = await callBridge(bridge, "semantic_search", query);
    expect(succeeded(r), JSON.stringify(r.result ?? r.error)).toBe(true);
    const found = body(r);
    expect(found.query).toBe(query.query);
    expect(Number(found.embeddingDimension)).toBeGreaterThan(0);
    const results = found.results as Json[];
    expect(results.length).toBe(found.count);
    expect(results.length).toBeLessThanOrEqual(5);
    for (const row of results) expect(typeof row.score).toBe("number");
  }, 180_000);

  it("semantic_search_index queues assets with a provider, and names the missing provider without one", async () => {
    const params = { assetPaths: ["/Engine/BasicShapes/Cube"] };
    if (status.available !== true) {
      await expectRefusedAsStatusSays("semantic_search_index", params);
      return;
    }
    const r = await callBridge(bridge, "semantic_search_index", params);
    expect(succeeded(r), JSON.stringify(r.result ?? r.error)).toBe(true);
    const queued = body(r);
    expect(queued.started).toBe(true);
    expect(queued.scope).toBe("assets");
    expect(queued.queuedAssetCount).toBe(1);
    expect(queued.rollbackPossible).toBe(false);

    const missing = await callBridge(bridge, "semantic_search_index", { assetPaths: ["/Game/DoesNot/Exist"] });
    expect(succeeded(missing)).toBe(false);
    expect(body(missing).missing).toEqual(["/Game/DoesNot/Exist"]);
  }, 120_000);
});
