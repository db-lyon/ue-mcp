/**
 * The TS modal-safe list must match the plugin's.
 *
 * A method the plugin answers during a modal but TS does not know about would
 * clear the dialog latch on success, disarming the gate. That is exactly how
 * list_dialogs used to disarm it, so the set is pinned to the C++ rather than
 * maintained by memory in two places.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { isModalSafeMethod, DialogGuard } from "../../src/dialog-guard.js";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const BRIDGE = path.join(
  REPO_ROOT,
  "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/BridgeServer.cpp",
);

const source = fs.readFileSync(BRIDGE, "utf8");

/** The set the plugin exempts from its own dialog gate. */
function modalSafeSet(): string[] {
  const block = source.match(/static const TSet<FString> ModalSafeMethods\s*=\s*\{([\s\S]*?)\};/);
  expect(block, "ModalSafeMethods not found in BridgeServer.cpp").toBeTruthy();
  return [...block![1].matchAll(/TEXT\("([a-z_]+)"\)/g)].map((m) => m[1]);
}

/** Methods answered before the gate is reached, so also unaffected by a modal. */
function servedBeforeGate(): string[] {
  const gateAt = source.indexOf("THE DIALOG GATE");
  expect(gateAt, "dialog gate marker not found").toBeGreaterThan(-1);
  const before = source.slice(0, gateAt);
  return [...before.matchAll(/Method == TEXT\("([a-z_]+)"\)/g)].map((m) => m[1]);
}

describe("modal-safe parity with the plugin", () => {
  it("knows every method the plugin exempts from its dialog gate", () => {
    const exempt = modalSafeSet();
    expect(exempt.length).toBeGreaterThan(0);
    for (const method of exempt) {
      expect(isModalSafeMethod(method), `${method} is modal-safe in C++ but not in TS`).toBe(true);
    }
  });

  it("knows every method the plugin answers before the gate", () => {
    const early = servedBeforeGate();
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
