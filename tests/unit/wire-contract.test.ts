import { describe, expect, it } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  deriveProjectPort,
  normalizeProjectRoot,
  DEFAULT_BRIDGE_PORT,
  PORT_HASH_VECTORS,
  recordState,
  ROUTING_PARAM_NAMES,
} from "../../src/bridge/wire-contract.js";
import { EDITOR_TARGET_PARAM, MIGRATE_TARGET_PARAM } from "../../src/surface/routing-params.js";
import * as scriptContract from "../../scripts/lib/wire-contract.mjs";
import { renderAll } from "../../scripts/generate-wire-contract.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/**
 * The TS module, the script module and the plugin header are generated from
 * scripts/wire-contract/contract.json. The generated drift gate also catches a
 * stale output; this says which one, without running every generator.
 */
describe("generated wire contract", () => {
  it("every committed output is what the contract generates", () => {
    for (const [rel, text] of Object.entries(renderAll())) {
      const onDisk = fs.readFileSync(path.join(ROOT, rel), "utf8").replace(/\r\n/g, "\n");
      expect(onDisk, `${rel} is stale: run npm run generate`).toBe(text);
    }
  });
});

describe("port derivation", () => {
  it("derives the contract's test vectors, which the plugin's automation test derives too", () => {
    for (const v of PORT_HASH_VECTORS) {
      expect(normalizeProjectRoot(v.root)).toBe(v.normalized);
      expect(deriveProjectPort(v.root)).toBe(v.port);
      expect(scriptContract.deriveProjectPort(v.root)).toBe(v.port);
    }
  });

  it("gives different worktrees different ports", () => {
    expect(deriveProjectPort("C:/work/game-main")).not.toBe(deriveProjectPort("C:/work/game-feature"));
  });

  it("falls back to the legacy fixed port", () => {
    expect(DEFAULT_BRIDGE_PORT).toBe(9877);
  });
});

describe("names the server declares on its own", () => {
  it("lists the per-call editor target params among the routing params", () => {
    expect(ROUTING_PARAM_NAMES).toContain(EDITOR_TARGET_PARAM);
    expect(ROUTING_PARAM_NAMES).toContain(MIGRATE_TARGET_PARAM);
  });
});

describe("recordState", () => {
  it("reads state, and status from a bridge that wrote only that", () => {
    expect(recordState({ state: "listening" })).toBe("listening");
    expect(recordState({ status: "bind-failed" })).toBe("bind-failed");
    expect(recordState({ state: "bind-failed", status: "listening" })).toBe("bind-failed");
    expect(recordState({ state: "something-new" })).toBeNull();
    expect(recordState({})).toBeNull();
  });
});
