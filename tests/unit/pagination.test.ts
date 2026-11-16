/**
 * The TypeScript half of cursor pagination (T3).
 *
 * `cursor` and `limit` are DECLARED wherever they are documented. The MCP
 * layer strips keys a category's flat zod bag does not declare, so a
 * documented-but-undeclared parameter arrives as `undefined` and the call
 * returns an ordinary success for a page that was never paged.
 */
import { describe, it, expect } from "vitest";
import { z } from "zod";
import { CURSOR_PARAM } from "../../src/surface/pagination.js";
import { actionAccepts, actionSchema } from "../../src/surface/action-schema.js";
import { reflectionTool } from "../../src/tools/reflection.js";
import { paramMapperOf } from "../../src/surface/epic-input.js";

describe("pagination parameter declarations", () => {
  it("accepts an omitted cursor", () => {
    expect(CURSOR_PARAM.parse(undefined)).toBeUndefined();
  });

  it("describes the cursor, since the description is the agent's only guide", () => {
    expect((CURSOR_PARAM as unknown as { description?: string }).description ?? "").not.toEqual("");
  });
});

describe("the reflection category, which is the first adopter", () => {
  const pagedActions = ["list_classes", "list_structs", "list_tags", "list_loaded_modules", "reflect_instance"];

  it("declares cursor and limit in its shape", () => {
    expect(reflectionTool.schema.cursor).toBeDefined();
    expect(reflectionTool.schema.limit).toBeDefined();
  });

  it("documents cursor and limit on every paged action", () => {
    for (const action of pagedActions) {
      const spec = reflectionTool.actions[action];
      expect(spec, `${action} is registered`).toBeDefined();
      const documented = actionAccepts(spec)?.takes ?? [];
      expect(documented, `${action} documents cursor`).toContain("cursor");
      expect(documented, `${action} documents limit`).toContain("limit");
    }
  });

  it("forwards cursor and limit through every mapParams it has", () => {
    // An action that maps its parameters by hand drops anything it does not
    // name, so a paged action with a mapParams has to list both explicitly.
    for (const action of pagedActions) {
      const spec = reflectionTool.actions[action];
      const mapper = spec.kind === "bridge" ? paramMapperOf(spec) : undefined;
      if (!mapper) continue;
      const mapped = mapper({ action, cursor: "abc", limit: 5 });
      expect(mapped.cursor, `${action} forwards cursor`).toBe("abc");
      expect(mapped.limit, `${action} forwards limit`).toBe(5);
    }
  });

  it("routes reflect_instance at the bridge method that answers the instance question", () => {
    expect(reflectionTool.actions.reflect_instance.bridge).toBe("reflect_instance");
  });

  it("declares every parameter reflect_instance documents", () => {
    const documented = actionSchema(reflectionTool, "reflect_instance").params
      .filter((p) => p.sources.includes("documented"));
    expect(documented.length).toBeGreaterThan(0);
    const declared = actionAccepts(reflectionTool.actions.reflect_instance)?.takes ?? [];
    for (const name of declared) {
      expect(reflectionTool.schema[name], `reflect_instance's '${name}' is declared`).toBeDefined();
    }
    // objectPath is the one required selector; everything else narrows.
    expect(documented.find((p) => p.name === "objectPath")?.required).toBe(true);
  });

  it("forwards list_structs' package and name filters to the bridge (#1088)", () => {
    const spec = reflectionTool.actions.list_structs;
    expect(spec.bridge).toBe("list_structs");
    expect(reflectionTool.schema.package).toBeDefined();
    expect(reflectionTool.schema.filter).toBeDefined();
    // Spec'd (#1057): no mapParams, so the bag reaches the bridge as sent.
    expect(spec.mapParams).toBeUndefined();
    expect(actionAccepts(spec)?.takes).toEqual(expect.arrayContaining(["package", "filter"]));
  });

  it("keeps limit a positive whole number now that pagination owns it", () => {
    const limit = reflectionTool.schema.limit as z.ZodType;
    expect(() => limit.parse(-1)).toThrow();
    expect(limit.parse(10)).toBe(10);
  });
});
