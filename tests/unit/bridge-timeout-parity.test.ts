/**
 * The client's timeout budget must not be shorter than the server's (#989).
 *
 * The C++ side registers a per-handler timeout with
 * FMCPHandlerRegistry::RegisterHandlerWithTimeout, and the game-thread executor
 * waits that long before giving up. The client waited a flat 30 seconds for
 * every method, so a handler allowed 300 seconds there was cut off after 30
 * here: the editor went on to finish the work, the client reported a failure,
 * and a naive retry applied the mutation twice.
 *
 * Those registered values are not advertised. get_bridge_capabilities publishes
 * the protocol version, the feature list and the registered action names, and
 * nothing about their timeouts, so a client cannot read them off the wire from
 * a plugin that is already built. The TypeScript table is therefore a hand
 * mirror, and this test is what keeps it honest: it parses the plugin sources
 * and fails when a registration is added, removed or changed without the table
 * following. A mirror nobody checks is worse than no mirror.
 */
import { readFileSync, readdirSync, statSync } from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
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

const HERE = path.dirname(fileURLToPath(import.meta.url));
const PLUGIN_SOURCE = path.join(HERE, "..", "..", "plugin");

function sourceFiles(dir: string, out: string[] = []): string[] {
  for (const entry of readdirSync(dir)) {
    const full = path.join(dir, entry);
    if (statSync(full).isDirectory()) sourceFiles(full, out);
    else if (/\.(cpp|h)$/i.test(entry)) out.push(full);
  }
  return out;
}

/**
 * Every RegisterHandlerWithTimeout call in the plugin, as method to seconds.
 *
 * The third argument is usually a float literal. One registration passes a
 * named constexpr declared in the same file, so a named argument is resolved
 * against the constants that file declares before it is given up on.
 */
function registrationsInPlugin(): { found: Record<string, number>; unresolved: string[] } {
  const found: Record<string, number> = {};
  const unresolved: string[] = [];
  // The call ends at its closing parenthesis, or at the opening brace of a
  // parameter spec (#1057).
  const CALL = /RegisterHandlerWithTimeout\(\s*TEXT\("([a-z0-9_]+)"\)\s*,\s*&?[A-Za-z0-9_:]+\s*,\s*([A-Za-z0-9_.]+)f?\s*(?:\)|,\s*\{)/g;
  const CONST = /constexpr\s+(?:float|double)\s+([A-Za-z0-9_]+)\s*=\s*([0-9.]+)f?\s*;/g;

  for (const file of sourceFiles(PLUGIN_SOURCE)) {
    const text = readFileSync(file, "utf8");
    if (!text.includes("RegisterHandlerWithTimeout")) continue;

    const constants = new Map<string, number>();
    for (const match of text.matchAll(CONST)) constants.set(match[1], Number(match[2]));

    for (const match of text.matchAll(CALL)) {
      const [, method, rawValue] = match;
      const literal = Number(rawValue.replace(/f$/, ""));
      const seconds = Number.isFinite(literal) ? literal : constants.get(rawValue);
      if (seconds === undefined || !Number.isFinite(seconds)) {
        unresolved.push(`${method} (${rawValue} in ${path.basename(file)})`);
        continue;
      }
      found[method] = seconds;
    }
  }
  return { found, unresolved };
}

describe("registered handler timeouts mirror the plugin (#989)", () => {
  it("finds the registrations at all", () => {
    const { found, unresolved } = registrationsInPlugin();
    expect(unresolved, "a timeout argument this parser could not resolve").toEqual([]);
    // If this drops to zero the parser has silently stopped matching and every
    // assertion below would pass while proving nothing.
    expect(Object.keys(found).length).toBeGreaterThanOrEqual(10);
  });

  it("declares exactly the methods the plugin registers, with the same values", () => {
    const { found } = registrationsInPlugin();
    expect(
      REGISTERED_HANDLER_TIMEOUT_SECONDS,
      "src/bridge/bridge-timeouts.ts is a hand mirror of the RegisterHandlerWithTimeout "
        + "calls in plugin/. Update the table to match, in alphabetical order.",
    ).toEqual(found);
  });

  it("keeps the table alphabetical, so the next diff is readable", () => {
    const keys = Object.keys(REGISTERED_HANDLER_TIMEOUT_SECONDS);
    expect(keys).toEqual([...keys].sort());
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
