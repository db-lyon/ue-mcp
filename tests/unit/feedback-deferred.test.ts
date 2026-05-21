import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

let tmpRoot: string;
let pendingDir: string;

beforeEach(() => {
  tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-deferred-test-"));
  pendingDir = path.join(tmpRoot, "pending-feedback");
  process.env.UE_MCP_PENDING_DIR = pendingDir;
});

afterEach(() => {
  delete process.env.UE_MCP_PENDING_DIR;
  fs.rmSync(tmpRoot, { recursive: true, force: true });
});

// feedback-deferred reads UE_MCP_PENDING_DIR lazily per call, so we can use
// the normal module import — no cache-busting needed.
import * as deferred from "../../src/feedback-deferred.js";
async function freshImport(): Promise<typeof deferred> {
  return deferred;
}

describe("feedback-deferred", () => {
  it("creates the pending dir on first defer", async () => {
    const { deferSubmission } = await freshImport();
    expect(fs.existsSync(pendingDir)).toBe(false);
    deferSubmission(
      { title: "t", body: "b", labels: ["agent-feedback"] },
      "Vale",
      "user",
    );
    expect(fs.existsSync(pendingDir)).toBe(true);
  });

  it("returns an entry with a sortable id and ISO timestamp", async () => {
    const { deferSubmission } = await freshImport();
    const entry = deferSubmission(
      { title: "t", body: "b", labels: [] },
      "Vale",
      "user",
    );
    expect(entry.id).toMatch(/^\d{8}T\d{9}-[0-9a-f]{6}$/);
    expect(entry.createdAt).toMatch(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}/);
    expect(entry.project).toBe("Vale");
    expect(entry.author).toBe("user");
  });

  it("listDeferred returns entries in chronological order", async () => {
    const mod = await freshImport();
    const a = mod.deferSubmission({ title: "a", body: "x", labels: [] }, "Vale", "user");
    await new Promise((r) => setTimeout(r, 5));
    const b = mod.deferSubmission({ title: "b", body: "y", labels: [] }, "Vale", "bot");
    const entries = mod.listDeferred();
    expect(entries.map((e) => e.id)).toEqual([a.id, b.id]);
  });

  it("loadDeferred reads back the exact payload", async () => {
    const mod = await freshImport();
    const entry = mod.deferSubmission(
      { title: "title", body: "body bytes", labels: ["a", "b"] },
      "Vale",
      "bot",
    );
    const back = mod.loadDeferred(entry.id);
    expect(back?.title).toBe("title");
    expect(back?.body).toBe("body bytes");
    expect(back?.labels).toEqual(["a", "b"]);
    expect(back?.author).toBe("bot");
  });

  it("deleteDeferred removes the file and returns true; returns false on unknown id", async () => {
    const mod = await freshImport();
    const entry = mod.deferSubmission({ title: "t", body: "b", labels: [] }, null, "user");
    expect(mod.deleteDeferred(entry.id)).toBe(true);
    expect(mod.loadDeferred(entry.id)).toBeNull();
    expect(mod.deleteDeferred(entry.id)).toBe(false);
  });

  it("listDeferred skips malformed json files silently", async () => {
    const mod = await freshImport();
    mod.deferSubmission({ title: "ok", body: "b", labels: [] }, "Vale", "user");
    fs.writeFileSync(path.join(pendingDir, "garbage.json"), "{not valid");
    const entries = mod.listDeferred();
    expect(entries).toHaveLength(1);
    expect(entries[0].title).toBe("ok");
  });
});
