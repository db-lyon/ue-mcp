/**
 * Every relative module a test mocks exists.
 *
 * vi.mock of a path that resolves to nothing is not an error: the mock is
 * registered for a module nobody imports, and the test runs against the real
 * one. A moved module silently disarms every mock of it, so this fails first.
 */
import { describe, it, expect } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";

const TESTS = path.resolve(import.meta.dirname, "..");
const MOCK = /\bvi\.(?:mock|doMock|unmock|importActual)\s*(?:<[^>]*>)?\(\s*(["'`])(\.{1,2}\/[^"'`]+)\1/g;

function testFiles(dir: string): string[] {
  const out: string[] = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === "node_modules" || entry.name === "ue_mcp") continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...testFiles(full));
    else if (/\.(ts|mts|mjs)$/.test(entry.name)) out.push(full);
  }
  return out;
}

function resolves(fromFile: string, spec: string): boolean {
  const base = path.resolve(path.dirname(fromFile), spec);
  const candidates = /\.js$/.test(spec) ? [base.replace(/\.js$/, ".ts"), base] : [base, `${base}.ts`, `${base}.js`];
  return candidates.some((c) => fs.existsSync(c));
}

describe("vi.mock paths", () => {
  it("name a module that exists", () => {
    const missing: string[] = [];
    let seen = 0;
    for (const file of testFiles(TESTS)) {
      const src = fs.readFileSync(file, "utf8");
      for (const m of src.matchAll(MOCK)) {
        seen++;
        if (!resolves(file, m[2])) missing.push(`${path.relative(TESTS, file)}: ${m[2]}`);
      }
    }
    expect(seen).toBeGreaterThan(0);
    expect(missing).toEqual([]);
  });
});
