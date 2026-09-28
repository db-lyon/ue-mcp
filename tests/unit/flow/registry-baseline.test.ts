/**
 * Characterisation of the task registry: every registered name and what
 * describe reports for each, recorded so a refactor of the task family shows
 * any drift as a diff. Re-record with `npx vitest run <this file> -u` and
 * review the change before committing it.
 */
import { describe, expect, it } from "vitest";
import type { TaskDefinition } from "@db-lyon/flowkit";
import { ALL_TOOLS } from "../../../src/tools.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { buildMicroGateway } from "../../../src/surface/context/micro-context.js";

/** Sorted keys at every depth, so the recording does not depend on insertion order. */
function canonical(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.keys(value as Record<string, unknown>).sort().map((k) => [k, canonical((value as Record<string, unknown>)[k])]),
    );
  }
  return value;
}

describe("task registry baseline", () => {
  it("lists the same names under every context strategy", async () => {
    const full = buildFlowRegistry(ALL_TOOLS).listRegistered().sort();
    const micro = buildFlowRegistry([buildMicroGateway(ALL_TOOLS), ...ALL_TOOLS]).listRegistered().sort();
    await expect(JSON.stringify({ full, micro }, null, 1) + "\n")
      .toMatchFileSnapshot("./__baselines__/registry-listing.json");
  });

  it("describes every registered task the same way", async () => {
    const registry = buildFlowRegistry(ALL_TOOLS);
    const definitions = buildDefaults(ALL_TOOLS).tasks as Record<string, TaskDefinition>;
    const lines: string[] = [];
    for (const name of registry.listRegistered().sort()) {
      let described: unknown;
      try {
        described = await registry.describe(name, definitions);
      } catch (e) {
        described = { describeError: e instanceof Error ? e.message : String(e) };
      }
      lines.push(JSON.stringify(canonical(described)));
    }
    await expect(`[\n${lines.join(",\n")}\n]\n`).toMatchFileSnapshot("./__baselines__/registry-describe.json");
  });
});
