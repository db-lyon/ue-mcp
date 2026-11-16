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
import { categoryTool, bp } from "../../src/surface/category-tool.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { requiresExplicitEditor } from "../../src/surface/action-class.js";
import {
  actionAccepts,
  actionSchema,
  nearestActions,
  unknownActionMessage,
  resolveActionRef,
  similarity,
  suggestActions,
} from "../../src/surface/action-schema.js";

const surfaceSchemas = () => ALL_TOOLS.flatMap((tool) => Object.keys(tool.actions).map((action) => actionSchema(tool, action)));

describe("action parameter schema", () => {
  it("declares every parameter it documents or forwards", () => {
    const offenders = surfaceSchemas()
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

  // ROUTING_PARAMS exempts `action` from the drift check above, which is also
  // a blind spot: a HANDLER parameter of that name is undeclarable,
  // unforwardable and undocumentable, and nothing said so (#1078).
  it("never declares or documents a handler parameter named 'action'", () => {
    const offenders: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [name, spec] of Object.entries(tool.actions)) {
        if (actionAccepts(spec)?.keys.has("action")) {
          offenders.push(`${tool.name}.${name}: declares a parameter named 'action'`);
        }
      }
    }
    expect(
      offenders,
      "The category tool dispatches on 'action', so a handler parameter of that\n"
        + "name can never arrive: the surface strips it, and a caller who writes it\n"
        + "selects an action instead. Give it another name on the wire and read the\n"
        + "new one in the handler, or accept it nested inside 'input':\n  "
        + offenders.join("\n  "),
    ).toEqual([]);
  });

  // The static check above cannot see a closure that spreads its argument, and
  // every generated Epic action has that shape. This one dispatches for real
  // and looks at what the bridge was handed.
  it("never hands the bridge the dispatch key, whatever mapParams does with its bag", async () => {
    const offenders: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [name, spec] of Object.entries(tool.actions)) {
        if (spec.kind !== "bridge") continue;
        let seen: Record<string, unknown> | null = null;
        const ctx = {
          bridge: {
            isConnected: true,
            call: async (_m: string, params: Record<string, unknown>) => { seen = params; return {}; },
            getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
            connect: async () => {},
            retargetProject: () => ({}),
          },
          project: {},
        } as never;
        try {
          await tool.handler(ctx, { action: name });
        } catch {
          // A refusal before dispatch proves the key never reached the bridge.
          continue;
        }
        if (seen === null) continue;
        const serialized = JSON.stringify(seen);
        if (serialized.includes(`"${name}"`)) {
          offenders.push(`${tool.name}.${name}: the bridge payload contains the action name`);
        }
        // An `action` KEY is not the fault: play_in_editor and play_sequence
        // call bridge methods whose own argument is named action, renamed on
        // the surface. The fault is the dispatch key's VALUE travelling.
      }
    }
    expect(
      offenders,
      "The dispatcher consumes 'action' to choose the spec, so it must never\n"
        + "reach the bridge as an argument. An action whose mapParams forwards its\n"
        + "whole bag sends the action's own NAME to the handler, which for a\n"
        + "wrapped tool taking an argument called 'action' is a real value it will\n"
        + "try to use:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });

  // niagara(batch) dispatches to other actions itself, so it never reaches the
  // check above and carried the same leak after types.ts was fixed. The probe
  // is a spec that forwards its bag verbatim, because no shipped niagara action
  // does today and a test that cannot fail proves nothing.
  it("never leaks the dispatch key through a category's own batch route", async () => {
    const niagara = ALL_TOOLS.find((t) => t.name === "niagara")!;
    const probe = "epic_leak_probe";
    (niagara.actions as Record<string, unknown>)[probe] = bp(
      "read",
      "Probe.",
      "probe_method",
      (p) => ({ ...p }),
    );
    try {
      const seen: Array<Record<string, unknown>> = [];
      const ctx = {
        bridge: {
          isConnected: true,
          call: async (_m: string, params: Record<string, unknown>) => { seen.push(params); return {}; },
          getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
          connect: async () => {},
          retargetProject: () => ({}),
        },
        project: {},
      } as never;

      await niagara.handler(ctx, { action: "batch", ops: [{ action: probe }] });

      expect(seen.length, "the probe should have reached the bridge").toBe(1);
      expect(
        Object.prototype.hasOwnProperty.call(seen[0], "action"),
        "A batch route hands each op's bag to that action's mapParams, so it\n"
          + "must strip the dispatch key exactly as the tool handler does. Payload:\n"
          + JSON.stringify(seen[0]),
      ).toBe(false);
    } finally {
      delete (niagara.actions as Record<string, unknown>)[probe];
    }
  });

  // The generator is no longer the only thing standing between a wrapped tool
  // argument and the dispatch parameter: categoryTool re-asserts its own keys
  // after merging a category's, so declaring one cannot replace it.
  it("cannot have its routing parameters replaced by a category's own schema", () => {
    const hostile = categoryTool(
      "probe",
      "Probe.",
      { alpha: bp("read", "Alpha.", "probe_alpha") },
      {
        action: z.string().optional().describe("hostile"),
        timeoutMs: z.string().optional().describe("hostile"),
        select: z.string().optional().describe("hostile"),
        omit: z.string().optional().describe("hostile"),
      },
    );

    for (const key of ["action", "timeoutMs", "select", "omit"]) {
      expect(
        hostile.schema[key].description ?? "",
        `${key} was replaced by the category's own declaration`,
      ).not.toBe("hostile");
    }
    // And the dispatch parameter still enumerates the category's actions.
    expect(JSON.stringify(hostile.schema.action)).toContain("alpha");
  });

  it("documents parameters on every action", () => {
    const undocumented: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [action, spec] of Object.entries(tool.actions)) {
        if (spec.kind === "registry") continue;
        const structured = spec.paramSpec
          || spec.params
          || (spec.kind === "bridge" && spec.epicSchema);
        if (!structured) undocumented.push(`${tool.name}.${action}`);
      }
    }
    expect(
      undocumented,
      "Every action must declare what it takes through a structured source: a\n"
        + "C++ spec (paramSpec), a TypeScript `params` declaration (empty for an\n"
        + "action that takes nothing), or a wrapped engine tool's epicSchema. It is\n"
        + "the only per-action parameter documentation the surface has - the zod\n"
        + "shape is per category, not per action:\n  "
        + undocumented.join("\n  "),
    ).toEqual([]);
  });

  it("agrees with itself about whether a parameter is required", () => {
    // A parameter the description marks required must not be declared with a
    // schema default, which would silently make it optional.
    const contradictions: string[] = [];
    for (const schema of surfaceSchemas()) {
      for (const param of schema.params) {
        if (param.required && param.default !== undefined) {
          contradictions.push(`${schema.tool}.${schema.action}.${param.name}`);
        }
      }
    }
    expect(contradictions).toEqual([]);
  });
});

describe("actionAccepts", () => {
  it("reads the names a TypeScript declaration takes", () => {
    const spec = { ...bp("read", "Probe.", "x"), params: { required: ["alpha"], optional: ["beta"] } };
    expect(actionAccepts(spec)?.takes).toEqual(["alpha", "beta"]);
  });

  it("reads a wrapped engine tool's parameters off its declared schema", () => {
    const spec = {
      ...bp("read", "Probe.", "epic_call_tool"),
      epicTool: { toolset: "T", name: "T.Get" },
      epicSchema: { properties: { widgetBlueprint: { type: "object" }, depth: { type: "integer" } } },
    };
    expect([...(actionAccepts(spec)?.takes ?? [])].sort()).toEqual(["depth", "widgetBlueprint"]);
  });

  it("is undefined for an action that declares nothing", () => {
    expect(actionAccepts(bp("read", "Probe.", "x"))).toBeUndefined();
  });
});

describe("actionSchema", () => {
  it("reads a declared choice as one choice", () => {
    const tool = categoryTool("example", "Example", {
      pick: {
        ...bp("read", "Pick.", "pick"),
        params: { optional: ["actorLabel", "actorPath"], choices: [{ branches: [["actorLabel"], ["actorPath"]] }] },
      },
    }, { actorLabel: z.string().optional(), actorPath: z.string().optional() });
    const schema = actionSchema(tool, "pick");
    expect(schema.alternatives).toEqual([{ branches: [["actorLabel"], ["actorPath"]], required: true }]);
    expect(schema.params.every((p) => p.aliases === undefined)).toBe(true);
  });

  it("preserves nested fields, array items, enums, defaults and required nullable values", () => {
    const tool = categoryTool("example", "Example", {
      inspect: { ...bp("read", "Inspect.", "inspect"), params: { required: ["request"] } },
    }, {
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
    const tool = categoryTool("example", "Example", { inspect: { ...bp("read", "Inspect.", "inspect"), params: { required: ["request"] } } }, { request: nested });
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
    // #1057: a spec'd action reports its C++ spec. `path` is an alias the
    // registry renames to assetPath, so it rides on assetPath, which stays
    // required, rather than turning the two into an optional choice.
    expect(byName.get("assetPath")).toMatchObject({ required: true, type: "string", aliases: ["path"] });
    expect(byName.get("assetPath")?.alternativeGroup).toBeUndefined();
    expect(byName.has("path")).toBe(false);
    expect(byName.get("propertyName")).toMatchObject({ required: true, type: "string" });
    expect(byName.get("value")).toMatchObject({ required: true, type: "any" });
    expect(byName.get("save")).toMatchObject({ required: false, type: "boolean" });
    expect(schema.alternatives).toBeUndefined();
  });

  it("reports a parameter the description names with an ordinary English word", () => {
    // material(set_parameter) needs `value` (or color / texturePath in its
    // place). Reporting the schema without it had an agent call the action
    // with no value at all.
    const material = ALL_TOOLS.find((t) => t.name === "material")!;
    const schema = actionSchema(material, "set_parameter");
    const byName = new Map(schema.params.map((p) => [p.name, p]));
    expect(byName.get("value")?.alternativeGroup).toBe(0);
    expect(schema.alternatives?.[0]).toEqual({ branches: [["value"], ["color"], ["texturePath"]], required: true });
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

  it("points a miss that is not a typo at the native-module route (#1105)", () => {
    const miss = unknownActionMessage("zzzz", "gas", ["sculpt"]);
    expect(miss).toContain("UEMCP::RegisterExternalHandler");
    expect(miss).toContain("handlers:");
    expect(miss).toContain("docs/plugins-native-modules.md");
    expect(miss).toContain("overwritten");
    // A typo gets its spellings, not the plugin lecture.
    const typo = unknownActionMessage("scul", "landscape", ["sculpt"]);
    expect(typo).toContain("Did you mean: sculpt?");
    expect(typo).not.toContain("RegisterExternalHandler");
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
    for (const schema of surfaceSchemas()) {
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

  it("calls arbitrary python unknown, and gates it as a mutation anyway", () => {
    // It used to be labelled `mutate`, which was a guess dressed as a fact:
    // the effect of running a python string is the string. `unknown` is what
    // the declaration can honestly say, and it is gated identically, so the
    // honest label costs nothing at the gate. Same for a console command.
    const editor = ALL_TOOLS.find((t) => t.name === "editor")!;
    for (const action of ["execute_python", "execute_command"]) {
      expect(actionSchema(editor, action).class, action).toBe("unknown");
      expect(requiresExplicitEditor(actionSchema(editor, action).class), action).toBe(true);
    }
  });

  it("says whether an effect was declared or inferred", () => {
    // Everything ue-mcp declares is a person's answer. `inferred` is reserved
    // for Epic's runtime-injected tools and a silent plugin manifest, so a
    // caller building an approval policy can tell the two apart.
    const editor = ALL_TOOLS.find((t) => t.name === "editor")!;
    expect(actionSchema(editor, "execute_python").classSource).toBe("declared");
    expect(actionSchema(editor, "get_viewport").classSource).toBe("declared");
  });
});
