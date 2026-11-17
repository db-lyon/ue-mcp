/**
 * Every action names its parameters through a structured source, and
 * `describe_action` reports every one of them.
 *
 * `tests/unit/action-schema.test.ts` holds the other direction: a name an
 * action declares must be declared by its category. That catches a typo in a
 * declaration, and nothing else. It is blind to a parameter the action
 * declares, the category declares and the handler requires, that
 * `describe_action` then leaves out - an agent obeying the reported schema
 * makes a call the handler rejects, or worse, one the handler accepts with an
 * empty value.
 *
 * The structured sources are a C++ spec (`paramSpec`), a TypeScript `params`
 * declaration, or a wrapped engine tool's `epicSchema`. Prose in a description
 * is never read for parameters.
 */
import { describe, it, expect } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { actionAccepts, actionSchema, ROUTING_PARAMS } from "../../src/surface/action-schema.js";

describe("the structured parameter source", () => {
  it("exists for every action that is not a registry lookup", () => {
    const missing: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [action, spec] of Object.entries(tool.actions)) {
        if (spec.kind === "registry") continue;
        const structured = spec.paramSpec || spec.params || (spec.kind === "bridge" && spec.epicSchema);
        if (!structured) missing.push(`${tool.name}.${action}`);
      }
    }
    expect(
      missing,
      "These actions declare no paramSpec, params or epicSchema, so nothing says what they take:\n  "
        + missing.join("\n  "),
    ).toEqual([]);
  });

  it("is reported whole by describe_action, aliases riding on the parameter they name", () => {
    const offenders: string[] = [];
    for (const tool of ALL_TOOLS) {
      const declared = new Set(Object.keys(tool.schema));
      for (const [action, spec] of Object.entries(tool.actions)) {
        const accepted = actionAccepts(spec);
        if (!accepted) continue;
        const params = actionSchema(tool, action).params;
        const reported = new Set(params.flatMap((p) => [p.name, ...(p.aliases ?? [])]));
        const dropped = accepted.takes.filter(
          (name) => declared.has(name) && !ROUTING_PARAMS.has(name) && !reported.has(name),
        );
        if (dropped.length > 0) offenders.push(`${tool.name}.${action}: dropped ${dropped.join(", ")}`);
      }
    }
    expect(
      offenders,
      "These actions declare a parameter the category accepts but describe_action\n"
        + "does not report. It is a field an agent will omit:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });
});
