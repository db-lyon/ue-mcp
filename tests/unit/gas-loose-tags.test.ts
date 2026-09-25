/**
 * #1104: gas(add_loose_gameplay_tag) / gas(remove_loose_gameplay_tag).
 *
 * The category shape is one flat bag and the MCP layer strips undeclared keys,
 * so each documented parameter has to be declared and forwarded.
 */
import { describe, it, expect } from "vitest";
import { z } from "zod";
import { parseParamsClause } from "../../src/action-schema.js";
import { gasTool } from "../../src/tools/gas.js";

const ACTIONS = ["add_loose_gameplay_tag", "remove_loose_gameplay_tag"] as const;

describe("gas loose tag actions", () => {
  for (const action of ACTIONS) {
    it(`${action} routes to the bridge method of the same name`, () => {
      expect(gasTool.actions[action]?.bridge).toBe(action);
    });

    it(`${action} declares every parameter it documents`, () => {
      const documented = parseParamsClause(gasTool.actions[action].description ?? "");
      expect(documented.map((p) => p.name)).toEqual(
        expect.arrayContaining(["actorLabel", "actorPath", "tag", "count", "world", "pieInstance"]),
      );
      for (const { name } of documented) {
        expect(gasTool.schema[name], `${action}'s '${name}' is declared`).toBeDefined();
      }
    });

    it(`${action} forwards the selector, tag and count to the bridge`, () => {
      const mapped = gasTool.actions[action].mapParams!({
        action,
        actorPath: "/Game/Map.Map:PersistentLevel.Hero",
        tag: "Status.Stunned",
        count: 2,
        world: "pie",
        pieInstance: 1,
      });
      expect(mapped).toMatchObject({
        actorPath: "/Game/Map.Map:PersistentLevel.Hero",
        tag: "Status.Stunned",
        count: 2,
        world: "pie",
        pieInstance: 1,
      });
    });
  }

  it("keeps count a whole number", () => {
    const count = gasTool.schema.count as z.ZodType;
    expect(count.parse(3)).toBe(3);
    expect(() => count.parse(1.5)).toThrow();
  });
});
