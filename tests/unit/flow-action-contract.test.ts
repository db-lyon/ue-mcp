/**
 * The flow tool's parameter contract.
 *
 * `tests/unit/action-schema.test.ts` holds this property over `ALL_TOOLS`, and
 * the flow tool is not in `ALL_TOOLS`: it is built per server, closing over a
 * task registry and a config source, so the module that declares the category
 * list cannot hold it. That exemption is exactly how the flow tool would come
 * to document a parameter it never declares.
 *
 * The failure it prevents is the worst shape this server has. A category's zod
 * shape is one flat bag shared by every action in it, and the MCP layer strips
 * keys the bag does not declare: a documented-but-undeclared parameter arrives
 * as `undefined` and the call returns an ordinary success for a mutation that
 * never happened.
 *
 * Every flow action must therefore carry a `Params:` clause and declare every
 * name in it, on the same terms as the 24 categories.
 */
import { describe, expect, it } from "vitest";
import { allActionSchemas } from "../../src/action-schema.js";
import { classifyActionClass } from "../../src/action-class.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import type { FlowConfig } from "../../src/flow/schema.js";

const EMPTY_CONFIG = { flows: {}, tasks: {} } as unknown as FlowConfig;
const flowTool = createFlowTool({} as never, () => EMPTY_CONFIG);

describe("flow action contract", () => {
  it("declares every parameter it documents or reads", () => {
    const offenders = allActionSchemas([flowTool])
      .filter((a) => a.drift.length > 0)
      .map((a) => `${a.tool}.${a.action}: ${a.drift.join(", ")}`);

    expect(
      offenders,
      "These flow actions document or read a parameter the tool never declares.\n"
        + "The MCP layer strips undeclared keys, so passing one has NO effect and the\n"
        + "call still reports success. Declare it in the flow tool's schema, or stop\n"
        + "documenting it:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });

  it("documents parameters on every action", () => {
    const undocumented = Object.entries(flowTool.actions)
      .filter(([, spec]) => !/\bParams:/.test(spec.description ?? ""))
      .map(([name]) => `flow.${name}`);
    expect(undocumented).toEqual([]);
  });

  it("advertises every action it dispatches, so the enum and the map agree", () => {
    const advertised = allActionSchemas([flowTool]).map((a) => a.action).sort();
    expect(advertised).toEqual(Object.keys(flowTool.actions).sort());
  });

  it("classifies every action as a read or a mutation, never by accident", () => {
    const unresolved = Object.keys(flowTool.actions).filter(
      (action) => classifyActionClass("flow", action).source === "unresolved",
    );
    expect(
      unresolved,
      "With more than one editor registered, an unclassified action is gated like a\n"
        + "mutation on a guess rather than a decision. Add each of these to OVERRIDES in\n"
        + "src/action-class.ts with the reason:\n  " + unresolved.join("\n  "),
    ).toEqual([]);
  });

  it("keeps the journal and skill actions distinct from the run/plan/list three", () => {
    const names = Object.keys(flowTool.actions);
    expect(names).toContain("run");
    expect(names.filter((n) => n.startsWith("journal_")).length).toBeGreaterThan(0);
    expect(names.filter((n) => n.startsWith("skill_")).length).toBeGreaterThan(0);
    // Every name is unique by construction, but a collision between a journal
    // action and one of the three would silently shadow it.
    expect(new Set(names).size).toEqual(names.length);
  });
});
