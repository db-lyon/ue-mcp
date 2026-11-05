import * as fs from "node:fs";
import * as path from "node:path";
import { describe, expect, it } from "vitest";

import { packShards, readWeights, WEIGHTS_FILE } from "../setup/weighted-shards.js";

const REPO = path.resolve(__dirname, "..", "..");
const testFiles = (dir: string) =>
  fs.readdirSync(path.join(REPO, dir)).filter((f) => f.endsWith(".test.ts")).map((f) => `${dir}/${f}`);

describe("weighted shards", () => {
  it("puts every file in exactly one shard, whatever order the files arrive in", () => {
    const files = ["a", "b", "c", "d", "e", "f", "g"];
    const weights = { a: 50, b: 40, c: 30, d: 5, e: 5, f: 1 };
    const shards = packShards(files, 3, weights);
    expect(shards.flat().sort()).toEqual(files);
    expect(packShards([...files].reverse(), 3, weights)).toEqual(shards);
  });

  it("packs the longest files apart, so no shard carries two of them", () => {
    const shards = packShards(["slow1", "slow2", "slow3", "x", "y", "z"], 3, { slow1: 100, slow2: 90, slow3: 80, x: 1, y: 1, z: 1 });
    for (const s of shards) expect(s.filter((f) => f.startsWith("slow"))).toHaveLength(1);
  });

  it("leaves a shard empty rather than splitting a file when there are more shards than files", () => {
    const shards = packShards(["a", "b"], 4, {});
    expect(shards.flat().sort()).toEqual(["a", "b"]);
    expect(shards.filter((s) => s.length === 0)).toHaveLength(2);
  });

  it("partitions the real unit and integration suites", () => {
    const weights = readWeights(path.join(REPO, WEIGHTS_FILE));
    for (const [dir, count] of [["tests/unit", 3], ["tests/integration", 6]] as const) {
      const files = testFiles(dir);
      const shards = packShards(files, count, weights);
      expect(shards.flat().sort()).toEqual([...files].sort());
    }
  });

  it("has a measured weight for every integration file", () => {
    // An unweighted file counts as two seconds, which for a spawned suite can be off by a minute.
    const weights = readWeights(path.join(REPO, WEIGHTS_FILE));
    const missing = testFiles("tests/integration").filter((f) => !(f in weights));
    expect(missing, "record it with scripts/vitest-weights.mjs").toEqual([]);
  });
});
