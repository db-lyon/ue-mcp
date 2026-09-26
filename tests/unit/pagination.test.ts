/**
 * The TypeScript half of cursor pagination (T3).
 *
 * Two properties matter here, and each of them is a real failure this repo
 * has already paid for once:
 *
 *   1. `cursor` and `limit` are DECLARED wherever they are documented. The MCP
 *      layer strips keys a category's flat zod bag does not declare, so a
 *      documented-but-undeclared parameter arrives as `undefined` and the call
 *      returns an ordinary success for a page that was never paged.
 *   2. `paged()` puts the two names INSIDE the `Params:` clause, because that
 *      clause is what `describe_action` and the drift test read. Appending
 *      after a `Returns` section would document them somewhere nothing looks.
 */
import { describe, it, expect } from "vitest";
import { z } from "zod";
import {
  CURSOR_PARAM,
  LIMIT_PARAM,
  PAGINATION_PARAM_NAMES,
  PAGINATION_SCHEMA,
  paged,
} from "../../src/pagination.js";
import { parseParamsClause } from "../../src/action-schema.js";
import { reflectionTool } from "../../src/tools/reflection.js";

describe("pagination parameter declarations", () => {
  it("declares exactly cursor and limit", () => {
    expect(Object.keys(PAGINATION_SCHEMA).sort()).toEqual(["cursor", "limit"]);
    expect(PAGINATION_SCHEMA.cursor).toBe(CURSOR_PARAM);
    expect(PAGINATION_SCHEMA.limit).toBe(LIMIT_PARAM);
    expect([...PAGINATION_PARAM_NAMES]).toEqual(["cursor", "limit"]);
  });

  it("accepts an omitted cursor and a whole positive limit", () => {
    expect(CURSOR_PARAM.parse(undefined)).toBeUndefined();
    expect(LIMIT_PARAM.parse(undefined)).toBeUndefined();
    expect(LIMIT_PARAM.parse(25)).toBe(25);
  });

  it("refuses a limit that is not a positive whole number", () => {
    expect(() => LIMIT_PARAM.parse(0)).toThrow();
    expect(() => LIMIT_PARAM.parse(-5)).toThrow();
    expect(() => LIMIT_PARAM.parse(2.5)).toThrow();
  });

  it("describes both parameters, since the description is the agent's only guide", () => {
    for (const schema of Object.values(PAGINATION_SCHEMA)) {
      const described = schema as unknown as { description?: string };
      expect(described.description ?? "").not.toEqual("");
    }
  });
});

describe("paged()", () => {
  it("adds both names to the end of an existing Params clause", () => {
    const out = paged("List gameplay tags. Params: filter?");
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["filter", "cursor", "limit"]);
  });

  it("adds only the missing name when the action already documents one", () => {
    const out = paged("List classes. Params: parentFilter?, limit?");
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["parentFilter", "limit", "cursor"]);
    // `limit` is documented once, not twice.
    expect(out.match(/\blimit\b/g)).toHaveLength(1);
  });

  it("inserts before a Returns section rather than after it", () => {
    const out = paged(
      "Enumerate modules. Params: filter? (case-insensitive substring), loadedOnly? (default false)."
      + " Returns modules[{name, loaded}] + totalLoaded",
    );
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["filter", "loadedOnly", "cursor", "limit"]);
    expect(out.indexOf("cursor?")).toBeLessThan(out.indexOf("Returns"));
  });

  it("inserts before a trailing issue reference, which is prose and not a parameter", () => {
    const out = paged("Enumerate modules. Params: filter?, loadedOnly? (#689)");
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["filter", "loadedOnly", "cursor", "limit"]);
    expect(out.trimEnd().endsWith("(#689)")).toBe(true);
  });

  it("keeps a bracketed comma attached to the parameter it documents", () => {
    const out = paged("Do a thing. Params: mode? (one of a, b, c)");
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["mode", "cursor", "limit"]);
  });

  it("adds a Params clause when the description has none", () => {
    const out = paged("List everything.");
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["cursor", "limit"]);
  });

  it("stays readable when the action it wraps documented `Params: none`", () => {
    // The two names land BEHIND the word `none`, which reads oddly and is the
    // shape a good few paged actions carry (`pcg.list_graphs` among them). It
    // is only a wording problem: the clause continues, so the reader has to
    // continue with it, and the paging parameters are advertised either way.
    const out = paged("List every graph. Params: none");
    expect(out).toContain("Params: none, cursor?, limit?");
    expect(parseParamsClause(out).map((p) => p.name)).toEqual(["cursor", "limit"]);
  });

  it("is idempotent, so re-wrapping a description cannot document a name twice", () => {
    const once = paged("List gameplay tags. Params: filter?");
    expect(paged(once)).toBe(once);
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
      const documented = parseParamsClause(spec.description ?? "").map((p) => p.name);
      expect(documented, `${action} documents cursor`).toContain("cursor");
      expect(documented, `${action} documents limit`).toContain("limit");
    }
  });

  it("forwards cursor and limit through every mapParams it has", () => {
    // An action that maps its parameters by hand drops anything it does not
    // name, so a paged action with a mapParams has to list both explicitly.
    for (const action of pagedActions) {
      const spec = reflectionTool.actions[action];
      if (!spec.mapParams) continue;
      const mapped = spec.mapParams({ action, cursor: "abc", limit: 5 });
      expect(mapped.cursor, `${action} forwards cursor`).toBe("abc");
      expect(mapped.limit, `${action} forwards limit`).toBe(5);
    }
  });

  it("routes reflect_instance at the bridge method that answers the instance question", () => {
    expect(reflectionTool.actions.reflect_instance.bridge).toBe("reflect_instance");
  });

  it("declares every parameter reflect_instance documents", () => {
    const documented = parseParamsClause(reflectionTool.actions.reflect_instance.description ?? "");
    expect(documented.length).toBeGreaterThan(0);
    for (const { name } of documented) {
      expect(reflectionTool.schema[name], `reflect_instance's '${name}' is declared`).toBeDefined();
    }
    // objectPath is the one required selector; everything else narrows.
    expect(documented.find((p) => p.name === "objectPath")?.optional).toBe(false);
  });

  it("forwards list_structs' package and name filters to the bridge (#1088)", () => {
    const spec = reflectionTool.actions.list_structs;
    expect(spec.bridge).toBe("list_structs");
    expect(reflectionTool.schema.package).toBeDefined();
    expect(reflectionTool.schema.filter).toBeDefined();
    // Spec'd (#1057): no mapParams, so the bag reaches the bridge as sent.
    expect(spec.mapParams).toBeUndefined();
    expect(parseParamsClause(spec.description ?? "").map((p) => p.name)).toEqual(
      expect.arrayContaining(["package", "filter"]),
    );
  });

  it("keeps limit a positive whole number now that pagination owns it", () => {
    const limit = reflectionTool.schema.limit as z.ZodType;
    expect(() => limit.parse(-1)).toThrow();
    expect(limit.parse(10)).toBe(10);
  });
});
