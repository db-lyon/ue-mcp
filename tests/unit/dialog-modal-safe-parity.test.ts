/**
 * The TS modal-safe list must match the plugin's.
 *
 * A method the plugin answers during a modal but TS does not know about would
 * clear the dialog latch on success, disarming the gate. That is exactly how
 * list_dialogs used to disarm it, so the set is pinned to what the plugin
 * declares: each handler's action facts, as tests/golden/handler-specs.json
 * records them from get_bridge_capabilities.actionFacts.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { isModalSafeMethod } from "../../src/editor/dialog-guard.js";
import type { ActionFacts } from "../../src/surface/handler-spec.js";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const RECORDING = JSON.parse(fs.readFileSync(path.join(REPO_ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  actionFacts: Record<string, ActionFacts>;
};
const facts = Object.entries(RECORDING.actionFacts);

describe("modal-safe parity with the plugin", () => {
  it("knows every method the plugin exempts from its dialog gate", () => {
    const exempt = facts.filter(([, f]) => f.modalSafe).map(([m]) => m);
    expect(exempt.length).toBeGreaterThan(0);
    for (const method of exempt) {
      expect(isModalSafeMethod(method), `${method} is modal-safe in C++ but not in TS`).toBe(true);
    }
  });

  it("knows every method the plugin answers off the game thread, before the gate", () => {
    const early = facts.filter(([, f]) => f.servedOffGameThread).map(([m]) => m);
    expect(early.length).toBeGreaterThan(0);
    for (const method of early) {
      expect(isModalSafeMethod(method), `${method} is served pre-gate in C++ but not in TS`).toBe(true);
    }
  });

  it("does not treat an ordinary handler as modal-safe", () => {
    for (const method of ["list_assets", "get_world_outliner", "spawn_actor", "compile_blueprint"]) {
      expect(isModalSafeMethod(method)).toBe(false);
    }
  });
});
