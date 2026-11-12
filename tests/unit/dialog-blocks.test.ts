/**
 * A dialog's body as structure (#1891): the paragraphs and tables list_dialogs
 * reads off the dialog, carried to every consumer next to the flat message.
 *
 * The status file and the plugin's refusal carry the message alone, and the
 * status file is usually what reports a dialog first, so a later read without
 * blocks must not erase them and a read that adds them must be announced.
 */
import { afterEach, describe, expect, it, vi } from "vitest";
import {
  DialogGuard,
  onDialogEvent,
  renderDialog,
  type BlockingDialog,
  type DialogEvent,
  type GuardDeps,
} from "../../src/editor/dialog-guard.js";
import type { DialogBlock, DialogPromptRequest } from "../../src/core/types.js";

const BUTTONS = ["Save Selected", "Don't Save", "Cancel"];
const CHOICES = BUTTONS.map((b) => ({ buttonLabel: b, respondWith: `editor(respond_to_dialog, buttonLabel="${b}")` }));
const FLAT = [
  "Select Content to Save",
  "Asset", "File", "Type",
  "L_FightArena", "/Game/Maps/Test/L_FightArena", "/Script/Engine.World",
  ...BUTTONS,
].join("\n");
const BLOCKS: DialogBlock[] = [
  { type: "paragraph", text: "Select Content to Save" },
  {
    type: "table",
    columns: ["Asset", "File", "Type"],
    rows: [["L_FightArena", "/Game/Maps/Test/L_FightArena", "/Script/Engine.World"]],
    checked: [true],
    items: [1],
  },
];
const ITEMS = [
  { index: 0, label: "Asset", cells: ["Asset", "File", "Type"], checked: true },
  { index: 1, label: "L_FightArena", cells: ["L_FightArena", "/Game/Maps/Test/L_FightArena", "/Script/Engine.World"], checked: true },
];
const listed = (blocks: DialogBlock[] = BLOCKS) => ({
  dialogs: [{ title: "Save Content", message: FLAT, buttons: BUTTONS, choices: CHOICES, items: ITEMS, blocks }],
});

const saved = { ...process.env };
afterEach(() => {
  process.env = { ...saved };
});

function make(over: Partial<GuardDeps> = {}) {
  const guard = new DialogGuard({
    mode: over.mode ?? (() => "auto"),
    probe: over.probe ?? (async () => listed()),
    press: over.press ?? (async () => ({ success: true })),
    prompt: over.prompt,
    readSnapshot: over.readSnapshot,
  });
  guard.label = "Scratch";
  return guard;
}

function collect() {
  const seen: DialogEvent[] = [];
  const off = onDialogEvent((e) => seen.push(e));
  return { seen, off };
}

describe("rendering a dialog from its blocks", () => {
  it("lays out any table under its own headings, without the button labels", () => {
    const dialog: BlockingDialog = { title: "Save Content", message: FLAT, buttons: BUTTONS, choices: [], blocks: BLOCKS };
    expect(renderDialog(dialog)).toBe([
      "Select Content to Save",
      "",
      "  Asset         File                          Type",
      "  ------------  ----------------------------  --------------------",
      "  L_FightArena  /Game/Maps/Test/L_FightArena  /Script/Engine.World",
    ].join("\n"));
  });

  it("is not limited to the save prompt's three columns", () => {
    const blocks: DialogBlock[] = [
      { type: "paragraph", text: "These files are checked out by someone else." },
      { type: "table", columns: ["File", "Checked Out By"], rows: [["/Game/A", "alice"]] },
      { type: "paragraph", text: "Continue anyway?" },
    ];
    expect(renderDialog({ title: "t", message: "", buttons: [], choices: [], blocks })).toBe([
      "These files are checked out by someone else.",
      "",
      "  File     Checked Out By",
      "  -------  --------------",
      "  /Game/A  alice",
      "",
      "Continue anyway?",
    ].join("\n"));
  });

  it("falls back to the flat message when the plugin reports no blocks", () => {
    const flat = renderDialog({ title: "Save Content", message: FLAT, buttons: BUTTONS, choices: [] });
    expect(flat).toContain("L_FightArena");
    expect(flat).not.toContain("Don't Save");
  });
});

describe("blocks reach every consumer", () => {
  it("are read from list_dialogs", async () => {
    const guard = make();
    await guard.check("project.get_status", "action");
    expect(guard.current?.blocks).toEqual(BLOCKS);
  });

  it("are given to an extension's prompt", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    const prompt = vi.fn(async (_req: DialogPromptRequest) => ({ action: "accept" as const, content: { button: "Cancel" } }));
    const guard = make({ mode: () => "interactive", prompt: () => prompt });
    await guard.check("editor.stop_editor", "action");
    expect(prompt.mock.calls[0][0].dialog.blocks).toEqual(BLOCKS);
  });

  it("survive a later read from the status file, which carries none", async () => {
    const guard = make();
    await guard.check("project.get_status", "action");
    guard.note({ title: "Save Content", message: "flattened differently", buttons: BUTTONS, choices: [] });
    expect(guard.current?.message).toBe("flattened differently");
    expect(guard.current?.blocks).toEqual(BLOCKS);
    expect(guard.current?.items).toEqual(ITEMS);
  });
});

describe("dialog.updated", () => {
  it("announces the blocks once a dialog first reported without them is read in full", async () => {
    const { seen, off } = collect();
    try {
      const guard = make();
      guard.note({ title: "Save Content", message: FLAT, buttons: BUTTONS, choices: [] });
      await guard.check("project.get_status", "action");
      await guard.check("project.get_status", "action");
      expect(seen.map((e) => e.type)).toEqual(["opened", "updated"]);
      expect(seen[0].blocks).toBeUndefined();
      expect(seen[1]).toMatchObject({ editor: "Scratch", title: "Save Content", buttons: BUTTONS, blocks: BLOCKS, items: ITEMS });
    } finally {
      off();
    }
  });

  it("announces a tick changed in the editor", async () => {
    const { seen, off } = collect();
    try {
      let blocks = BLOCKS;
      const guard = make({ probe: async () => listed(blocks) });
      await guard.check("project.get_status", "action");
      blocks = [BLOCKS[0], { ...(BLOCKS[1] as Extract<DialogBlock, { type: "table" }>), checked: [false] }];
      await guard.check("project.get_status", "action");
      expect(seen.map((e) => e.type)).toEqual(["opened", "updated"]);
      expect((seen[1].blocks![1] as Extract<DialogBlock, { type: "table" }>).checked).toEqual([false]);
    } finally {
      off();
    }
  });
});
