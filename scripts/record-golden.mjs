#!/usr/bin/env node
/**
 * Re-record a golden baseline (#817, plan item 1.10).
 *
 *     npm run golden:record                 the editor-down half
 *     npm run golden:record -- --connected   the editor-connected half
 *
 * Deliberately the same code path each guard uses: it runs the guard test with
 * UE_MCP_RECORD_GOLDEN=1, which makes it write the baseline instead of
 * asserting against it. A separate recorder would be a second implementation
 * of the surface capture, and the baseline would then only prove the two
 * implementations agree with each other.
 *
 * The connected half goes through scripts/live-tests.mjs, so it gets the same
 * preflight as the rest of the live tests: the editor is found, proved to have
 * tests/ue_mcp open, and named before anything is recorded from it.
 *
 * Review the resulting diff before committing. It is the contract every client
 * sees at startup.
 */
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import * as path from "node:path";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const connected = process.argv.slice(2).includes("--connected");

const result = connected
  ? spawnSync(
      process.execPath,
      [
        path.join(repoRoot, "scripts", "live-tests.mjs"),
        "--record-golden",
        "--only",
        "tests/live/golden-connected.test.ts",
      ],
      { cwd: repoRoot, stdio: "inherit", env: process.env },
    )
  : spawnSync(
      process.execPath,
      [path.join(repoRoot, "node_modules", "vitest", "vitest.mjs"), "run", "tests/unit/golden-editor-down.test.ts"],
      {
        cwd: repoRoot,
        stdio: "inherit",
        env: { ...process.env, UE_MCP_RECORD_GOLDEN: "1" },
      },
    );

if (result.error) {
  console.error(`[golden] failed to run vitest: ${result.error.message}`);
  process.exit(1);
}
process.exit(result.status ?? 1);
