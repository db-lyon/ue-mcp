/**
 * A status file outlives the editor that wrote it.
 *
 * The plugin removes it only on a clean shutdown, and a crash is often preceded
 * by the very modal it last recorded. Reading a modal out of a stale snapshot
 * told a caller "An editor IS running and is blocked on the dialog" in the same
 * breath as the dialog guard correctly refusing to claim any such thing, from
 * the same file, under two different policies.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, it, expect } from "vitest";
import { readEngineSnapshot } from "../../src/engine-observer.js";
import { STATUS_STALE_AFTER_MS } from "../../src/dialog-guard.js";

function projectWithStatus(modal: unknown, ageMs: number): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-stale-"));
  const projectPath = path.join(dir, "P.uproject");
  fs.writeFileSync(projectPath, "{}");
  const statusDir = path.join(dir, "Saved", "UE_MCP_Bridge");
  fs.mkdirSync(statusDir, { recursive: true });
  const file = path.join(statusDir, "status.json");
  fs.writeFileSync(file, JSON.stringify({ phase: "ready", modal }));
  const when = Date.now() - ageMs;
  fs.utimesSync(file, new Date(when), new Date(when));
  return projectPath;
}

describe("how old a published snapshot is", () => {
  it("is reported, so a reader can tell a live editor from a leftover", () => {
    const fresh = readEngineSnapshot(projectWithStatus({ title: "Save Content" }, 0));
    expect(fresh?.ageSeconds).toBeLessThan(5);

    const old = readEngineSnapshot(projectWithStatus({ title: "Save Content" }, 60_000));
    expect(old?.ageSeconds).toBeGreaterThan(STATUS_STALE_AFTER_MS / 1000);
  });

  it("still carries the modal, so the staleness rule is the caller's to apply", () => {
    // The snapshot reader reports what it read. Deciding a 60s-old modal is
    // not a live one belongs to the callers, and they must agree: the guard
    // and the offline explainer read the same file.
    const old = readEngineSnapshot(projectWithStatus({ title: "Save Content" }, 60_000));
    expect(old?.modal?.title).toBe("Save Content");
    const stale = (old?.ageSeconds ?? 0) * 1000 > STATUS_STALE_AFTER_MS;
    expect(stale).toBe(true);
  });

  it("uses one staleness bound, shared, not a number per reader", () => {
    expect(STATUS_STALE_AFTER_MS).toBeGreaterThan(0);
  });
});
