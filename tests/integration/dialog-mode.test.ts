/**
 * `ue-mcp dialog mode`, spawned: what it stores in the user state file, how
 * the variable outranks it, and that a server started afterwards handles a
 * blocking editor dialog the way the stored mode says.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, plain, runCli, Sandbox, type CliResult } from "./harness.js";

let sb: Sandbox;
let uproject: string;

const statePath = () => path.join(sb.home, ".ue-mcp", "state.json");
const state = () => (fs.existsSync(statePath()) ? JSON.parse(fs.readFileSync(statePath(), "utf8")) : {}) as {
  preferences?: { dialog?: { mode?: string } };
  projects?: Record<string, { dialog?: { mode?: string } }>;
};

async function dialog(args: string[], env: Record<string, string> = {}): Promise<CliResult> {
  const r = await runCli(sb, ["dialog", ...args], { env });
  return { ...r, stdout: plain(r.stdout), stderr: plain(r.stderr) };
}

beforeAll(() => {
  sb = new Sandbox();
  uproject = sb.makeProject("Game");
});
afterAll(() => sb.cleanup());

describe("ue-mcp dialog mode", () => {
  it("prints the default, stores user-wide and per-editor modes, and lets the variable win", async () => {
    covers("cli.dialog.mode");
    const initial = await dialog(["mode"]);
    expect(initial.code).toBe(0);
    expect(initial.stdout).toContain("effective: interactive when the connected MCP client advertises elicitation, otherwise defer");

    const [set, scoped] = [await dialog(["mode", "defer"]), await dialog(["mode", "AUTO", "--editor", uproject])];
    expect(set.code).toBe(0);
    expect(scoped.code).toBe(0);
    expect(scoped.stdout + scoped.stderr).toContain("the person at the keyboard is no longer the one deciding");
    expect(state().preferences?.dialog?.mode).toBe("defer");
    expect(Object.values(state().projects ?? {}).map((p) => p.dialog?.mode)).toEqual(["auto"]);

    const [user, editor, env, typo] = await Promise.all([
      dialog(["mode"]),
      dialog(["mode", "--editor", uproject]),
      dialog(["mode", "--editor", uproject], { UE_MCP_DIALOG_MODE: "interactive" }),
      dialog(["mode"], { UE_MCP_DIALOG_MODE: "sometimes" }),
    ]);
    expect(user.stdout).toMatch(/effective:\s+defer\n/);
    expect(editor.stdout).toMatch(/effective:\s+auto\n/);
    expect(editor.stdout).toContain("editor 'Game': auto");
    expect(env.stdout).toContain("effective: interactive (UE_MCP_DIALOG_MODE)");
    expect(typo.stdout + typo.stderr).toContain('UE_MCP_DIALOG_MODE="sometimes" names no mode and is ignored');
    expect(typo.stdout).toMatch(/effective:\s+defer\n/);

    const before = fs.readFileSync(statePath(), "utf8");
    const bad = await dialog(["mode", "whenever"]);
    expect(bad.code).toBe(1);
    expect(bad.stdout + bad.stderr).toContain('Unknown mode "whenever"');
    expect(fs.readFileSync(statePath(), "utf8")).toBe(before);
  });

  it("decides how a server started afterwards handles a blocking dialog", async () => {
    covers("cli.dialog.mode");
    const projectDir = path.dirname(uproject);
    const modal = { title: "Save Content", message: "Save changes to 1 package?", buttons: ["Save", "Don't Save"] };
    const bridge = await FakeBridge.start({ projectDir, handlers: { list_dialogs: () => ({ dialogs: [modal] }) } });
    try {
      const refusal = async (): Promise<Record<string, unknown>> => {
        const server = await McpServer.start(sb, { projects: [uproject], strategy: "full", port: null });
        try {
          return json(await server.call("level", { action: "get_outliner" }));
        } finally {
          await server.close();
        }
      };

      const auto = await refusal();
      expect(auto).toMatchObject({ dialogBlocking: true, dialogMode: "auto", dialogTitle: "Save Content", refusedMethod: expect.stringContaining("get_outliner") });

      expect((await dialog(["mode", "default", "--editor", uproject])).code).toBe(0);
      expect(state().projects).toBeUndefined();
      expect(state().preferences?.dialog?.mode).toBe("defer");
      const deferred = await refusal();
      expect(deferred).toMatchObject({ dialogBlocking: true, dialogMode: "defer" });
      expect(bridge.calls.some((c) => c.method === "get_world_outliner")).toBe(false);
    } finally {
      await bridge.stop();
    }
  });
});
