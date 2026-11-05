import { defineConfig } from "vitest/config";

import WeightedSequencer from "./tests/setup/weighted-shards.js";

export default defineConfig({
  test: {
    include: ["tests/**/*.test.ts"],
    // Spawned-process suites with their own parallel config (vitest.integration.config.ts).
    exclude: ["**/node_modules/**", "tests/integration/**"],
    setupFiles: ["tests/setup/isolate-user-state.ts"],
    testTimeout: 60_000,
    hookTimeout: 60_000,
    reporters: ["verbose"],
    fileParallelism: false,
    // --shard=i/n in CI packs files by measured duration (tests/setup/weighted-shards.ts).
    sequence: { concurrent: false, sequencer: WeightedSequencer },
    pool: "forks",
    poolOptions: { forks: { singleFork: true } },
  },
});
