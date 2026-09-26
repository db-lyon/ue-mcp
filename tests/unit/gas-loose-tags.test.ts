/**
 * #1104: gas(add_loose_gameplay_tag) / gas(remove_loose_gameplay_tag).
 *
 * The category shape is one flat bag and the MCP layer strips undeclared keys,
 * so each documented parameter has to be declared and forwarded.
 */
import { describe, it, expect } from "vitest";
import { z } from "zod";
import { parseParams } from "../../src/surface/action-schema.js";
import { gasTool } from "../../src/tools/gas.js";

const ACTIONS = ["add_loose_gameplay_tag", "remove_loose_gameplay_tag"] as const;

describe("gas loose tag actions", () => {
  for (const action of ACTIONS) {
    it(`${action} routes to the bridge method of the same name`, () => {
      expect(gasTool.actions[action]?.bridge).toBe(action);
    });

    it(`${action} declares every parameter it documents`, () => {
      const documented = parseParams(gasTool.actions[action].description ?? "").params;
      expect(documented.map((p) => p.name)).toEqual(
        expect.arrayContaining(["actorLabel", "actorPath", "tag", "count", "world"]),
      );
      for (const { name } of documented) {
        expect(gasTool.schema[name], `${action}'s '${name}' is declared`).toBeDefined();
      }
    });

    it(`${action} forwards the selector, tag and count to the bridge untouched`, () => {
      // Spec'd from C++ (#1057): no mapParams, so the whole bag reaches the handler.
      expect(gasTool.actions[action].mapParams).toBeUndefined();
    });
  }

  it("keeps count a whole number", () => {
    const count = gasTool.schema.count as z.ZodType;
    expect(count.parse(3)).toBe(3);
    expect(() => count.parse(1.5)).toThrow();
  });
});
