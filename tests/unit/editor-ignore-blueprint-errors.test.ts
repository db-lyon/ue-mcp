import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import type { ElicitFn, ToolContext } from "../../src/types.js";
import type { UeMcpConfig } from "../../src/project.js";

function makeContext(config: UeMcpConfig, elicit?: ElicitFn) {
  const call = vi.fn().mockResolvedValue({ success: true, action: "start" });
  const ctx = {
    bridge: { call },
    project: { projectDir: "C:/Projects/Demo", config },
    elicit,
  } as unknown as ToolContext;
  return { ctx, call };
}

async function invoke(ctx: ToolContext) {
  return editorTool.handler(ctx, {
    action: "play_in_editor_ignore_blueprint_errors",
  }) as Promise<Record<string, unknown>>;
}

describe("editor(play_in_editor_ignore_blueprint_errors)", () => {
  it("refuses when neither the config opt-in nor elicitation is available", async () => {
    const { ctx, call } = makeContext({});

    const result = await invoke(ctx);

    expect(result.blocked).toBe(true);
    expect(result.code).toBe("approval_required");
    expect(call).not.toHaveBeenCalled();
  });

  it("does not start PIE when the user declines", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" });
    const { ctx, call } = makeContext({}, elicit);

    const result = await invoke(ctx);

    expect(result.blocked).toBe(true);
    expect(result.code).toBe("user_declined");
    expect(call).not.toHaveBeenCalled();
  });

  it("does not start PIE when the approval prompt is cancelled", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "cancel" });
    const { ctx, call } = makeContext({}, elicit);

    const result = await invoke(ctx);

    expect(result.blocked).toBe(true);
    expect(result.code).toBe("user_cancelled");
    expect(call).not.toHaveBeenCalled();
  });

  it("does not start PIE when the approval prompt itself fails", async () => {
    const elicit = vi.fn<ElicitFn>().mockRejectedValue(new Error("client closed the prompt"));
    const { ctx, call } = makeContext({}, elicit);

    const result = await invoke(ctx);

    expect(result.blocked).toBe(true);
    expect(result.code).toBe("approval_prompt_failed");
    expect(call).not.toHaveBeenCalled();
  });

  it("starts the native guarded path after explicit user approval", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "accept" });
    const { ctx, call } = makeContext({}, elicit);

    await invoke(ctx);

    expect(call).toHaveBeenCalledWith("pie_control", expect.objectContaining({
      action: "start",
      ignoreBlueprintErrors: true,
      authorizationSource: "user_approval",
    }));
  });

  it("accepts the standing config opt-in without prompting", async () => {
    const elicit = vi.fn<ElicitFn>();
    const { ctx, call } = makeContext({ pie: { allowIgnoreBlueprintErrors: true } }, elicit);

    await invoke(ctx);

    expect(elicit).not.toHaveBeenCalled();
    expect(call).toHaveBeenCalledWith("pie_control", expect.objectContaining({
      action: "start",
      ignoreBlueprintErrors: true,
      authorizationSource: "config",
    }));
  });

  it("still prompts when the config opt-in is present but false", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" });
    const { ctx, call } = makeContext({ pie: { allowIgnoreBlueprintErrors: false } }, elicit);

    const result = await invoke(ctx);

    expect(elicit).toHaveBeenCalledTimes(1);
    expect(result.code).toBe("user_declined");
    expect(call).not.toHaveBeenCalled();
  });

  it("never sends the bypass flag on the plain play_in_editor action", async () => {
    const { ctx, call } = makeContext({ pie: { allowIgnoreBlueprintErrors: true } });

    await editorTool.handler(ctx, { action: "play_in_editor", pieAction: "start" });

    expect(call).toHaveBeenCalledWith(
      "pie_control",
      expect.not.objectContaining({ ignoreBlueprintErrors: true }),
      undefined,
    );
  });
});
