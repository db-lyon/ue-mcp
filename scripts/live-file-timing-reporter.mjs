/**
 * Records each live file's whole cost: collection, setup, hooks and tests.
 *
 * The JSON reporter only times tests, and a live file's fixtures and server
 * start live in its hooks, so balancing shards on test time alone put a 150 s
 * shard beside a 410 s one. The runner reads this file instead (#1436).
 */
import fs from "node:fs";
import path from "node:path";

export default class LiveFileTimingReporter {
  files = {};

  onTestModuleEnd(testModule) {
    const d = testModule.diagnostic();
    const file = path.relative(process.cwd(), testModule.moduleId).split(path.sep).join("/");
    this.files[file] = Math.round(d.prepareDuration + d.environmentSetupDuration + d.collectDuration + d.setupDuration + d.duration);
  }

  onTestRunEnd() {
    const out = process.env.UE_MCP_LIVE_FILE_TIMING;
    if (out) fs.writeFileSync(out, `${JSON.stringify(this.files, null, 2)}\n`);
  }
}
