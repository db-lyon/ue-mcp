// @live-editor interactive
/**
 * A dialog's body read as structure, not one line per text widget (#1891).
 *
 * The Save Content prompt is the case: a caption over a list with a header row
 * (Asset, File, Type) and a checkbox per unsaved package, then the buttons.
 * Raised the way a user meets it, a dirty map and a quit, and cancelled so the
 * editor outlives the test.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;

beforeAll(async () => {
  // auto, so the cancel below is the test's own press and no form is raised.
  server = await LiveServer.start({
    projects: [target.uproject],
    env: { UE_MCP_DIALOG_MODE: "auto" },
  });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
});

async function listDialogs(): Promise<any[]> {
  const listed = resultJson<any>(await server.call("editor", { action: "list_dialogs" }));
  return Array.isArray(listed.dialogs) ? listed.dialogs : [];
}

async function waitForDialogs(want: "some" | "none", timeoutMs = 30_000): Promise<any[]> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const dialogs = await listDialogs();
    if (want === "some" ? dialogs.length > 0 : dialogs.length === 0) return dialogs;
    if (Date.now() > deadline) throw new Error(`timed out waiting for ${want} dialogs (saw ${dialogs.length})`);
    await new Promise((r) => setTimeout(r, 250));
  }
}

describe("the Save Content prompt as structure (#1891)", () => {
  it("reports the caption as a paragraph and the package list as a table with its ticks", async () => {
    await waitForDialogs("none");
    resultJson<any>(await server.call("level", {
      action: "place_actor",
      actorClass: "StaticMeshActor",
      label: `DialogStructureProbe_${Date.now()}`,
      staticMesh: "/Engine/BasicShapes/Cube.Cube",
    }));
    resultJson<any>(await server.call("editor", { action: "request_editor_shutdown", requireClean: false }));
    try {
      const [dialog] = await waitForDialogs("some");
      expect(dialog.title).toBe("Save Content");
      // The flat text is still there for a client that reads only that.
      expect(typeof dialog.message).toBe("string");
      expect(dialog.message).toContain("/Script/Engine.World");

      const blocks: any[] = dialog.blocks;
      expect(Array.isArray(blocks), JSON.stringify(dialog)).toBe(true);
      const tables = blocks.filter((b) => b.type === "table");
      const paragraphs = blocks.filter((b) => b.type === "paragraph");
      expect(tables, JSON.stringify(blocks)).toHaveLength(1);
      const table = tables[0];

      // The visible headings, without the checkbox and icon columns, which
      // have no heading and no text in any cell.
      expect(table.columns).toEqual(["Asset", "File", "Type"]);
      expect(table.rows.length).toBeGreaterThan(0);
      for (const row of table.rows) expect(row).toHaveLength(3);
      // The dirty map is a row: its name, its package, its class.
      const map = table.rows.find((r: string[]) => r[2] === "/Script/Engine.World");
      expect(map, JSON.stringify(table.rows)).toBeDefined();
      expect(map[1].startsWith("/Game/")).toBe(true);
      expect(map[1].endsWith(`/${map[0]}`)).toBe(true);

      // Each row's tick, and the item respond_to_dialog would take for it.
      expect(table.checked).toHaveLength(table.rows.length);
      for (const c of table.checked) expect(typeof c).toBe("boolean");
      expect(table.items).toHaveLength(table.rows.length);
      const mapIndex = table.rows.indexOf(map);
      const item = dialog.items[table.items[mapIndex]];
      expect(item.cells).toEqual(expect.arrayContaining(map));
      expect(item.checked).toBe(table.checked[mapIndex]);

      // The caption is a paragraph; the title and the buttons are not body.
      expect(paragraphs.length).toBeGreaterThan(0);
      const body = paragraphs.map((p) => p.text);
      expect(body).not.toContain("Save Content");
      for (const button of dialog.buttons) expect(body).not.toContain(button);
      for (const heading of table.columns) expect(body).not.toContain(heading);
      expect(blocks.indexOf(paragraphs[0])).toBeLessThan(blocks.indexOf(table));
    } finally {
      await server.call("editor", { action: "respond_to_dialog", buttonLabel: "Cancel" });
      await waitForDialogs("none");
    }
  }, 180_000);
});
