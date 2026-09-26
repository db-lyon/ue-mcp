/**
 * An in-process action's declared options are the source of its describe
 * schema, its signature and its task's option schema. Its description keeps a
 * `Params:` clause, recorded by the golden baseline, and these tests hold the
 * two to each other: the clause names every declared parameter, marks exactly
 * the optional ones with `?`, and says `none` only when there are none.
 *
 * The reverse direction, a declared key written in the clause but missing from
 * the declaration, is held by action-schema-clause-coverage.test.ts.
 */
import { describe, expect, it } from "vitest";
import { TaskRegistry } from "@db-lyon/flowkit";
import { ALL_TOOLS } from "../../src/tools.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import type { HandlerActionSpec } from "../../src/core/types.js";

/**
 * Handlers that declare nothing yet. Each is being rebuilt as a composite with
 * its own option schema; the entry goes when that lands, and a name here that
 * is no longer an undeclared handler fails the test.
 */
const UNDECLARED = new Set(["blueprint.author", "niagara.batch", "asset.search"]);

const flowTool = createFlowTool(new TaskRegistry(), () => ({ tasks: {}, flows: {} }) as never);

function handlers(): Array<{ key: string; spec: HandlerActionSpec }> {
  const out: Array<{ key: string; spec: HandlerActionSpec }> = [];
  for (const tool of [...ALL_TOOLS, flowTool]) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      if (spec.kind === "handler") out.push({ key: `${tool.name}.${action}`, spec });
    }
  }
  return out;
}

/** The clause as the description writes it, up to where the prose resumes. */
function clauseOf(description: string): string | undefined {
  const at = description.search(/\bParams:/);
  if (at < 0) return undefined;
  const text = description.slice(at + "Params:".length);
  let depth = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if ("([{".includes(c)) depth++;
    else if (")]}".includes(c)) depth = Math.max(0, depth - 1);
    else if (depth === 0 && /^(\.\s|Returns\b)/.test(text.slice(i))) return text.slice(0, i);
  }
  return text;
}

/** Where `name` stands in the clause as a parameter, and whether it is marked optional there. */
function marking(clause: string, name: string): "absent" | "required" | "optional" {
  const re = new RegExp(String.raw`(?<![\w.])${name}(\[\])?(\s*\?)?(?![\w])`, "g");
  let seen = false;
  for (const m of clause.matchAll(re)) {
    seen = true;
    if (m[2]) return "optional";
  }
  return seen ? "required" : "absent";
}

describe("declared handler options", () => {
  it("are declared by every in-process action", () => {
    const missing = handlers().filter(({ key, spec }) => !spec.options && !UNDECLARED.has(key)).map(({ key }) => key);
    expect(missing, "Declare options: { params: [...] } on these actions").toEqual([]);
    const stale = [...UNDECLARED].filter((key) => !handlers().some((h) => h.key === key && !h.spec.options));
    expect(stale, "No longer an undeclared handler; drop it from UNDECLARED").toEqual([]);
  });

  it("agree with the Params clause the description carries", () => {
    const offenders: string[] = [];
    for (const { key, spec } of handlers()) {
      if (!spec.options) continue;
      const clause = clauseOf(spec.description ?? "");
      if (clause === undefined) {
        offenders.push(`${key}: no Params clause`);
        continue;
      }
      const choiceMembers = new Set((spec.options.choices ?? []).flatMap((c) => c.branches.flat()));
      if (spec.options.params.length === 0 && !/^\s*none\b/.test(clause)) {
        offenders.push(`${key}: declares no parameters, clause is not 'none'`);
      }
      for (const raw of spec.options.params) {
        const optional = raw.endsWith("?");
        const name = optional ? raw.slice(0, -1) : raw;
        const seen = marking(clause, name);
        if (seen === "absent") offenders.push(`${key}: declares ${name}, clause does not name it`);
        else if (!choiceMembers.has(name) && (seen === "optional") !== optional) {
          offenders.push(`${key}: ${name} is ${optional ? "optional" : "required"}, clause marks it ${seen}`);
        }
      }
    }
    expect(offenders).toEqual([]);
  });
});
