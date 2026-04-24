/**
 * Schema drift guard: every C++ handler registered via
 * `Registry.RegisterHandler(TEXT("name"), &Handler)` should either
 *   (a) be referenced by at least one TS action's `bridge: "name"`, or
 *   (b) be on the intentional-internal allow-list below.
 *
 * And every TS action that declares `bridge: "name"` should map to a real
 * C++ handler.
 *
 * This closes the H3 footgun flagged in review: previously the TS schema's
 * param names were manually kept in sync with C++ handler signatures with
 * no compile-time check. This test catches at least the name-level drift
 * in CI, so a rename on either side fails loud.
 */
import { describe, it, expect } from "vitest";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS, enumerateBridgeActions } from "../../src/tools.js";

const here = dirname(fileURLToPath(import.meta.url));
const handlersDir = join(
  here,
  "..",
  "..",
  "plugin",
  "ue_mcp_bridge",
  "Source",
  "UE_MCP_Bridge",
  "Private",
  "Handlers",
);

function cppHandlerNames(): Set<string> {
  const re = /Registry\.RegisterHandler(?:WithTimeout)?\(\s*TEXT\("([^"]+)"\)/g;
  const names = new Set<string>();
  for (const entry of readdirSync(handlersDir)) {
    if (!entry.endsWith(".cpp")) continue;
    const body = readFileSync(join(handlersDir, entry), "utf8");
    for (const m of body.matchAll(re)) names.add(m[1]);
  }
  return names;
}

// Handlers registered on the C++ side that intentionally have no TS-action
// surface. Typically infra endpoints (bridge health) or reachable via flows.
// Add a comment next to each entry when extending this list.
const CPP_ONLY: ReadonlySet<string> = new Set<string>([
  // e.g. "ping",  // infra health endpoint, not user-facing
]);

describe("TS <-> C++ bridge name drift", () => {
  it("every TS bridge: call names a real C++ handler", () => {
    const cpp = cppHandlerNames();
    const ts = enumerateBridgeActions();
    const missing: typeof ts = ts.filter((a) => !cpp.has(a.bridge));

    if (missing.length > 0) {
      const msg = missing
        .map((m) => `  ${m.tool}.${m.action} -> bridge "${m.bridge}" (no C++ handler registered)`)
        .join("\n");
      throw new Error(
        `${missing.length} TS action(s) reference a bridge method that does not exist:\n${msg}\n\n` +
          `Either the C++ handler was renamed/removed or the TS action points at the wrong method name.`,
      );
    }
    expect(missing).toEqual([]);
  });

  // C++ handler orphans are a softer problem than the reverse direction:
  // the capability exists on the bridge, it's just not surfaced as a
  // first-class TS action. Usually reachable via flows or execute_python
  // fallback. Report the count so we can drive it down over time; do not
  // fail the suite.
  it("reports C++ handlers with no TS action referencing them (non-fatal)", () => {
    const cpp = cppHandlerNames();
    const ts = enumerateBridgeActions();
    const referenced = new Set(ts.map((a) => a.bridge));
    const orphans = [...cpp]
      .filter((name) => !referenced.has(name) && !CPP_ONLY.has(name))
      .sort();

    if (orphans.length > 0) {
      // eslint-disable-next-line no-console
      console.warn(
        `[drift] ${orphans.length} C++ handler(s) have no TS action surface. ` +
          `Either wire them as actions or move to CPP_ONLY with a reason.\n` +
          orphans.map((o) => `  - ${o}`).join("\n"),
      );
    }
    // Intentionally does not assert. See test title.
    expect(true).toBe(true);
  });

  it("TS tool action names do not accidentally collide across tools", () => {
    // Multiple tools can reuse the same ACTION key (e.g. "list"); that's fine
    // because dispatch happens through tool+action. But a single TOOL should
    // not have a duplicate action key after merging in any overrides.
    for (const t of ALL_TOOLS) {
      const keys = Object.keys(t.actions);
      const unique = new Set(keys);
      expect(unique.size, `${t.name} has duplicate action keys`).toBe(keys.length);
    }
  });
});
