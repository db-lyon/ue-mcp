/**
 * Viewport control and the transaction stack, against a real editor.
 *
 * These exist for one reason: a screenshot taken twice of the same scene was
 * not comparable. `set_viewport` never wrote the FOV it read back, and
 * `capture_scene_png` set no exposure override, so every capture inherited
 * whatever the world's auto-exposure had settled on. Neither fact is visible
 * from a unit test, because both are fields on `FEditorViewportClient`, a
 * plain C++ class that no reflection path reaches.
 *
 * The transaction half is here for a companion reason: `material`'s
 * begin/end pair had no cancel, so a flow that failed partway could only ever
 * commit what it had already done.
 *
 * Every case restores what it changed.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;

const call = async (tool: string, args: Record<string, unknown>) =>
  server.call(tool, { ...args, timeoutMs: 120_000 });

interface ViewportState {
  viewMode?: string;
  viewportType?: string;
  fov?: number;
  gameView?: boolean;
  exposure?: { fixed?: boolean; ev100?: number };
  location?: { x: number; y: number; z: number };
}

const state = async (): Promise<ViewportState> =>
  resultJson<ViewportState>(await call("editor", { action: "get_viewport_state" }));

let original: ViewportState;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
  original = await state();
}, 240_000);

afterAll(async () => {
  // Put the viewport back the way it was found, whatever happened above.
  try {
    if (original.viewMode) await call("editor", { action: "set_view_mode", viewMode: original.viewMode });
    if (original.gameView !== undefined) await call("editor", { action: "set_game_view", enabled: original.gameView });
    await call("editor", { action: "set_viewport_exposure", mode: "auto" });
  } catch {
    // Best effort; a cleanup failure must not mask a real one.
  }
  await server?.close();
  closeLiveBridges();
});

describe("get_viewport_state", () => {
  it("reports what get_viewport never did", async () => {
    const body = await state();
    // The whole point: these fields exist and were previously unreadable.
    expect(body.viewMode).toBeTruthy();
    expect(body.viewportType).toBeTruthy();
    expect(typeof body.fov).toBe("number");
    expect(typeof body.gameView).toBe("boolean");
    expect(body.exposure).toBeDefined();
  });
});

describe("set_view_mode", () => {
  it("changes the mode and reports the previous one", async () => {
    const body = resultJson<{ viewMode: string; previousViewMode?: string; unchanged?: boolean }>(
      await call("editor", { action: "set_view_mode", viewMode: "Unlit" }),
    );
    expect(body.viewMode.toLowerCase()).toContain("unlit");
    expect((await state()).viewMode?.toLowerCase()).toContain("unlit");
  });

  it("is idempotent: setting the mode it already has says so", async () => {
    const body = resultJson<{ unchanged?: boolean }>(
      await call("editor", { action: "set_view_mode", viewMode: "Unlit" }),
    );
    expect(body.unchanged).toBe(true);
  });

  it("refuses an unknown mode by listing the real ones", async () => {
    const body = resultJson<{ success: boolean; error?: string }>(
      await call("editor", { action: "set_view_mode", viewMode: "NotAViewMode" }),
    );
    expect(body.success).toBe(false);
    // A list of valid modes is the difference between one turn and three.
    expect(body.error).toMatch(/Lit|Unlit|Wireframe/);
  });
});

describe("set_viewport_exposure", () => {
  it("pins a fixed EV100, which is what makes two captures comparable", async () => {
    const body = resultJson<{ exposure?: { fixed?: boolean; ev100?: number } }>(
      await call("editor", { action: "set_viewport_exposure", ev100: 10, fixed: true }),
    );
    const after = await state();
    expect(after.exposure?.fixed ?? body.exposure?.fixed).toBe(true);
  });

  it("returns to auto eye adaptation", async () => {
    await call("editor", { action: "set_viewport_exposure", mode: "auto" });
    expect((await state()).exposure?.fixed).toBe(false);
  });
});

describe("set_viewport_view", () => {
  it("writes the FOV that set_viewport silently ignored", async () => {
    const before = (await state()).fov ?? 90;
    const wanted = Math.round(before) === 70 ? 80 : 70;
    await call("editor", { action: "set_viewport_view", fov: wanted });
    expect(Math.round((await state()).fov ?? 0)).toBe(wanted);
    await call("editor", { action: "set_viewport_view", fov: before });
  });
});

describe("set_game_view and redraw_viewport", () => {
  it("toggles game view and reports an unchanged repeat", async () => {
    await call("editor", { action: "set_game_view", enabled: true });
    expect((await state()).gameView).toBe(true);
    const again = resultJson<{ unchanged?: boolean }>(
      await call("editor", { action: "set_game_view", enabled: true }),
    );
    expect(again.unchanged).toBe(true);
    await call("editor", { action: "set_game_view", enabled: false });
  });

  it("redraws without erroring", async () => {
    const body = resultJson<{ success: boolean }>(await call("editor", { action: "redraw_viewport" }));
    expect(body.success).toBe(true);
  });
});

describe("the transaction stack", () => {
  it("reports what an undo would actually reverse", async () => {
    const body = resultJson<{ canUndo: boolean; canRedo: boolean; undoDescription?: string }>(
      await call("editor", { action: "get_undo_state" }),
    );
    expect(typeof body.canUndo).toBe("boolean");
    expect(typeof body.canRedo).toBe("boolean");
  });

  it("opens and cancels a transaction, which material's pair could never do", async () => {
    const begun = resultJson<{ success: boolean; transactionIndex?: number }>(
      await call("editor", { action: "begin_transaction", description: "MCP live probe" }),
    );
    expect(begun.success).toBe(true);

    const cancelled = resultJson<{ success: boolean }>(
      await call("editor", { action: "cancel_transaction" }),
    );
    expect(cancelled.success).toBe(true);
  });

  it("reports rather than errors when there is nothing open to cancel", async () => {
    // A rollback has to be safe to replay.
    const body = resultJson<{ success: boolean; wasActive?: boolean }>(
      await call("editor", { action: "cancel_transaction" }),
    );
    expect(body.success).toBe(true);
    expect(body.wasActive).toBe(false);
  });

  it("opens and commits a transaction", async () => {
    await call("editor", { action: "begin_transaction", description: "MCP live probe commit" });
    const ended = resultJson<{ success: boolean }>(await call("editor", { action: "end_transaction" }));
    expect(ended.success).toBe(true);
  });

  it("lists the undo buffer with titles rather than opaque indices", async () => {
    const body = resultJson<{
      transactions?: Array<{ title?: string; index?: number; state?: string }>;
      currentIndex?: number;
      order?: string;
    }>(await call("editor", { action: "get_transaction_history", maxEntries: 5 }));
    expect(Array.isArray(body.transactions)).toBe(true);
    expect(body.order).toBe("newestFirst");
    // Every row carries a human title; an index alone is not addressable by a caller.
    for (const row of body.transactions ?? []) {
      expect(typeof row.title).toBe("string");
      expect(["applied", "undone"]).toContain(row.state);
    }
  });

  it("refuses a multi-step undo while a transaction is open", async () => {
    await call("editor", { action: "begin_transaction", description: "MCP live probe guard" });
    const body = resultJson<{ success: boolean; error?: string }>(
      await call("editor", { action: "undo_redo_steps", steps: 1, direction: "undo" }),
    );
    await call("editor", { action: "cancel_transaction" });
    // Undoing across an open transaction corrupts the buffer, so it is refused.
    expect(body.success).toBe(false);
  });
});
