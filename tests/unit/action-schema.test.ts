/**
 * The advertised parameter surface has to match the accepted one.
 *
 * A category's zod shape is one flat bag shared by every action in it, and an
 * MCP tool call is validated against that bag with unknown keys stripped. So a
 * parameter an action documents, or reads out of its own `mapParams`, but
 * which the category never declares, does not fail: it arrives at the handler
 * as `undefined`. The call then returns a perfectly ordinary success for a
 * mutation that never happened, which is the single worst failure shape this
 * server has, because nothing downstream can detect it.
 *
 * These tests hold three properties over the whole surface:
 *
 *   1. every action documents its parameters at all,
 *   2. every documented or forwarded parameter is actually declared, and
 *   3. `describe_action` can answer for any of them.
 *
 * A new action that breaks any of the three fails here rather than in an
 * agent session six weeks later.
 */
import { describe, it, expect } from "vitest";
import { z } from "zod";
import { categoryTool, bp } from "../../src/types.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { requiresExplicitEditor } from "../../src/action-class.js";
import {
  actionSchema,
  allActionSchemas,
  forwardedParams,
  nearestActions,
  parseParams,
  parseParamsClause,
  resolveActionRef,
  similarity,
  suggestActions,
} from "../../src/action-schema.js";

describe("action parameter schema", () => {
  it("declares every parameter it documents or forwards", () => {
    const offenders = allActionSchemas(ALL_TOOLS)
      .filter((a) => a.drift.length > 0)
      .map((a) => `${a.tool}.${a.action}: ${a.drift.join(", ")}`);

    expect(
      offenders,
      "These actions document or read a parameter their category never declares.\n"
        + "The MCP layer strips undeclared keys, so passing one has NO effect and the\n"
        + "call still reports success. Declare it in the category's extraSchema, or\n"
        + "stop documenting/reading it:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });

  it("documents parameters on every action", () => {
    const undocumented: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [action, spec] of Object.entries(tool.actions)) {
        const description = spec.description ?? "";
        if (!/\bParams:/.test(description)) undocumented.push(`${tool.name}.${action}`);
      }
    }
    expect(
      undocumented,
      "Every action's description must carry a `Params:` clause listing what it\n"
        + "takes, or `Params: none`. It is the only per-action parameter documentation\n"
        + "the surface has - the zod shape is per category, not per action:\n  "
        + undocumented.join("\n  "),
    ).toEqual([]);
  });

  it("agrees with itself about whether a parameter is required", () => {
    // A parameter the description marks required must not be declared with a
    // schema default, which would silently make it optional.
    const contradictions: string[] = [];
    for (const schema of allActionSchemas(ALL_TOOLS)) {
      for (const param of schema.params) {
        if (param.required && param.default !== undefined) {
          contradictions.push(`${schema.tool}.${schema.action}.${param.name}`);
        }
      }
    }
    expect(contradictions).toEqual([]);
  });
});

describe("parseParamsClause", () => {
  it("reads a plain list, with optionality from the ? marker", () => {
    expect(parseParamsClause("Does a thing. Params: assetPath, propertyName, save?")).toEqual([
      { name: "assetPath", optional: false },
      { name: "propertyName", optional: false },
      { name: "save", optional: true },
    ]);
  });

  it("treats a parenthesised aside as commentary, not as parameters", () => {
    expect(parseParamsClause("Params: query (space-separated keywords), limit? (default 20)")).toEqual([
      { name: "query", optional: false },
      { name: "limit", optional: true },
    ]);
  });

  it("stops at the Returns clause, whose names are result fields", () => {
    const names = parseParamsClause("Params: assetPath. Returns min, max, boxExtent, meshKind").map((p) => p.name);
    expect(names).toEqual(["assetPath"]);
  });

  it("stops where the description resumes prose", () => {
    const names = parseParamsClause(
      "Params: path (relative to Source/), content. After editing, call live_coding_compile.",
    ).map((p) => p.name);
    expect(names).toEqual(["path", "content"]);
  });

  it("reads both sides of an OR alternative", () => {
    const names = parseParamsClause("Params: assetPaths? (string[]) OR directory?, classNames?").map((p) => p.name);
    expect(names).toEqual(["assetPaths", "directory", "classNames"]);
  });

  it("reads a nested (+ ...) group as further parameters", () => {
    const names = parseParamsClause("Params: directory? (+ recursive?, default true), limit?").map((p) => p.name);
    expect(names).toEqual(["directory", "recursive", "limit"]);
  });

  it("reads a slash alias as both spellings, since the resolver accepts both", () => {
    const names = parseParamsClause("Params: target/targetLabel (actor label) OR targetPath").map((p) => p.name);
    expect(names).toEqual(["target", "targetLabel", "targetPath"]);
  });

  it("looks past an 'at least one of' quantifier to the parameters behind it", () => {
    const names = parseParamsClause("Params: properties, at least one of actorLabels/labelPrefix, dryRun?").map((p) => p.name);
    expect(names).toContain("actorLabels");
    expect(names).toContain("labelPrefix");
    expect(names).not.toContain("at");
  });

  it("reads `Params: none` as no parameters rather than as a parameter named none", () => {
    expect(parseParamsClause("Reads the thing. Params: none (#204)")).toEqual([]);
  });

  it("keeps reading past `none` when the clause carries on", () => {
    // `paged()` appends `cursor?, limit?` to whatever clause is already there,
    // including `none`. Treating `none` as a terminator hid the paging
    // parameters of every paged action that documents no others, which left
    // page two of pcg(list_graphs) unreachable from the advertised schema.
    expect(parseParamsClause("Lists them. Params: none, cursor?, limit?").map((p) => p.name))
      .toEqual(["cursor", "limit"]);
  });

  it("reads a parameter whose name is also an English word", () => {
    // `value`, `all`, `from`, `to` and `name` are real parameters on this
    // surface. A prose filter that refuses them by vocabulary deletes a
    // required field from the schema an agent is handed, and the agent then
    // makes a call the handler rejects - or, where the handler reads the field
    // unguarded, one that writes an empty string and reports success.
    expect(parseParamsClause("Params: assetPath, parameterName, value, association?").map((p) => p.name))
      .toEqual(["assetPath", "parameterName", "value", "association"]);
    expect(parseParamsClause("Params: foliageTypePath, center?, radius?, all?").map((p) => p.name))
      .toContain("all");
  });

  it("reads every separator the descriptions actually use", () => {
    expect(parseParamsClause("Params: widgetName? | className?, childName?").map((p) => p.name))
      .toEqual(["widgetName", "className", "childName"]);
    expect(parseParamsClause("Params: actorLabels[] and/or actorPaths[]").map((p) => p.name))
      .toEqual(["actorLabels", "actorPaths"]);
    expect(parseParamsClause("Params: assetPaths (string[]) or assetPath").map((p) => p.name))
      .toEqual(["assetPaths", "assetPath"]);
    expect(parseParamsClause("Params: systemPath, scaleX?/scaleY?/scaleZ? (separate keys)").map((p) => p.name))
      .toEqual(["systemPath", "scaleX", "scaleY", "scaleZ"]);
  });

  it("stops where the prose resumes inside an item", () => {
    // `where each entry is ...` describes the shape of `renames`, and the
    // names behind it are fields of that shape, not parameters.
    expect(parseParamsClause("Params: renames[] where each entry is {sourcePath, destinationPath}").map((p) => p.name))
      .toEqual(["renames"]);
    expect(parseParamsClause("Params: dryRun? to preview").map((p) => p.name)).toEqual(["dryRun"]);
  });

  it("reports alternatives as one choice rather than as several required names", () => {
    const { params, alternatives } = parseParams("Params: actorLabel OR actorPath, seed?");
    expect(params.filter((p) => p.group !== undefined).map((p) => p.name)).toEqual(["actorLabel", "actorPath"]);
    expect(alternatives).toEqual([{ branches: [["actorLabel"], ["actorPath"]], required: true }]);
  });

  it("keeps the names that go together inside one branch of a choice", () => {
    const { alternatives } = parseParams("Params: name + packagePath? (create) OR materialPath (build)");
    expect(alternatives[0].branches).toEqual([["name", "packagePath"], ["materialPath"]]);
  });

  it("says a choice is optional when the clause marks every side optional", () => {
    expect(parseParams("Params: widgetName? | className?").alternatives[0].required).toBe(false);
  });

  it("carries an `at least one of` quantifier across the names behind it", () => {
    const { params, alternatives } = parseParams(
      "Params: at least one of labelPrefix, labelContains, className; dryRun? to preview",
    );
    expect(alternatives).toEqual([
      { branches: [["labelPrefix"], ["labelContains"], ["className"]], required: true },
    ]);
    expect(params.find((p) => p.name === "dryRun")).toEqual({ name: "dryRun", optional: true });
  });

  it("uses the declared keys to settle a bracket that could be prose", () => {
    // `(or ...)` carries real alias names (`sourceString (or value)`) and also
    // plain English (`boneName (or socket name)`). The category's own shape is
    // what tells the two apart, so a name is only read out of one when the
    // wire would accept every name in it.
    const known = new Set(["sourceString", "value"]);
    expect(parseParamsClause("Params: sourceString (or value)", known).map((p) => p.name))
      .toEqual(["sourceString", "value"]);
    expect(parseParamsClause("Params: boneName (or socket name)", new Set(["boneName", "name"])).map((p) => p.name))
      .toEqual(["boneName"]);
  });

  it("returns nothing when there is no clause at all", () => {
    expect(parseParamsClause("Just a description.")).toEqual([]);
  });
});

describe("forwardedParams", () => {
  it("reads the keys a mapParams closure pulls off its bag", () => {
    const names = forwardedParams({ bridge: "x", mapParams: (p) => ({ a: p.alpha, b: p.beta }) });
    expect(names.sort()).toEqual(["alpha", "beta"]);
  });

  it("reports both spellings of an alias", () => {
    const names = forwardedParams({ bridge: "x", mapParams: (p) => ({ assetPath: p.assetPath ?? p.path }) });
    expect(names.sort()).toEqual(["assetPath", "path"]);
  });

  it("does not mistake a method call for a parameter", () => {
    // `arr.length` and `Array.isArray` are not parameters, and reporting them
    // would put permanent false entries in the drift guard above.
    const names = forwardedParams({
      bridge: "x",
      mapParams: (p) => ({ n: Array.isArray(p.items) ? (p.items as unknown[]).length : 0 }),
    });
    expect(names).toEqual(["items"]);
  });

  it("reads a local handler's second argument", () => {
    const names = forwardedParams({ handler: async (_ctx, p) => p.slotName });
    expect(names).toEqual(["slotName"]);
  });

  it("is empty for an action that takes no parameters", () => {
    expect(forwardedParams({ bridge: "x" })).toEqual([]);
  });
});

describe("actionSchema", () => {
  it("preserves nested fields, array items, enums, defaults and required nullable values", () => {
    const tool = categoryTool("example", "Example", {
      inspect: bp("Inspect. Params: request", "inspect"),
    }, undefined, {
      request: z.object({
        targets: z.array(z.object({
          name: z.string().nullable(),
          axis: z.enum(["forward", "right", "up"]),
          amount: z.number().default(5),
        })),
        mode: z.union([z.literal("preview"), z.literal("apply")]).optional(),
      }).optional(),
    });
    const request = actionSchema(tool, "inspect").params.find((p) => p.name === "request")!;
    expect(request.required).toBe(true);
    const fields = request.properties!.targets.items!.properties!;
    expect(fields.name.required).toBe(true);
    expect(fields.axis.enumValues).toEqual(["forward", "right", "up"]);
    expect(fields.amount).toMatchObject({ required: false, default: 5 });
    expect(request.properties!.mode).toMatchObject({ required: false, enumValues: ["preview", "apply"] });
  });

  it("bounds nested discovery and explicitly marks omitted detail", () => {
    let nested: z.ZodTypeAny = z.string();
    for (let i = 0; i < 10; i++) nested = z.object({ child: nested });
    const tool = categoryTool("example", "Example", { inspect: bp("Inspect. Params: request", "inspect") }, undefined, { request: nested });
    let result = actionSchema(tool, "inspect").params.find((p) => p.name === "request")!;
    for (let i = 0; i < 6; i++) result = result.properties!.child as typeof result;
    expect(result.truncated).toBe(true);
    expect(result.properties).toBeUndefined();
  });

  it("reports the bridge method and required parameters of a real action", () => {
    const asset = ALL_TOOLS.find((t) => t.name === "asset")!;
    const schema = actionSchema(asset, "set_property");

    expect(schema.bridge).toBe("set_asset_property");
    expect(schema.local).toBe(false);
    expect(schema.drift).toEqual([]);

    const byName = new Map(schema.params.map((p) => [p.name, p]));
    expect(byName.get("assetPath")?.required).toBe(true);
    expect(byName.get("propertyName")?.required).toBe(true);
    expect(byName.get("save")?.required).toBe(false);
    expect(byName.get("save")?.type).toBe("boolean");
  });

  it("reports a parameter the description names with an ordinary English word", () => {
    // material(set_parameter) hard-requires `value` and the description says
    // so. Reporting the schema without it had an agent call the action with
    // no value at all.
    const material = ALL_TOOLS.find((t) => t.name === "material")!;
    const byName = new Map(actionSchema(material, "set_parameter").params.map((p) => [p.name, p]));
    expect(byName.get("value")?.required).toBe(true);
  });

  it("reports the paging parameters of an action that documents no others", () => {
    const pcg = ALL_TOOLS.find((t) => t.name === "pcg")!;
    const names = actionSchema(pcg, "list_graphs").params.map((p) => p.name);
    expect(names).toContain("cursor");
    expect(names).toContain("limit");
  });

  it("publishes a choice instead of marking both sides required", () => {
    const pcg = ALL_TOOLS.find((t) => t.name === "pcg")!;
    const schema = actionSchema(pcg, "execute");
    const byName = new Map(schema.params.map((p) => [p.name, p]));
    expect(byName.get("actorLabel")?.required).toBe(false);
    expect(byName.get("actorPath")?.required).toBe(false);
    expect(schema.alternatives).toEqual([
      { branches: [["actorLabel"], ["actorPath"]], required: true },
    ]);
    expect(byName.get("actorLabel")?.alternativeGroup).toBe(0);
    expect(byName.get("actorPath")?.alternativeGroup).toBe(0);
  });

  it("marks a server-side action as local", () => {
    const project = ALL_TOOLS.find((t) => t.name === "project")!;
    expect(actionSchema(project, "search_tools").local).toBe(true);
  });

  it("offers the routing parameters every action accepts", () => {
    const project = ALL_TOOLS.find((t) => t.name === "project")!;
    const names = actionSchema(project, "search_tools").params.map((p) => p.name);
    expect(names).toContain("timeoutMs");
  });

  it("refuses an action the tool does not have", () => {
    const asset = ALL_TOOLS.find((t) => t.name === "asset")!;
    expect(() => actionSchema(asset, "no_such_action")).toThrow(/Unknown action/);
  });
});

describe("resolveActionRef", () => {
  it("resolves a qualified name to exactly one action", () => {
    const hits = resolveActionRef("asset.set_property", ALL_TOOLS);
    expect(hits).toHaveLength(1);
    expect(hits[0].tool.name).toBe("asset");
  });

  it("accepts a colon separator as well as a dot", () => {
    expect(resolveActionRef("asset:set_property", ALL_TOOLS)).toHaveLength(1);
  });

  it("returns every category providing a bare name", () => {
    const hits = resolveActionRef("save", ALL_TOOLS);
    expect(hits.length).toBeGreaterThan(1);
  });

  it("returns nothing for a name no category has", () => {
    expect(resolveActionRef("definitely_not_an_action", ALL_TOOLS)).toEqual([]);
  });

  it("does not fall back to a bare match when the category was named and lacks it", () => {
    // `asset.list_layers` must not resolve to `landscape.list_layers`: the
    // caller named a category, and quietly serving another one's action would
    // hand back a schema for a call that cannot be made.
    expect(resolveActionRef("asset.list_layers", ALL_TOOLS)).toEqual([]);
  });
});

describe("suggestions", () => {
  it("puts the intended action first for a one-character typo", () => {
    expect(suggestActions("asset.set_propery", ALL_TOOLS)[0]).toBe("asset.set_property");
  });

  it("finds an action from a truncated name", () => {
    expect(suggestActions("sculp", ALL_TOOLS)).toContain("landscape.sculpt");
  });

  it("ranks a containing name above an unrelated one", () => {
    expect(similarity("sculpt", "sculpt")).toBe(1);
    expect(similarity("sculp", "sculpt")).toBeGreaterThan(similarity("sculp", "select_actors"));
  });

  it("scores nothing for names with no real overlap", () => {
    expect(similarity("zzzz", "sculpt")).toBe(0);
  });

  it("nearestActions works off a plain name list", () => {
    expect(nearestActions("scul", ["sculpt", "paint_layer", "create"])).toEqual(["sculpt"]);
  });

  it("returns nothing rather than noise for an empty reference", () => {
    expect(nearestActions("", ["sculpt"])).toEqual([]);
    expect(suggestActions("", ALL_TOOLS)).toEqual([]);
  });
});

describe("action class", () => {
  it("labels a read as read and a mutation as mutate", () => {
    const asset = ALL_TOOLS.find((t) => t.name === "asset")!;
    expect(actionSchema(asset, "read_properties").class).toBe("read");
    expect(actionSchema(asset, "set_property").class).toBe("mutate");
    expect(actionSchema(asset, "delete").class).toBe("mutate");
  });

  it("labels every action on the surface", () => {
    // `unknown` is a real answer for an action whose effect a parameter
    // decides, but it must be a deliberate one, not a gap.
    for (const schema of allActionSchemas(ALL_TOOLS)) {
      expect(["read", "mutate", "unknown"], `${schema.tool}.${schema.action}`).toContain(schema.class);
    }
  });

  it("says unknown for an action whose effect a parameter decides", () => {
    // What editor(invoke_function) does is chosen by the UFUNCTION named in
    // its parameters, so the honest label is unknown. It is gated exactly
    // like a mutation, which is why the honest label costs nothing.
    const editor = ALL_TOOLS.find((t) => t.name === "editor")!;
    expect(actionSchema(editor, "invoke_function").class).toBe("unknown");
    expect(requiresExplicitEditor(actionSchema(editor, "invoke_function").class)).toBe(true);
  });

  it("gates arbitrary python as a mutation rather than leaving it unlabelled", () => {
    const editor = ALL_TOOLS.find((t) => t.name === "editor")!;
    expect(actionSchema(editor, "execute_python").class).toBe("mutate");
  });
});
