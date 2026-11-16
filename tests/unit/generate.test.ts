/**
 * `npm run generate` (scripts/generate.mjs) is the one regeneration command.
 * These hold its step list to the scripts it runs and to the order the outputs
 * depend on; the `generated` release gate runs it in CI with --check.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { STEPS, changedBetween, planSteps } from "../../scripts/generate.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, "package.json"), "utf8")) as { scripts: Record<string, string> };
const at = (name: string) => STEPS.findIndex((s: { name: string }) => s.name === name);

describe("npm run generate", () => {
  it("is the package's generate script", () => {
    expect(pkg.scripts.generate).toBe("node scripts/generate.mjs");
  });

  it("runs each step from a script that exists, and names an npm script that runs it alone", () => {
    for (const step of STEPS) {
      const script = step.cmd.find((a: string) => a.startsWith("scripts/"));
      expect(script && fs.existsSync(path.join(ROOT, script)), `${step.name}: ${script}`).toBe(true);
      expect(pkg.scripts[step.npm.split(" ")[0]], `${step.name}: npm run ${step.npm}`).toBeDefined();
    }
  });

  it("orders the steps by what each one reads", () => {
    const before = (a: string, b: string) => expect(at(a), `${a} runs before ${b}`).toBeLessThan(at(b));
    before("specs:record", "specs:generate");
    before("epic:record", "epic:generate");
    before("epic:generate", "epic:wire");
    before("epic:wire", "epic:docs");
    for (const surface of ["specs:generate", "epic:wire"]) {
      before(surface, "metadata");
      before(surface, "golden");
      before(surface, "coverage:behavioural");
    }
    before("metadata", "stamp:package");
    before("stamp:package", "golden");
  });

  it("skips exactly the steps that record from a live editor unless asked", () => {
    const editorSteps = STEPS.filter((s: { editor?: boolean }) => s.editor).map((s: { name: string }) => s.name);
    expect(editorSteps).toEqual(["specs:record", "epic:record", "golden:connected"]);
    const plan = planSteps();
    expect(plan.skipped.map((s: { name: string }) => s.name)).toEqual(editorSteps);
    expect(plan.run.some((s: { editor?: boolean }) => s.editor)).toBe(false);
    expect(planSteps({ editor: true }).skipped).toEqual([]);
    expect(planSteps({ only: ["golden", "metadata"] }).run.map((s: { name: string }) => s.name)).toEqual(["metadata", "golden"]);
    expect(() => planSteps({ only: ["nope"] })).toThrow(/No such step: nope/);
  });

  it("reports a file already dirty before the run when a step changes it again", () => {
    const before = new Map([["docs/a.md", "1"], ["docs/b.md", "2"]]);
    const after = new Map([["docs/a.md", "1"], ["docs/b.md", "3"], ["docs/c.md", "4"]]);
    expect(changedBetween(before, after)).toEqual(["docs/b.md", "docs/c.md"]);
  });
});
