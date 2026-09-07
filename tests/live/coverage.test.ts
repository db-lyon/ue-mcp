/**
 * The 7.3 matrix has to describe reality (#817).
 *
 * `matrix.ts` claims, case by case, where the assertion lives. A claim about
 * another file is worth exactly as much as the check that it is still true, so
 * every reference is resolved here: the file has to exist and it has to
 * contain the test it names. Renaming a test therefore breaks this list rather
 * than quietly emptying a row of the matrix.
 *
 * This is the one file in the suite that needs no editor. It is the inventory,
 * not an assertion about the engine.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { MATRIX_CASES, SINGLE_EDITOR_CHANGES, type CoverageRef, type MatrixCase } from "./matrix.js";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

const ALL_CASES: MatrixCase[] = [...MATRIX_CASES, ...SINGLE_EDITOR_CHANGES];

function read(file: string): string {
  return fs.readFileSync(path.join(REPO_ROOT, file), "utf-8");
}

function describeRef(ref: CoverageRef): string {
  switch (ref.kind) {
    case "live":
    case "engine-free":
      return `${ref.kind}: ${ref.file} :: ${ref.title}`;
    case "cpp":
      return `cpp: ${ref.reason}`;
    case "pending":
      return `pending ${ref.planItem}: ${ref.reason}`;
  }
}

describe("the 7.3 matrix", () => {
  it("gives every case somewhere its assertion lives", () => {
    for (const testCase of ALL_CASES) {
      expect(testCase.coverage.length, `case '${testCase.id}' claims no coverage at all`).toBeGreaterThan(0);
    }
  });

  it("uses ids that are unique, so a report can name a row", () => {
    const ids = ALL_CASES.map((c) => c.id);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it("names files that exist", () => {
    for (const testCase of ALL_CASES) {
      for (const ref of testCase.coverage) {
        if (ref.kind !== "live" && ref.kind !== "engine-free") continue;
        const full = path.join(REPO_ROOT, ref.file);
        expect(fs.existsSync(full), `case '${testCase.id}' points at ${ref.file}, which does not exist`).toBe(true);
      }
    }
  });

  it("names tests those files actually contain", () => {
    const cache = new Map<string, string>();
    for (const testCase of ALL_CASES) {
      for (const ref of testCase.coverage) {
        if (ref.kind !== "live" && ref.kind !== "engine-free") continue;
        if (!cache.has(ref.file)) cache.set(ref.file, read(ref.file));
        expect(
          cache.get(ref.file)!.includes(ref.title),
          `case '${testCase.id}' points at "${ref.title}" in ${ref.file}, which no longer contains it`,
        ).toBe(true);
      }
    }
  });

  it("puts live references in the live tests and engine-free ones outside it", () => {
    for (const testCase of ALL_CASES) {
      for (const ref of testCase.coverage) {
        if (ref.kind === "live") {
          expect(ref.file.startsWith("tests/live/"), `case '${testCase.id}': ${ref.file} is not in the live tests`).toBe(true);
        }
        if (ref.kind === "engine-free") {
          expect(
            ref.file.startsWith("tests/unit/") || ref.file.startsWith("tests/multi-editor/"),
            `case '${testCase.id}': ${ref.file} is claimed to need no engine but is not in an engine-free tests`,
          ).toBe(true);
        }
      }
    }
  });

  it("says which plan item ships anything still pending", () => {
    for (const testCase of ALL_CASES) {
      for (const ref of testCase.coverage) {
        if (ref.kind !== "pending") continue;
        expect(ref.planItem, `case '${testCase.id}' is pending on nothing in particular`).toMatch(/^\d+\.\d+$/);
        expect(ref.reason.length).toBeGreaterThan(20);
      }
    }
  });

  it("prints where every case stands", () => {
    const lines: string[] = [];
    let live = 0;
    let engineFree = 0;
    let cpp = 0;
    let pending = 0;
    for (const testCase of ALL_CASES) {
      lines.push(`${testCase.id}: ${testCase.text}`);
      for (const ref of testCase.coverage) {
        lines.push(`    ${describeRef(ref)}`);
        if (ref.kind === "live") live++;
        else if (ref.kind === "engine-free") engineFree++;
        else if (ref.kind === "cpp") cpp++;
        else pending++;
      }
    }
    lines.push(
      `\n${ALL_CASES.length} cases: ${live} live assertions, ${engineFree} referenced engine-free, ` +
        `${cpp} owned by the plugin's automation suite, ${pending} pending on unshipped work.`,
    );
    console.log(lines.join("\n"));
    expect(live).toBeGreaterThan(0);
  });
});
