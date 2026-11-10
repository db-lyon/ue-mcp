/**
 * select/omit on the routes that did not project the way a category action
 * does: a directive response (execute_python's workaround instruction around
 * its result) and the flow tool. The directive envelope is never filtered,
 * and the projection narrows the result inside it.
 */
import { describe, expect, it } from "vitest";
import { finishCall, prepareCall } from "../../src/dispatch/call-pipeline.js";
import { directive, isDirectiveResponse } from "../../src/core/directive.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";

describe("select on a directive response", () => {
  const wrapped = directive("File a feedback issue for this workaround.", { output: "ok", stdout: ["a", "b"], rollback: { kind: "x" } }, {
    kind: "workaround.feedback",
    requiredActions: ["feedback(submit)"],
  });

  it("narrows the wrapped result and keeps the directive", () => {
    const pipeline = prepareCall({ select: "output" });
    const out = finishCall(wrapped, pipeline);
    expect(isDirectiveResponse(out)).toBe(true);
    expect(out).toMatchObject({ directive: wrapped.directive, machine: wrapped.machine, result: { output: "ok" } });
    expect((out as { result: Record<string, unknown> }).result).toEqual({ output: "ok" });
  });

  it("omits inside the result and reports a path that matched nothing there", () => {
    const out = finishCall(wrapped, prepareCall({ omit: ["stdout", "nope"] })) as { result: Record<string, unknown> };
    expect(out.result.stdout).toBeUndefined();
    expect(out.result.output).toBe("ok");
    expect(out.result.fieldsNotFound).toMatchObject({ paths: ["nope"] });
  });

  it("passes a directive through untouched without a selection", () => {
    expect(finishCall(wrapped, prepareCall({}))).toEqual(wrapped);
  });
});

describe("select on the flow tool", () => {
  it("advertises select and omit, and narrows what an action answers", async () => {
    const tool = createFlowTool({} as never, () => ({ flows: { a: { description: "A", steps: { 1: { task: "shell" } } }, b: { steps: {} } } }) as never);
    expect(tool.schema.select).toBeDefined();
    expect(tool.schema.omit).toBeDefined();
    const all = await tool.handler!({} as never, { action: "list" }) as Record<string, unknown>;
    expect(all.flowCount).toBe(2);
    const names = await tool.handler!({} as never, { action: "list", select: "flows.name" });
    expect(names).toEqual({ flows: [{ name: "a" }, { name: "b" }] });
    const dropped = await tool.handler!({} as never, { action: "list", omit: "flows" });
    expect(dropped).toEqual({ flowCount: 2 });
  });
});
