/**
 * Print the logs of servers a test spawned when that test fails, so a failure
 * on CI shows what the server saw instead of only the assertion.
 */
import * as fs from "node:fs";
import { afterEach } from "vitest";

export function printServerLogsOnFailure(logs: () => string[], extra?: () => unknown): void {
  afterEach((ctx) => {
    if (ctx.task.result?.state !== "fail") return;
    for (const file of logs()) {
      const text = fs.existsSync(file) ? fs.readFileSync(file, "utf-8") : "(no log)";
      console.error(`\n----- ${file} (last 4000 chars) -----\n${text.slice(-4000)}`);
    }
    if (extra) console.error(`----- extra -----\n${JSON.stringify(extra(), null, 2).slice(0, 4000)}`);
  });
}
