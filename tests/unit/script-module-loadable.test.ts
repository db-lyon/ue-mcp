/**
 * A script a test imports has to be importable.
 *
 * `scripts/*.mjs` files are dual-purpose: run as a CLI with `node scripts/x.mjs`,
 * and imported by a test that asserts on what they compute. A `#!/usr/bin/env node`
 * line is fine for the first job and fatal for the second, because Vite hands the
 * file to the ESM loader without stripping it and the module fails to parse. The
 * importing suite then fails to COLLECT, which reports zero assertions rather than
 * a failure anyone reads as one.
 *
 * That is how the handler-conventions ratchet sat inert while its numbers were
 * being quoted in reviews. The failure is silent in the specific way that matters:
 * the suite is not red on an assertion, it is absent.
 *
 * So this checks the property directly rather than trusting a convention: every
 * scripts/*.mjs any test imports is really imported here, which is the same thing
 * the importing suite does and fails the same way if it ever stops working.
 */
import { describe, it, expect } from "vitest";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

const ROOT = join(import.meta.dirname, "..", "..");
const SCRIPTS = join(ROOT, "scripts");

/** Every scripts/*.mjs named by an import in any test file. */
function importedByTests(): string[] {
  const named = new Set<string>();
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name === "node_modules" || entry.name === "ue_mcp") continue;
        walk(path);
        continue;
      }
      if (!entry.name.endsWith(".test.ts") && !entry.name.endsWith(".ts")) continue;
      const source = readFileSync(path, "utf8");
      for (const m of source.matchAll(/from\s+"[^"]*\/scripts\/([\w.-]+\.mjs)"/g)) named.add(m[1]);
    }
  };
  walk(join(ROOT, "tests"));
  return [...named].sort();
}

describe("scripts a test imports", () => {
  const names = importedByTests();

  it("finds the imports at all, so an empty pass cannot look like a green one", () => {
    expect(names.length).toBeGreaterThan(0);
  });

  it("carries no shebang, which makes the module unparseable under Vite", () => {
    const offenders = names.filter((n) => readFileSync(join(SCRIPTS, n), "utf8").startsWith("#!"));
    expect(
      offenders,
      "These scripts are imported by a test and start with a shebang, so the importing suite "
        + "will fail to collect and its assertions will not run:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });

  it("imports cleanly", async () => {
    for (const name of names) {
      const url = new URL("file://" + join(SCRIPTS, name).replace(/\\/g, "/"));
      await expect(import(/* @vite-ignore */ url.href), `${name} failed to import`).resolves.toBeTruthy();
    }
  });
});
