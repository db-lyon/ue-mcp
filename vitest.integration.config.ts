import { defineConfig } from "vitest/config";

import WeightedSequencer from "./tests/setup/weighted-shards.js";

/**
 * The integration suite (tests/integration): every case spawns the real CLI,
 * server or daemon in its own sandbox, so files run in parallel forks. The
 * unit suite stays single-fork; it shares process state these never touch.
 */
export default defineConfig({
  test: {
    include: ["tests/integration/**/*.test.ts"],
    testTimeout: 120_000,
    hookTimeout: 120_000,
    reporters: ["verbose"],
    fileParallelism: true,
    pool: "forks",
    // --shard=i/n in CI packs files by measured duration (tests/setup/weighted-shards.ts).
    sequence: { sequencer: WeightedSequencer },
    // Each file spawns its own servers and daemons, so a fork per file on a
    // many-core machine starves the worker RPC; four at a time keeps it answering.
    poolOptions: { forks: { singleFork: false, maxForks: 4, minForks: 1 } },
  },
});
