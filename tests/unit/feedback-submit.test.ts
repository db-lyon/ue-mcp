import { describe, it, expect, beforeEach, vi } from "vitest";
import { isDirectiveResponse, type ToolContext, type ElicitFn, type ElicitResult } from "../../src/types.js";
import { clearWorkarounds, pushWorkaround } from "../../src/workaround-tracker.js";

// Stub the GitHub submission so no network call happens.
const mockSubmitFeedback = vi.fn();
vi.mock("../../src/github-app.js", () => ({
  submitFeedback: (...args: unknown[]) => mockSubmitFeedback(...args),
}));

// Stub the OAuth cache lookup so the test environment never depends on the
// developer's actual ~/.ue-mcp/auth.json state.
const mockReadUserAuth = vi.fn();
vi.mock("../../src/auth.js", () => ({
  readUserAuth: () => mockReadUserAuth(),
}));

const { feedbackTool } = await import("../../src/tools/feedback.js");

const realTitle = "blueprint.set_class_default does not save asset";
const realSummary =
  "blueprint.set_class_default marks the asset dirty but never saves it, forcing python flushes via execute_python on a separate call.";
const realPy = "import unreal\nfoo = unreal.do_thing()";

function makeCtx(elicit?: ElicitFn): ToolContext {
  return { bridge: {} as never, project: {} as never, elicit };
}

async function call(ctx: ToolContext, params: Record<string, unknown>): Promise<unknown> {
  return feedbackTool.actions.submit.handler!(ctx, { action: "submit", ...params });
}

const CACHED_USER = {
  token: "ghu_abc",
  login: "tester",
  authorized_at: "2026-05-20T00:00:00Z",
};

describe("feedback(submit) elicitation gate", () => {
  beforeEach(() => {
    clearWorkarounds();
    mockSubmitFeedback.mockReset();
    mockReadUserAuth.mockReset();
    // Default to "cached auth present" so the elicitation flow runs end-to-
    // end. Tests that specifically exercise the no-auth refuse path override
    // this with mockResolvedValue(null).
    mockReadUserAuth.mockResolvedValue(CACHED_USER);
  });

  it("refuses deterministically when the client did NOT advertise elicitation", async () => {
    const ctx = makeCtx(undefined);
    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect(r.machine?.kind).toBe("feedback.blocked");
    expect((r.result as { code?: string }).code).toBe("elicitation_unsupported");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("blocks submission and emits an elicitation prompt with the full body verbatim", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" } as ElicitResult);
    const ctx = makeCtx(elicit);
    await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(elicit).toHaveBeenCalledTimes(1);
    const [params] = elicit.mock.calls[0];
    expect(params.message).toContain(realSummary);
    expect(params.message).toContain(realPy);
    expect(params.message).toContain(realTitle);
    expect(params.requestedSchema.properties.decision).toBeDefined();
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("submits ONLY when the user clicks accept + decision=approve", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({
      action: "accept",
      content: { decision: "approve" },
    } as ElicitResult);
    const ctx = makeCtx(elicit);
    mockSubmitFeedback.mockResolvedValue({
      kind: "submitted",
      url: "https://github.com/x/y/issues/42",
      number: 42,
      authoredBy: "tester",
      authoredAs: "user",
    });

    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(isDirectiveResponse(r)).toBe(false);
    expect(mockSubmitFeedback).toHaveBeenCalledTimes(1);
    const [postedTitle, postedBody, postedLabels] = mockSubmitFeedback.mock.calls[0];
    expect(postedTitle).toBe(realTitle);
    expect(postedBody).toContain(realSummary);
    expect(postedBody).toContain(realPy);
    expect(postedLabels).toContain("agent-feedback");
    expect(postedLabels).toContain("blueprint");
  });

  it("does NOT submit when user declines the prompt", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" } as ElicitResult);
    const ctx = makeCtx(elicit);
    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect(r.machine?.kind).toBe("feedback.declined");
    expect((r.result as { code?: string }).code).toBe("user_declined");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("does NOT submit when user cancels the prompt", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "cancel" } as ElicitResult);
    const ctx = makeCtx(elicit);
    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect((r.result as { code?: string }).code).toBe("user_cancelled");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("does NOT submit when user accepts but picks decision=decline", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({
      action: "accept",
      content: { decision: "decline" },
    } as ElicitResult);
    const ctx = makeCtx(elicit);
    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect(r.machine?.kind).toBe("feedback.declined");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("rejects garbage submissions BEFORE asking the user", async () => {
    pushWorkaround({ code: "x", timestamp: "t" });
    const elicit = vi.fn<ElicitFn>();
    const ctx = makeCtx(elicit);
    const r = await call(ctx, { title: "noop", summary: realSummary, pythonWorkaround: realPy });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect(r.machine?.kind).toBe("feedback.rejected");
    expect(elicit).not.toHaveBeenCalled();
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("refuses (does not silently substitute bot) when author=\"user\" and no auth is cached", async () => {
    mockReadUserAuth.mockResolvedValue(null);
    const elicit = vi.fn<ElicitFn>();
    const ctx = makeCtx(elicit);

    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
      author: "user",
    });

    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect(r.machine?.kind).toBe("feedback.blocked");
    expect((r.result as { code?: string }).code).toBe("auth_required");
    expect(elicit).not.toHaveBeenCalled();
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("refuses when author is omitted (default \"user\") and no auth is cached", async () => {
    // Omitting author is the same as author="user". Schema enum, two states.
    mockReadUserAuth.mockResolvedValue(null);
    const elicit = vi.fn<ElicitFn>();
    const ctx = makeCtx(elicit);

    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });

    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect(r.machine?.kind).toBe("feedback.blocked");
    expect((r.result as { code?: string }).code).toBe("auth_required");
    expect(elicit).not.toHaveBeenCalled();
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("approval prompt advertises the cached GitHub user when one is cached", async () => {
    mockReadUserAuth.mockResolvedValue({
      token: "ghu_abc",
      login: "tester",
      authorized_at: "2026-05-20T00:00:00Z",
    });
    mockSubmitFeedback.mockResolvedValue({
      kind: "submitted",
      url: "https://github.com/x/y/issues/42",
      number: 42,
      authoredBy: "tester",
      authoredAs: "user",
    });
    let promptShown = "";
    const elicit = vi.fn<ElicitFn>().mockImplementation(async (p) => {
      promptShown = p.message;
      return { action: "accept", content: { decision: "approve" } } as ElicitResult;
    });
    const ctx = makeCtx(elicit);

    await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });

    expect(promptShown).toContain("@tester");
    const [, , , opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: false });
  });

  it("author=\"bot\" posts as bot regardless of cached auth", async () => {
    mockReadUserAuth.mockResolvedValue({
      token: "ghu_abc",
      login: "tester",
      authorized_at: "2026-05-20T00:00:00Z",
    });
    mockSubmitFeedback.mockResolvedValue({
      kind: "submitted",
      url: "https://github.com/x/y/issues/42",
      number: 42,
      authoredBy: "ue-mcp-feedback[bot]",
      authoredAs: "bot",
    });
    let promptShown = "";
    const elicit = vi.fn<ElicitFn>().mockImplementation(async (p) => {
      promptShown = p.message;
      return { action: "accept", content: { decision: "approve" } } as ElicitResult;
    });
    const ctx = makeCtx(elicit);

    await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
      author: "bot",
    });

    expect(promptShown).toContain("ue-mcp-feedback bot");
    const [, , , opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: true });
  });

  it("if the elicitation request itself fails, treat as not-approved and do not submit", async () => {
    const elicit = vi.fn<ElicitFn>().mockRejectedValue(new Error("transport closed"));
    const ctx = makeCtx(elicit);
    const r = await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect((r.result as { code?: string }).code).toBe("elicitation_failed");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("posts the EXACT bytes the user saw — agent params after approval cannot mutate them", async () => {
    // The handler only consults `params` once, before elicitation, and posts
    // the captured payload after. Nothing in the API exposes a way to mutate
    // between approval and post. Lock that behavior with a test that
    // inspects the posted body against the prompt's message field.
    let promptBody = "";
    const elicit = vi
      .fn<ElicitFn>()
      .mockImplementation(async (p) => {
        promptBody = p.message;
        return { action: "accept", content: { decision: "approve" } } as ElicitResult;
      });
    const ctx = makeCtx(elicit);
    mockSubmitFeedback.mockResolvedValue({
      kind: "submitted",
      url: "https://github.com/x/y/issues/42",
      number: 42,
      authoredBy: "tester",
      authoredAs: "user",
    });

    await call(ctx, {
      title: realTitle,
      summary: realSummary,
      pythonWorkaround: realPy,
    });
    const [, postedBody] = mockSubmitFeedback.mock.calls[0];
    // The body in the prompt and the body posted must agree on every line.
    expect(promptBody).toContain(postedBody as string);
  });
});
