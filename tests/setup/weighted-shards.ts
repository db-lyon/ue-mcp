/**
 * `vitest --shard=i/n` by measured duration rather than by file count.
 *
 * Vitest's own sharding hashes paths into equal-count slices, so the three integration files
 * that take 90 s or more could land in one job and set the CI wall time. This packs files
 * longest first onto the lightest shard, from the per-file seconds in tests/timing/vitest-weights.json.
 * Every shard computes the same packing from the same file list, so each file runs exactly once.
 * A file the weights do not list counts as DEFAULT_S. Refresh them with scripts/vitest-weights.mjs.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { BaseSequencer, type TestSpecification } from "vitest/node";

const DEFAULT_S = 2;
/** Collecting and transforming a file costs this much whatever it holds. */
const PER_FILE_S = 0.1;
export const WEIGHTS_FILE = "tests/timing/vitest-weights.json";

export function readWeights(file: string): Record<string, number> {
  try {
    return (JSON.parse(fs.readFileSync(file, "utf8")) as { files?: Record<string, number> }).files ?? {};
  } catch {
    return {};
  }
}

/** Longest first onto the lightest shard; ties by path, so the result does not depend on input order. */
export function packShards(files: string[], count: number, weights: Record<string, number>): string[][] {
  const weight = (f: string) => (weights[f] ?? DEFAULT_S) + PER_FILE_S;
  const order = [...new Set(files)].sort((a, b) => weight(b) - weight(a) || (a < b ? -1 : a > b ? 1 : 0));
  const shards = Array.from({ length: count }, () => ({ files: [] as string[], load: 0 }));
  for (const f of order) {
    let lightest = shards[0];
    for (const s of shards) if (s.load < lightest.load) lightest = s;
    lightest.files.push(f);
    lightest.load += weight(f);
  }
  return shards.map((s) => s.files);
}

export default class WeightedSequencer extends BaseSequencer {
  override async shard(files: TestSpecification[]): Promise<TestSpecification[]> {
    const { index, count } = this.ctx.config.shard!;
    const root = this.ctx.config.root;
    const key = (spec: TestSpecification) => path.relative(root, spec.moduleId).split(path.sep).join("/");
    const mine = new Set(packShards(files.map(key), count, readWeights(path.join(root, WEIGHTS_FILE)))[index - 1]);
    return files.filter((spec) => mine.has(key(spec)));
  }
}
