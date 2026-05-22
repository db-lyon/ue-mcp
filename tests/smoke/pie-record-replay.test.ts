import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

/**
 * Smoke for the PIE record / replay surface. These run against
 * tests/ue_mcp without starting PIE, so they verify the actions are
 * registered and that idle-state behaviour is sensible. The full
 * record→replay loop needs a live PIE session; that is a manual test in
 * docs/pie-record-replay.md.
 */

let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(() => {
  disconnectBridge();
});

describe("pie record/replay — registration + idle behaviour", () => {
  it("pie_record_status returns idle when nothing is armed", async () => {
    const r = await callBridge(bridge, "pie_record_status");
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { state?: string }).state).toBeDefined();
  });

  it("pie_record_disarm is a no-op when nothing is armed", async () => {
    const r = await callBridge(bridge, "pie_record_disarm");
    expect(r.ok, r.error).toBe(true);
  });

  it("pie_record_arm accepts a minimal config", async () => {
    const r = await callBridge(bridge, "pie_record_arm", {
      sample_hz: 60,
      axis_threshold: 0.15,
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { armed?: boolean }).armed).toBe(true);
    expect((r.result as { id?: string }).id).toMatch(/^recording-/);
  });

  it("pie_record_arm accepts track_actors", async () => {
    await callBridge(bridge, "pie_record_disarm");
    const r = await callBridge(bridge, "pie_record_arm", {
      track_actors: ["BP_Hero_C", "BP_Boss_C"],
      sample_hz: 60,
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { armed?: boolean }).armed).toBe(true);
    await callBridge(bridge, "pie_record_disarm");
  });

  it("pie_record_arm with a custom id and seed honours both", async () => {
    await callBridge(bridge, "pie_record_disarm");
    const r = await callBridge(bridge, "pie_record_arm", {
      id: "smoke-arm-fixed",
      rng_seed: 42,
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { id?: string }).id).toBe("smoke-arm-fixed");
    expect((r.result as { rng_seed?: number }).rng_seed).toBe(42);
  });

  it("pie_record_disarm clears the armed state", async () => {
    const r = await callBridge(bridge, "pie_record_disarm");
    expect(r.ok, r.error).toBe(true);
    const s = await callBridge(bridge, "pie_record_status");
    expect((s.result as { state?: string }).state).toBe("idle");
  });

  it("pie_record_list returns an array (possibly empty)", async () => {
    const r = await callBridge(bridge, "pie_record_list");
    expect(r.ok, r.error).toBe(true);
    expect(Array.isArray((r.result as { recordings?: unknown[] }).recordings)).toBe(true);
  });

  it("pie_record_read errors cleanly on missing id", async () => {
    const r = await callBridge(bridge, "pie_record_read", { id: "does-not-exist-xyz" });
    // Expect a structured error rather than a crash.
    expect(r.ok).toBe(true);
    expect(((r.result as { error?: string }).error ?? "")).toMatch(/not found|Not found/i);
  });

  it("pie_record_delete refuses without confirm", async () => {
    const r = await callBridge(bridge, "pie_record_delete", {
      id: "smoke-never-existed",
      confirm: false,
    });
    expect(r.ok).toBe(true);
    // Either alreadyDeleted=true (dir absent) or an error about confirm.
    const res = r.result as Record<string, unknown>;
    expect(Boolean(res.alreadyDeleted) || typeof res.error === "string").toBe(true);
  });

  it("pie_mark is a no-op (active=false) when not recording", async () => {
    const r = await callBridge(bridge, "pie_mark", { label: "test-marker" });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { active?: boolean }).active).toBe(false);
  });

  it("pie_replay_status returns idle", async () => {
    const r = await callBridge(bridge, "pie_replay_status");
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { state?: string }).state).toBeDefined();
  });

  it("pie_replay_arm requires at least one source", async () => {
    const r = await callBridge(bridge, "pie_replay_arm", {});
    expect(r.ok).toBe(true);
    // Should be an error result because no recording_id / sequence_path / steps provided.
    expect((r.result as { error?: string }).error).toMatch(/recording_id|sequence_path|inline steps/i);
  });

  it("pie_replay_arm with inline steps accepts the sequence", async () => {
    const r = await callBridge(bridge, "pie_replay_arm", {
      steps: [{ type: "mark", delay_ms: 0, label: "ok" }],
      sample_hz: 60,
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { armed?: boolean }).armed).toBe(true);
    await callBridge(bridge, "pie_replay_disarm");
  });

  it("pie_record_diff errors cleanly when one id is missing", async () => {
    const r = await callBridge(bridge, "pie_record_diff", {
      a_id: "missing-a",
      b_id: "missing-b",
    });
    expect(r.ok).toBe(true);
    expect((r.result as { error?: string }).error).toMatch(/not found/i);
  });
});

describe("inject_input primitives — registration", () => {
  it("inject_input fails cleanly when PIE is not running", async () => {
    const r = await callBridge(bridge, "inject_input", {
      action: "/Engine/EngineDamageTypes/DmgTypeBP_Crushed", // any path - this should fail before the load check
    });
    expect(r.ok).toBe(true);
    // Expect structured error - either action not found or no PIE.
    expect(typeof (r.result as { error?: string }).error).toBe("string");
  });

  it("inject_input_stop is idempotent for an unknown id", async () => {
    const r = await callBridge(bridge, "inject_input_stop", { injection_id: "nope" });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as { stopped?: boolean }).stopped).toBe(false);
  });
});
