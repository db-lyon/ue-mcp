#!/usr/bin/env node
/**
 * Re-record the editor-down golden baseline (#817, plan item 1.10).
 *
 * Deliberately the same code path the guard uses: it runs the guard test with
 * UE_MCP_RECORD_GOLDEN=1, which makes it write tests/golden/editor-down.json
 * instead of asserting against it. A separate recorder would be a second
 * implementation of the surface capture, and the baseline would then only
 * prove the two implementations agree with each other.
 *
 * Review the resulting diff before committing. It is the contract every client
 * sees at startup.
 */
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import * as path from "node:path";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const target = "tests/unit/golden-editor-down.test.ts";

const result = spawnSync(
  process.execPath,
  [path.join(repoRoot, "node_modules", "vitest", "vitest.mjs"), "run", target],
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
