import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ToolContext, ElicitFn, ElicitResult } from "../../src/core/types.js";
import { isDirectiveResponse } from "../../src/core/directive.js";
import { clearWorkarounds } from "../../src/workaround-tracker.js";
import {
  buildPrefilledIssueUrl,
  writeFallbackReport,
  findByConfirmToken,
  MaxIssueUrlChars,
} from "../../src/feedback/feedback-fallback.js";
import { CORE_REPO } from "../../src/extensions/registry-catalog.js";
import { listDeferred } from "../../src/feedback/feedback-deferred.js";

const mockSubmitFeedback = vi.fn();
vi.mock("../../src/feedback/github-app.js", () => ({
  submitFeedback: (...args: unknown[]) => mockSubmitFeedback(...args),
}));

const mockReadUserAuth = vi.fn();
vi.mock("../../src/feedback/github-auth.js", () => ({
  readUserAuth: () => mockReadUserAuth(),
}));

const { feedbackTool } = await import("../../src/tools/feedback.js");

const realTitle = "widget.add_widget wedges on a stale WidgetBlueprint pointer";
const realSummary =
  "Every second or third add_widget call failed with 'Failed to load WidgetBlueprint' on an asset that exists on disk and is in the AssetRegistry.";

function makeCtx(elicit?: ElicitFn): ToolContext {
  const project = { projectName: null, projectDir: null, config: {} } as never;
  return { bridge: {} as never, project, elicit };
}

async function call(ctx: ToolContext, params: Record<string, unknown>): Promise<unknown> {
  return feedbackTool.actions.submit.handler!(ctx, { action: "submit", ...params });
}

let root: string;
beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-fallback-"));
  process.env.UE_MCP_USER_STATE = path.join(root, "state.json");
  process.env.UE_MCP_PENDING_DIR = path.join(root, "pending");
  process.env.UE_MCP_FEEDBACK_ROUTING = "off";
  clearWorkarounds();
  mockSubmitFeedback.mockReset();
  mockReadUserAuth.mockReset();
  mockReadUserAuth.mockResolvedValue({
    token: "ghu_abc",
    login: "tester",
    authorized_at: "2026-05-20T00:00:00Z",
  });
});
afterEach(() => {
  delete process.env.UE_MCP_USER_STATE;
  delete process.env.UE_MCP_PENDING_DIR;
  delete process.env.UE_MCP_FEEDBACK_ROUTING;
  delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
  fs.rmSync(root, { recursive: true, force: true });
});

describe("buildPrefilledIssueUrl", () => {
  it("carries the whole body when it fits", () => {
    const r = buildPrefilledIssueUrl(CORE_REPO, "a title", "a short body");
    expect(r.truncated).toBe(false);
    expect(r.url).toContain("issues/new?title=a%20title");
    expect(r.url).toContain(encodeURIComponent("a short body"));
  });

  it("measures the ENCODED length, not the plain one", () => {
    // Newlines and backticks triple in size once encoded. A body under the
    // cap in plain characters can be far over it in the actual URL, which is
    // exactly how a link ends up silently truncated by the browser.
    const body = "```\n".repeat(1200);
    expect(body.length).toBeLessThan(MaxIssueUrlChars);
    const r = buildPrefilledIssueUrl(CORE_REPO, "a title", body);
    expect(r.truncated).toBe(true);
    expect(r.url!.length).toBeLessThanOrEqual(MaxIssueUrlChars);
  });

  it("points at the saved file when it has to truncate", () => {
    const r = buildPrefilledIssueUrl(CORE_REPO, "a title", "x".repeat(20000), "/tmp/report.md");
    expect(r.truncated).toBe(true);
    expect(r.url!.length).toBeLessThanOrEqual(MaxIssueUrlChars);
    expect(decodeURIComponent(r.url!)).toContain("/tmp/report.md");
  });

  it("returns no link at all rather than a broken one", () => {
    // A title that cannot fit means no prefix of the body can either.
    const r = buildPrefilledIssueUrl(CORE_REPO, "t".repeat(MaxIssueUrlChars * 2), "body");
    expect(r.url).toBeNull();
    expect(r.truncated).toBe(true);
  });
});

describe("writeFallbackReport", () => {
  it("saves the report where the CLI already looks, with the token in the file", () => {
    const report = writeFallbackReport({
      title: realTitle,
      body: realSummary,
      labels: ["agent-feedback", "widget"],
      repo: CORE_REPO,
      routing: "db-lyon/ue-mcp (routing disabled)",
      project: null,
      author: "user",
      reason: "test",
    });
    expect(fs.existsSync(report.path)).toBe(true);
    expect(fs.readFileSync(report.path, "utf-8")).toContain(report.token);
    expect(fs.readFileSync(report.path, "utf-8")).toContain(realSummary);
    // Same store `npx ue-mcp feedback list/approve` reads.
    const pending = listDeferred();
    expect(pending).toHaveLength(1);
    expect(pending[0].id).toBe(report.id);
    expect(pending[0].confirmToken).toBe(report.token);
    expect(findByConfirmToken(report.token)?.id).toBe(report.id);
    expect(findByConfirmToken("deadbeef")).toBeNull();
  });
});

describe("feedback(submit) fallback when the gate cannot reach a human (#991)", () => {
  it("saves the report and hands back a link when the client auto-declines instantly", async () => {
    delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" } as ElicitResult);
    const r = await call(makeCtx(elicit), { title: realTitle, summary: realSummary });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    const res = r.result as Record<string, unknown>;
    expect(res.code).toBe("form_not_presented");
    expect(fs.existsSync(res.saved_report as string)).toBe(true);
    expect(res.manual_url).toContain("github.com/db-lyon/ue-mcp/issues/new?");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("never puts the confirmation token in the tool result", async () => {
    delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" } as ElicitResult);
    const r = await call(makeCtx(elicit), { title: realTitle, summary: realSummary });
    if (!isDirectiveResponse(r)) throw new Error("expected a directive");
    const token = listDeferred()[0].confirmToken!;
    expect(token).toBeTruthy();
    // The whole point of the token is that the USER reads it. Leaking it into
    // the response would let the agent approve its own submission.
    expect(JSON.stringify(r)).not.toContain(token);
  });

  it("falls back when the client never advertised elicitation", async () => {
    const r = await call(makeCtx(undefined), { title: realTitle, summary: realSummary });
    if (!isDirectiveResponse(r)) throw new Error("expected a directive");
    expect((r.result as { code?: string }).code).toBe("elicitation_unsupported");
    expect(listDeferred()).toHaveLength(1);
  });

  it("falls back when the client throws on the elicitation request", async () => {
    const elicit = vi.fn<ElicitFn>().mockRejectedValue(new Error("method not found"));
    const r = await call(makeCtx(elicit), { title: realTitle, summary: realSummary });
    if (!isDirectiveResponse(r)) throw new Error("expected a directive");
    expect((r.result as { code?: string }).code).toBe("elicitation_failed");
    expect(listDeferred()).toHaveLength(1);
  });
});

describe("feedback(submit, confirmToken)", () => {
  async function saveOne(): Promise<string> {
    delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "decline" } as ElicitResult);
    await call(makeCtx(elicit), { title: realTitle, summary: realSummary });
    return listDeferred()[0].confirmToken!;
  }

  it("posts the SAVED bytes and ignores the params on the confirming call", async () => {
    const token = await saveOne();
    const saved = listDeferred()[0];
    mockSubmitFeedback.mockResolvedValue({
      kind: "submitted",
      url: "https://github.com/db-lyon/ue-mcp/issues/991",
      number: 991,
      authoredBy: "tester",
      authoredAs: "user",
    });
    const r = await call(makeCtx(undefined), {
      confirmToken: token,
      // Deliberately different. The token approved the saved body, so this
      // must not reach GitHub.
      title: "something else entirely that the user never read",
      summary: "a body the user never approved, at least forty characters long",
    });
    expect(mockSubmitFeedback).toHaveBeenCalledTimes(1);
    const [postedTitle, postedBody] = mockSubmitFeedback.mock.calls[0];
    expect(postedTitle).toBe(saved.title);
    expect(postedBody).toBe(saved.body);
    expect((r as { issue_number?: number }).issue_number).toBe(991);
    // Consumed: the token cannot post a second issue.
    expect(listDeferred()).toHaveLength(0);
    expect(fs.existsSync(path.join(process.env.UE_MCP_PENDING_DIR!, `${saved.id}.md`))).toBe(false);
  });

  it("refuses an unknown token without posting anything", async () => {
    const r = await call(makeCtx(undefined), { confirmToken: "notarealtoken" });
    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect((r.result as { code?: string }).code).toBe("unknown_confirm_token");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("keeps the report and the token alive when the post fails", async () => {
    const token = await saveOne();
    mockSubmitFeedback.mockResolvedValue({ kind: "repo_unavailable", repo: "db-lyon/ue-mcp", status: 410 });
    const r = await call(makeCtx(undefined), { confirmToken: token });
    if (!isDirectiveResponse(r)) throw new Error("expected a directive");
    expect((r.result as { code?: string }).code).toBe("repo_unavailable");
    expect((r.result as { pending_id?: string }).pending_id).toBe(listDeferred()[0].id);
    expect(r.directive).toContain("its token still works");
    expect(listDeferred()).toHaveLength(1);
    expect(findByConfirmToken(token)).not.toBeNull();
  });
});
