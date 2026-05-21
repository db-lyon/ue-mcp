import { describe, it, expect } from "vitest";
import yaml from "js-yaml";
import { dumpYaml } from "../../src/yaml-dump.js";

describe("dumpYaml", () => {
  it("unquotes integer-string keys at the top level", () => {
    // JS objects always have string keys, so reading {1: ...} from YAML
    // gives us {"1": ...}. js-yaml.dump quotes those as '1':; dumpYaml
    // strips the quotes back so the result matches the original.
    const obj = { steps: { 1: "a", 2: "b", 10: "c" } };
    const out = dumpYaml(obj);
    expect(out).toContain("1: a");
    expect(out).toContain("2: b");
    expect(out).toContain("10: c");
    expect(out).not.toContain("'1':");
    expect(out).not.toContain("'2':");
    expect(out).not.toContain("'10':");
  });

  it("round-trips a flowkit-style nested step block", () => {
    const original = {
      "ue-mcp": { version: 1 },
      flows: {
        test_flow: {
          description: "demo",
          steps: {
            1: { task: "level.get_outliner" },
            2: { task: "editor.capture_screenshot" },
          },
        },
      },
    };
    const dumped = dumpYaml(original);
    expect(dumped).toMatch(/^\s+1:\s*$/m);
    expect(dumped).toMatch(/^\s+2:\s*$/m);
    expect(dumped).not.toContain("'1':");
    expect(dumped).not.toContain("'2':");
    // The result must still parse back to the same shape.
    const back = yaml.load(dumped) as typeof original;
    expect(back.flows.test_flow.steps).toEqual({
      "1": { task: "level.get_outliner" },
      "2": { task: "editor.capture_screenshot" },
    });
  });

  it("does NOT damage a string value that happens to contain '1': inside it", () => {
    // The regex is anchored at line start (with optional whitespace / list
    // bullet), so a quoted-key-looking substring inside a YAML scalar value
    // is safe. Round-trip the value to confirm the unquote pass didn't
    // accidentally rewrite it.
    const obj = { message: "'1': not a key" };
    const back = yaml.load(dumpYaml(obj)) as typeof obj;
    expect(back.message).toBe("'1': not a key");
  });

  it("handles list-item map keys: `- '1':` → `- 1:`", () => {
    // YAML lists where each item is a single-pair map keyed by an integer.
    const obj = [{ 1: "a" }, { 2: "b" }];
    const out = dumpYaml(obj);
    expect(out).toMatch(/^\s*-\s+1: a$/m);
    expect(out).toMatch(/^\s*-\s+2: b$/m);
  });

  it("leaves non-integer-looking string keys alone", () => {
    const obj = { "test-flow": { description: "x" } };
    const out = dumpYaml(obj);
    expect(out).toContain("test-flow:");
  });
});
