import { describe, it, expect } from "vitest";
import type { InvocationEntry } from "../../src/invocation-tracker.js";
import {
  createInvocationProjector,
  parseKant,
  compose,
  select,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/emit.js";

function projectAndCompose(entries: InvocationEntry[]) {
  const proj = createInvocationProjector(() => entries);
  const parsed = parseKant(serializeFragment(proj.project(undefined as never)), "proj");
  return compose([{ priority: 1, fragment: parsed }]);
}

describe("ontology: InvocationProjector", () => {
  it("emits an empty Log when no invocations recorded", () => {
    const view = projectAndCompose([]);
    const log = view.root.children!.Audit.children!.Invocations.children!.Log;
    expect(log.fields!.entryCount).toBe(0);
    expect(log.fields!.okCount).toBe(0);
  });

  it("aggregates ok/error/requires_unmet counters and emits per-entry points", () => {
    const view = projectAndCompose([
      { sequence: 1, tool: "blueprint", action: "list_variables", status: "ok", durationMs: 5, timestamp: "2026-04-19T10:00:00Z" },
      { sequence: 2, tool: "gas", action: "create_ability", status: "requires_unmet", durationMs: 1, timestamp: "2026-04-19T10:00:01Z", errorCode: "REQUIRES_UNMET", errorSnippet: "missing GameplayAbilities" },
      { sequence: 3, tool: "editor", action: "execute_python", status: "error", durationMs: 12, timestamp: "2026-04-19T10:00:02Z", errorCode: "BRIDGE_TIMEOUT", errorSnippet: "timeout after 30s" },
    ]);
    const log = view.root.children!.Audit.children!.Invocations.children!.Log;
    expect(log.fields!.entryCount).toBe(3);
    expect(log.fields!.okCount).toBe(1);
    expect(log.fields!.requiresUnmetCount).toBe(1);
    expect(log.fields!.errorCount).toBe(1);

    const e2 = log.children!.entry_000002;
    expect((e2.fields!.status as { marker?: string }).marker).toBe("requires_unmet");
    expect(e2.fields!.tool).toBe("gas");
    expect(e2.fields!.errorCode).toBe("REQUIRES_UNMET");
  });

  it("selector filters errors via @status=error predicate", () => {
    const view = projectAndCompose([
      { sequence: 1, tool: "a", action: "one", status: "ok", durationMs: 1, timestamp: "t1" },
      { sequence: 2, tool: "b", action: "two", status: "error", durationMs: 2, timestamp: "t2" },
      { sequence: 3, tool: "c", action: "three", status: "error", durationMs: 3, timestamp: "t3" },
    ]);
    const errors = select(view.root, "/UE/Audit/Invocations/Log/*@status=error");
    expect(errors.map((m) => m.point.fields!.tool).sort()).toEqual(["b", "c"]);
  });
});
