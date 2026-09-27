/**
 * blueprint.author answers exactly as the handler it replaced: that handler,
 * as it stood before the move to a composite, is kept below.
 */
import { describe, expect, it } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { callAction } from "../../src/flow/action-call.js";
import { expectParity, type LegacyHandler } from "../helpers/composite-parity.js";

const legacyAuthor: LegacyHandler = async (ctx, p) => {
  const assetPath = p.assetPath as string;
  if (!assetPath) throw new Error("Missing 'assetPath'");
  const steps: Array<{ step: string; target: string; ok: boolean; result?: unknown; error?: string }> = [];
  const run = async (step: string, target: string, action: string, params: Record<string, unknown>) => {
    const defined = Object.fromEntries(Object.entries(params).filter(([, v]) => v !== undefined));
    const r = await callAction(ctx, `blueprint.${action}`, defined, blueprintTool);
    steps.push({
      step,
      target,
      ok: r.success,
      ...(r.data !== undefined ? { result: r.data } : {}),
      ...(r.success ? {} : { error: r.error?.message ?? `blueprint.${action} failed` }),
    });
    return r;
  };
  let created = false;
  if (p.parentClass) {
    const r = await run("create", assetPath, "create", { assetPath, parentClass: p.parentClass });
    created = r.success && (r.data as Record<string, unknown> | undefined)?.created !== false;
  }
  for (const c of (p.components as Array<Record<string, unknown>> ?? [])) {
    await run("add_component", String(c.componentClass ?? ""), "add_component", { assetPath, componentClass: c.componentClass, componentName: c.componentName ?? c.componentClass, parentComponent: c.parentComponent, childActorClass: c.childActorClass });
  }
  for (const v of (p.variables as Array<Record<string, unknown>> ?? [])) {
    await run("add_variable", String(v.name ?? ""), "add_variable", { assetPath, name: v.name, varType: v.varType ?? v.type });
  }
  for (const f of (p.functions as Array<Record<string, unknown>> ?? [])) {
    await run("create_function", String(f.functionName ?? ""), "create_function", { assetPath, functionName: f.functionName });
  }
  if ((p.compile ?? true) !== false) {
    await run("compile", assetPath, "compile", { assetPath });
  }
  const failed = steps.filter(s => !s.ok);
  return { assetPath, created, stepCount: steps.length, failedCount: failed.length, ok: failed.length === 0, steps };
};

const door = {
  assetPath: "/Game/BP_Door",
  parentClass: "Actor",
  components: [{ componentClass: "StaticMeshComponent" }, { componentClass: "BoxComponent", componentName: "Trigger", parentComponent: "StaticMeshComponent" }],
  variables: [{ name: "Open", varType: "bool" }, { name: "Speed", type: "float" }],
  functions: [{ functionName: "Toggle" }],
};

const parity = (answer: Parameters<typeof expectParity>[3], params: Record<string, unknown>) =>
  expectParity(blueprintTool, "author", legacyAuthor, answer, params);

describe("blueprint.author answers as the handler did", () => {
  it("on a clean run", async () => {
    const out = await parity(() => ({ success: true, created: true }), door);
    expect(out.data).toMatchObject({ created: true, stepCount: 7, failedCount: 0, ok: true });
  });

  it("when a child answers success:false, and carries on", async () => {
    const out = await parity((m) => (m === "add_variable" ? { success: false, error: "Variable already exists" } : { success: true }), door);
    expect(out.data).toMatchObject({ stepCount: 7, failedCount: 2, ok: false });
  });

  it("when a child throws, and when a failing child carries a rollback", async () => {
    await parity((m) => {
      if (m === "add_component") throw new Error("socket closed");
      if (m === "create_function") return { success: false, error: "bad name", rollback: { method: "delete_function", payload: { name: "Toggle" } } };
      return { success: true };
    }, door);
  });

  it("when the Blueprint already existed, without a parent, and without compiling", async () => {
    await parity(() => ({ success: true, created: false, existed: true }), door);
    await parity(() => ({ success: true }), { assetPath: "/Game/BP_X", variables: [{ name: "A", varType: "int" }] });
    await parity(() => ({ success: true }), { ...door, compile: false, select: ["stepCount"] });
  });

  it("when there is no asset to author", async () => {
    await parity(() => ({ success: true }), { components: [] });
  });
});
