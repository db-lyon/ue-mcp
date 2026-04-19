import { describe, it, expect } from "vitest";
import type { WorkaroundEntry } from "../../src/workaround-tracker.js";
import {
  createWorkaroundProjector,
  parse,
  compose,
  select,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/index.js";

function projectAndCompose(entries: WorkaroundEntry[]) {
  const proj = createWorkaroundProjector(() => entries);
  const text = serializeFragment(proj.project(undefined as never));
  const parsed = parse(text, "projected");
  return compose([{ priority: 1, fragment: parsed }]);
}

describe("ontology: WorkaroundProjector", () => {
  it("emits an empty Log when no workarounds recorded", () => {
    const view = projectAndCompose([]);
    const log = view.root.children!.Audit.children!.Workarounds.children!.Log;
    expect(log.fields!.entryCount).toBe(0);
    expect(log.children ?? {}).toEqual({});
  });

  it("records one entry per workaround with sequence-padded keys", () => {
    const view = projectAndCompose([
      { code: "print('one')", timestamp: "2026-04-19T10:00:00Z", resultSnippet: "ok" },
      { code: "print('two')\nprint('also')", timestamp: "2026-04-19T10:01:00Z" },
    ]);
    const log = view.root.children!.Audit.children!.Workarounds.children!.Log;
    expect(log.fields!.entryCount).toBe(2);
    expect(Object.keys(log.children!).sort()).toEqual(["entry_0001", "entry_0002"]);

    const first = log.children!.entry_0001;
    expect(first.fields!.sequence).toBe(1);
    expect(first.fields!.firstLine).toBe("print('one')");
    expect(first.fields!.resultSnippet).toBe("ok");

    const second = log.children!.entry_0002;
    expect(second.fields!.firstLine).toBe("print('two')");
    expect(second.fields!.resultSnippet).toBeUndefined();
  });

  it("truncates long first lines and result snippets", () => {
    const longCode = `x = ${"a".repeat(500)}`;
    const longResult = "b".repeat(500);
    const view = projectAndCompose([
      { code: longCode, timestamp: "2026-04-19T10:00:00Z", resultSnippet: longResult },
    ]);
    const entry = view.root.children!.Audit.children!.Workarounds.children!.Log.children!.entry_0001;
    expect((entry.fields!.firstLine as string).length).toBeLessThanOrEqual(140);
    expect((entry.fields!.firstLine as string).endsWith("...")).toBe(true);
    expect((entry.fields!.resultSnippet as string).length).toBeLessThanOrEqual(200);
  });

  it("entries are queryable by path selector", () => {
    const view = projectAndCompose([
      { code: "one", timestamp: "2026-04-19T10:00:00Z" },
      { code: "two", timestamp: "2026-04-19T10:01:00Z" },
      { code: "three", timestamp: "2026-04-19T10:02:00Z" },
    ]);
    const all = select(view.root, "/UE/Audit/Workarounds/Log/*");
    expect(all).toHaveLength(3);

    const numeric = select(view.root, "/UE/Audit/Workarounds/Log/*@sequence>=2");
    expect(numeric.map((m) => m.point.fields!.firstLine).sort()).toEqual(["three", "two"]);
  });
});
