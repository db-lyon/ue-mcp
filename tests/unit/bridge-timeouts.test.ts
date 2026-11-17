/**
 * The client's timeout budget must not be shorter than the server's (#989).
 *
 * The editor waits as long as an action declares at its C++ registration, and
 * the client used to give up at a flat 30 seconds: the editor finished the work,
 * the client reported a failure, and a naive retry applied the mutation twice.
 * The declared timeouts are published in get_bridge_capabilities.actionFacts and
 * recorded into the generated ACTION_FACTS, so the table is read, not mirrored;
 * tests/live/handler-specs.test.ts holds the recording to a running editor.
 */
import { describe, expect, it } from "vitest";
import {
  DEFAULT_BRIDGE_TIMEOUT_MS,
  MAX_BRIDGE_TIMEOUT_MS,
  REGISTERED_HANDLER_TIMEOUT_SECONDS,
  SERVER_TIMEOUT_MARGIN_MS,
  TIMEOUT_ENV_VAR,
  environmentTimeoutMs,
  registeredTimeoutMs,
  resolveBridgeTimeout,
} from "../../src/bridge/bridge-timeouts.js";
import { DEFAULT_HANDLER_TIMEOUT_SECONDS, MAX_REQUEST_TIMEOUT_SECONDS } from "../../src/bridge/wire-contract.js";
import { ALL_TOOLS } from "../../src/tools.js";

describe("declared handler timeouts", () => {
  it("come from the recorded action facts", () => {
    // If this drops to zero the recording lost its facts and every budget below
    // would quietly fall back to the default.
    expect(Object.keys(REGISTERED_HANDLER_TIMEOUT_SECONDS).length).toBeGreaterThanOrEqual(10);
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.create_cpp_class).toBe(300);
  });

  it("take the default and the ceiling from the wire contract", () => {
    expect(DEFAULT_BRIDGE_TIMEOUT_MS).toBe(DEFAULT_HANDLER_TIMEOUT_SECONDS * 1000);
    expect(MAX_BRIDGE_TIMEOUT_MS).toBe(MAX_REQUEST_TIMEOUT_SECONDS * 1000);
  });
});

describe("bridge actions", () => {
  it("leave their timeout to the C++ declaration", () => {
    // epic_call_mcp_tool runs whichever Epic tool it is handed, so a caller of it
    // (canvas) names a budget for the tool it runs.
    const restated = ALL_TOOLS.flatMap((tool) =>
      Object.entries(tool.actions)
        .filter(([, spec]) => spec.kind === "bridge" && spec.timeoutMs !== undefined && spec.bridge !== "epic_call_mcp_tool")
        .map(([action]) => `${tool.name}.${action}`),
    );
    expect(restated, "declare the timeout at the action's C++ registration (Timeout / RegisterHandlerWithTimeout)").toEqual([]);
  });
});

describe("resolveBridgeTimeout", () => {
  const noEnv: NodeJS.ProcessEnv = {};

  it("leaves an ordinary method on the 30s default", () => {
    expect(resolveBridgeTimeout("get_status", undefined, noEnv)).toBe(DEFAULT_BRIDGE_TIMEOUT_MS);
  });

  it("outlives the server's own limit on a method that registered one", () => {
    // The exact case in the report: the editor is allowed 300s and the client
    // used to give up at 30, so the server's error could never be read.
    const budget = resolveBridgeTimeout("delete_exact_labeled_actors_in_levels", undefined, noEnv);
    expect(budget).toBe(300_000 + SERVER_TIMEOUT_MARGIN_MS);
    expect(budget).toBeGreaterThan(registeredTimeoutMs("delete_exact_labeled_actors_in_levels")!);
  });

  it("honours an explicit per-call budget, longer or shorter", () => {
    expect(resolveBridgeTimeout("get_status", 600_000, noEnv)).toBe(600_000);
    expect(resolveBridgeTimeout("delete_exact_labeled_actors_in_levels", 5_000, noEnv)).toBe(5_000);
  });

  it("caps any budget at the ceiling", () => {
    expect(resolveBridgeTimeout("get_status", 99_999_999, noEnv)).toBe(MAX_BRIDGE_TIMEOUT_MS);
  });

  it("raises the floor from the environment without lowering a registered one", () => {
    const env = { [TIMEOUT_ENV_VAR]: "120000" } as NodeJS.ProcessEnv;
    expect(resolveBridgeTimeout("get_status", undefined, env)).toBe(120_000);
    // A method that already waits longer is not shortened by the floor.
    expect(resolveBridgeTimeout("create_cpp_class", undefined, env))
      .toBe(300_000 + SERVER_TIMEOUT_MARGIN_MS);
  });

  it("ignores an unusable environment value rather than failing the call", () => {
    for (const raw of ["", "abc", "0", "-5"]) {
      const env = { [TIMEOUT_ENV_VAR]: raw } as NodeJS.ProcessEnv;
      expect(environmentTimeoutMs(env), raw).toBeUndefined();
      expect(resolveBridgeTimeout("get_status", undefined, env)).toBe(DEFAULT_BRIDGE_TIMEOUT_MS);
    }
  });

  it("ignores an unusable explicit budget", () => {
    expect(resolveBridgeTimeout("get_status", 0, noEnv)).toBe(DEFAULT_BRIDGE_TIMEOUT_MS);
    expect(resolveBridgeTimeout("get_status", Number.NaN, noEnv)).toBe(DEFAULT_BRIDGE_TIMEOUT_MS);
    expect(resolveBridgeTimeout("get_status", -1, noEnv)).toBe(DEFAULT_BRIDGE_TIMEOUT_MS);
  });
});
