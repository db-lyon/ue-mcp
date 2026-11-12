/**
 * opencode advertised MCP elicitation from 2.0.18 and dropped every form until
 * 2.1.4 (anomalyco/opencode#58184). A dialog put into interactive mode on those
 * builds waited on a form nobody could see (#1459).
 */
import { describe, expect, it, vi } from "vitest";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { elicitationUnrenderedNote } from "../../src/dispatch/client-quirks.js";
import { buildElicit } from "../../src/dispatch/server-dispatch.js";
import { clientAdvertisesElicitation, resolveDialogMode } from "../../src/editor/dialog-mode.js";
import { DialogGuard, type GuardDeps } from "../../src/editor/dialog-guard.js";

describe("elicitationUnrenderedNote", () => {
  it("names the opencode builds that advertise forms they never show", () => {
    const note = elicitationUnrenderedNote({ name: "opencode", version: "2.1.3" });
    expect(note).toContain("58184");
    expect(note).toContain("2.1.4");
    expect(elicitationUnrenderedNote({ name: "opencode", version: "2.0.30" })).not.toBeNull();
    expect(elicitationUnrenderedNote({ name: "opencode", version: "1.14.2" })).not.toBeNull();
  });

  it("stays quiet once opencode renders the form", () => {
    expect(elicitationUnrenderedNote({ name: "opencode", version: "2.1.4" })).toBeNull();
    expect(elicitationUnrenderedNote({ name: "opencode", version: "2.2.5" })).toBeNull();
    expect(elicitationUnrenderedNote({ name: "opencode", version: "3.0.0" })).toBeNull();
  });

  it("treats an opencode build with no readable version as affected", () => {
    // opencode sends "unknown" when it has no app version. Defer is the safe
    // miss: interactive with no form waits on a question nobody sees.
    expect(elicitationUnrenderedNote({ name: "opencode", version: "unknown" })).not.toBeNull();
    expect(elicitationUnrenderedNote({ name: "opencode" })).not.toBeNull();
  });

  it("does not touch other clients", () => {
    expect(elicitationUnrenderedNote({ name: "claude-code", version: "2.1.3" })).toBeNull();
    expect(elicitationUnrenderedNote({ name: "opencode-something", version: "0.1.0" })).toBeNull();
    expect(elicitationUnrenderedNote(undefined)).toBeNull();
  });
});

function fakeServer(client: { name: string; version?: string }) {
  const elicitInput = vi.fn(async () => ({ action: "accept", content: { button: "Cancel" } }));
  const mcp = {
    server: {
      getClientCapabilities: () => ({ elicitation: { form: {}, url: {} } }),
      getClientVersion: () => client,
      elicitInput,
    },
  } as unknown as McpServer;
  return { mcp, elicitInput };
}

describe("buildElicit against opencode", () => {
  it("reports an affected build as unable to elicit, so dialogs default to defer", async () => {
    const { mcp, elicitInput } = fakeServer({ name: "opencode", version: "2.1.3" });
    const elicit = buildElicit(mcp);
    const canElicit = clientAdvertisesElicitation(elicit);
    expect(canElicit).toBe(false);
    expect(resolveDialogMode({ canElicit, env: {} }).mode).toBe("defer");
    expect(DialogGuard.effectiveMode("interactive", canElicit)).toBe("defer");
    await expect(elicit({ message: "m", requestedSchema: { type: "object", properties: {} } })).rejects.toThrow(
      /58184/,
    );
    expect(elicitInput).not.toHaveBeenCalled();
  });

  it("asks a fixed build in a form", async () => {
    const { mcp, elicitInput } = fakeServer({ name: "opencode", version: "2.1.4" });
    const elicit = buildElicit(mcp);
    const canElicit = clientAdvertisesElicitation(elicit);
    expect(canElicit).toBe(true);
    expect(resolveDialogMode({ canElicit, env: {} }).mode).toBe("interactive");
    await elicit({ message: "m", requestedSchema: { type: "object", properties: {} } });
    expect(elicitInput).toHaveBeenCalledOnce();
  });
});

describe("the defer refusal says why no form appeared", () => {
  const DIALOG = { title: "Save Content", message: "Select Content to Save", buttons: ["Cancel"], choices: [] };
  const deps = (mode: "defer" | "auto"): GuardDeps => ({
    mode: () => mode,
    probe: async () => ({ dialogs: [] }),
    press: async () => ({ success: true }),
    elicitationNote: () => elicitationUnrenderedNote({ name: "opencode", version: "2.1.3" }),
  });

  it("appends the client note under defer", () => {
    const refusal = new DialogGuard(deps("defer")).refusal("level.spawn_actor", DIALOG, { canElicit: false });
    expect(refusal.dialogMode).toBe("defer");
    expect(String(refusal.error)).toContain("opencode 2.1.3");
  });

  it("leaves auto alone, where the agent answers and no form was expected", () => {
    const refusal = new DialogGuard(deps("auto")).refusal("level.spawn_actor", DIALOG);
    expect(String(refusal.error)).not.toContain("opencode");
  });
});
