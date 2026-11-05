import { describe, it, expect, beforeEach } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { appendTimingRun, readTimingRuns } from "../../scripts/lib/release-timing.mjs";

describe("release timing history", () => {
  let file: string;
  beforeEach(() => {
    file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "release-timing-")), "release-timing.json");
  });

  it("starts a history when there is no file", () => {
    expect(readTimingRuns(file)).toEqual([]);
    appendTimingRun(file, { version: "1.0.0", totalS: 500 });
    expect(JSON.parse(fs.readFileSync(file, "utf-8"))).toEqual({ runs: [{ version: "1.0.0", totalS: 500 }] });
  });

  it("keeps every earlier run, newest last", () => {
    appendTimingRun(file, { version: "1.0.0", totalS: 700 });
    const runs = appendTimingRun(file, { version: "1.0.1", totalS: 400 });
    expect(runs.map((r) => r.version)).toEqual(["1.0.0", "1.0.1"]);
    expect(readTimingRuns(file)).toEqual(runs);
  });

  it("keeps a single-run file written before the history as the first entry", () => {
    fs.writeFileSync(file, JSON.stringify({ version: "0.9.0", totalS: 5769, overBudget: true }));
    const runs = appendTimingRun(file, { version: "1.0.0", totalS: 555 });
    expect(runs).toEqual([{ version: "0.9.0", totalS: 5769, overBudget: true }, { version: "1.0.0", totalS: 555 }]);
  });

  it("reads an unparseable file as no runs rather than failing the release summary", () => {
    fs.writeFileSync(file, "{ not json");
    expect(appendTimingRun(file, { version: "1.0.0", totalS: 1 })).toHaveLength(1);
  });
});
