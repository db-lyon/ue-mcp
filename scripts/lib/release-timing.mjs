/**
 * release-logs/release-timing.json keeps every run the release runner made, oldest first, so
 * an overrun stays visible after the next run (#1509). A file from before this held one run
 * as a bare object; it becomes the first entry.
 */
import fs from "node:fs";

/** Reads the recorded runs. A missing or unreadable file reads as none. */
export function readTimingRuns(file) {
  let parsed;
  try {
    parsed = JSON.parse(fs.readFileSync(file, "utf-8"));
  } catch {
    return [];
  }
  if (Array.isArray(parsed?.runs)) return parsed.runs;
  if (parsed && typeof parsed === "object" && "totalS" in parsed) return [parsed];
  return [];
}

/** Appends one run and writes the file back. Returns every run, the new one last. */
export function appendTimingRun(file, run) {
  const runs = [...readTimingRuns(file), run];
  fs.writeFileSync(file, `${JSON.stringify({ runs }, null, 2)}\n`);
  return runs;
}
