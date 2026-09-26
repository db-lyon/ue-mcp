import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { type ToolContext, type ElicitFn, type ElicitResult, type PluginInfo } from "../../src/types.js";
import { isDirectiveResponse } from "../../src/directive.js";
import { clearWorkarounds } from "../../src/workaround-tracker.js";
import type { RegistryPlugin } from "../../src/registry-catalog.js";

/**
 * feedback(submit) with plugin routing live: the same approval gate, but the
 * issue is aimed at the repo that owns the surface being reported.
 *
 * The registry catalog is stubbed at the module boundary so nothing here
 * touches the network; everything else (routing, scrubbing, elicitation,
 * submission) is the real code path.
 */

const CATALOG: RegistryPlugin[] = [
  {
    slug: "pie-studio",
    name: "PIE Studio",
    packageName: "pie-studio",
    repoUrl: "https://github.com/db-lyon/pie-studio",
    repoPrivate: false,
    tagline: "Record, replay, observe, and inject input in Play-In-Editor sessions.",
    tags: ["testing", "replay", "input-injection"],
    status: "published",
  },
];

vi.mock("../../src/registry-catalog.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../src/registry-catalog.js")>();
  return { ...actual, fetchRegistryCatalog: async () => CATALOG };
});

const mockSubmitFeedback = vi.fn();
vi.mock("../../src/github-app.js", () => ({
  submitFeedback: (...args: unknown[]) => mockSubmitFeedback(...args),
}));

const mockReadUserAuth = vi.fn();
vi.mock("../../src/auth.js", () => ({
  readUserAuth: () => mockReadUserAuth(),
}));

const { feedbackTool } = await import("../../src/tools/feedback.js");
const { clearCoreSurfaceCache } = await import("../../src/feedback-routing.js");

const PIE_REPO = { owner: "db-lyon", repo: "pie-studio" };
const CORE = { owner: "db-lyon", repo: "ue-mcp" };

const pieTitle = "pie(replay) diverges from the recorded run after 200 frames";
const pieSummary =
  "Replaying a recorded PIE session drifts from the capture: the pawn ends up several metres from where it was recorded, and the profile reports no divergence at all.";

function pieStudioPlugin(): PluginInfo {
  return {
    name: "pie-studio",
    version: "1.0.0",
    actionPrefix: "pie",
    status: "active",
    degraded: [],
    injected: {},
    provided: { pie: ["replay", "record", "observe"] },
    knowledge: {},
    flows: [],
    tasks: [],
    pkgDir: "/does/not/exist",
    manifestPath: "/does/not/exist/ue-mcp.plugin.yml",
  };
}

function makeCtx(elicit?: ElicitFn, plugins: PluginInfo[] = []): ToolContext {
  return {
    bridge: {} as never,
    project: { projectName: null, projectDir: null, config: {} } as never,
    elicit,
    getPlugins: () => plugins,
  };
}

async function submit(ctx: ToolContext, params: Record<string, unknown>): Promise<unknown> {
  return feedbackTool.actions.submit.handler!(ctx, { action: "submit", ...params });
}

async function route(ctx: ToolContext, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  return (await feedbackTool.actions.route.handler!(ctx, { action: "route", ...params })) as Record<string, unknown>;
}

const CACHED_USER = { token: "ghu_abc", login: "tester", authorized_at: "2026-05-20T00:00:00Z" };

// Redirect ~/.ue-mcp/state.json to a per-run temp file. resolveFeedbackMode
// falls back to the user-scoped preference when no env override is set, so
// without this these tests read the developer's real feedback mode and a
// machine set to auto-approve skips the approval gate they assert on.
let stateRoot: string;
beforeEach(() => {
  stateRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-routing-state-"));
  process.env.UE_MCP_USER_STATE = path.join(stateRoot, "state.json");
});
afterEach(() => {
  delete process.env.UE_MCP_USER_STATE;
  fs.rmSync(stateRoot, { recursive: true, force: true });
});

describe("feedback(submit) plugin routing", () => {
  beforeEach(() => {
    delete process.env.UE_MCP_FEEDBACK_ROUTING;
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
    clearWorkarounds();
    clearCoreSurfaceCache();
    mockSubmitFeedback.mockReset();
    mockReadUserAuth.mockReset();
    mockReadUserAuth.mockResolvedValue(CACHED_USER);
    mockSubmitFeedback.mockResolvedValue({
      kind: "submitted",
      url: "https://github.com/db-lyon/pie-studio/issues/7",
      number: 7,
      authoredBy: "tester",
      authoredAs: "user",
      repo: "db-lyon/pie-studio",
    });
  });

  afterEach(() => {
    delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
    delete process.env.UE_MCP_FEEDBACK_ROUTING;
  });

  it("posts a plugin-owned report to the plugin tracker", async () => {
    let promptShown = "";
    const elicit = vi.fn<ElicitFn>().mockImplementation(async (p) => {
      promptShown = p.message;
      return { action: "accept" } as ElicitResult;
    });

    const r = await submit(makeCtx(elicit, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
      idealTool: "pie(action=replay)",
    });

    // The user is told, at consent time, which public tracker this goes to.
    expect(promptShown).toContain("db-lyon/pie-studio");
    expect(promptShown).toContain("PIE Studio");

    const [, body, labels, opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: false, repo: PIE_REPO });
    // Core category labels are meaningless on a plugin repo.
    expect(labels).toEqual(["agent-feedback"]);
    expect(body).toContain("## Routing");
    expect((r as { target_repo?: string }).target_repo).toBe("db-lyon/pie-studio");
  });

  it("offers a tracker field and honours a flip back to core without changing the body", async () => {
    let shownBody = "";
    let schema: Record<string, unknown> = {};
    const elicit = vi.fn<ElicitFn>().mockImplementation(async (p) => {
      shownBody = p.message;
      schema = p.requestedSchema.properties as Record<string, unknown>;
      return { action: "accept", content: { destination: "db-lyon/ue-mcp" } } as ElicitResult;
    });

    await submit(makeCtx(elicit, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
      idealTool: "pie(action=replay)",
    });

    expect((schema.destination as { enum?: string[] }).enum).toEqual([
      "db-lyon/pie-studio",
      "db-lyon/ue-mcp",
    ]);

    const [, postedBody, , opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: false, repo: CORE });
    // The bytes the user read are the bytes that posted, tracker flip or not.
    expect(shownBody).toContain(postedBody as string);
  });

  it("ignores a destination the form never offered", async () => {
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({
      action: "accept",
      content: { destination: "someone-else/elsewhere" },
    } as ElicitResult);

    await submit(makeCtx(elicit, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
      idealTool: "pie(action=replay)",
    });

    const [, , , opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: false, repo: PIE_REPO });
  });

  it("keeps a core report on core and adds no tracker field", async () => {
    let schema: Record<string, unknown> = {};
    const elicit = vi.fn<ElicitFn>().mockImplementation(async (p) => {
      schema = p.requestedSchema.properties as Record<string, unknown>;
      return { action: "accept" } as ElicitResult;
    });

    await submit(makeCtx(elicit, [pieStudioPlugin()]), {
      title: "blueprint(set_class_default) does not save the asset",
      summary:
        "Setting a class default marks the blueprint dirty but never saves it, so the change is lost unless a separate save call runs afterwards.",
      idealTool: "blueprint(action=set_class_default)",
    });

    expect(schema.destination).toBeUndefined();
    const [, , labels, opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: false, repo: CORE });
    expect(labels).toContain("blueprint");
  });

  it("hands back a prefilled URL when the plugin tracker refuses the issue", async () => {
    mockSubmitFeedback.mockResolvedValue({
      kind: "repo_unavailable",
      repo: "db-lyon/pie-studio",
      status: 410,
      message: "Issues are disabled for this repo",
    });
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "accept" } as ElicitResult);

    const r = await submit(makeCtx(elicit, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
      idealTool: "pie(action=replay)",
    });

    expect(isDirectiveResponse(r)).toBe(true);
    if (!isDirectiveResponse(r)) return;
    expect((r.result as { code?: string }).code).toBe("repo_unavailable");
    expect((r.result as { manual_url?: string }).manual_url).toContain("db-lyon/pie-studio/issues/new");
    // It must NOT silently re-file somewhere the user never approved.
    expect(mockSubmitFeedback).toHaveBeenCalledTimes(1);
  });

  it("UE_MCP_FEEDBACK_ROUTING=off pins everything to core", async () => {
    process.env.UE_MCP_FEEDBACK_ROUTING = "off";
    const elicit = vi.fn<ElicitFn>().mockResolvedValue({ action: "accept" } as ElicitResult);

    await submit(makeCtx(elicit, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
      idealTool: "pie(action=replay)",
    });

    const [, , , opts] = mockSubmitFeedback.mock.calls[0];
    expect(opts).toEqual({ useBot: false, repo: CORE });
  });

  it("defer mode records the routed tracker on the pending entry", async () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-routing-defer-"));
    process.env.UE_MCP_PENDING_DIR = tmp;
    process.env.UE_MCP_FEEDBACK_MODE = "defer";
    try {
      const r = await submit(makeCtx(undefined, [pieStudioPlugin()]), {
        title: pieTitle,
        summary: pieSummary,
        idealTool: "pie(action=replay)",
      });

      expect((r as { target_repo?: string }).target_repo).toBe("db-lyon/pie-studio");
      const files = fs.readdirSync(tmp);
      const entry = JSON.parse(fs.readFileSync(path.join(tmp, files[0]), "utf-8")) as {
        repo: string;
        routing: string;
        labels: string[];
      };
      expect(entry.repo).toBe("db-lyon/pie-studio");
      expect(entry.routing).toContain("PIE Studio");
      expect(entry.labels).toEqual(["agent-feedback"]);
      expect(mockSubmitFeedback).not.toHaveBeenCalled();
    } finally {
      delete process.env.UE_MCP_PENDING_DIR;
      delete process.env.UE_MCP_FEEDBACK_MODE;
      fs.rmSync(tmp, { recursive: true, force: true });
    }
  });

  it("auto-approve falls back to core when the plugin tracker refuses", async () => {
    process.env.UE_MCP_FEEDBACK_MODE = "auto-approve";
    try {
      mockSubmitFeedback
        .mockResolvedValueOnce({ kind: "repo_unavailable", repo: "db-lyon/pie-studio", status: 403, message: "no" })
        .mockResolvedValueOnce({
          kind: "submitted",
          url: "https://github.com/db-lyon/ue-mcp/issues/9",
          number: 9,
          authoredBy: "tester",
          authoredAs: "user",
          repo: "db-lyon/ue-mcp",
        });

      const r = (await submit(makeCtx(undefined, [pieStudioPlugin()]), {
        title: pieTitle,
        summary: pieSummary,
        idealTool: "pie(action=replay)",
      })) as Record<string, unknown>;

      expect(mockSubmitFeedback).toHaveBeenCalledTimes(2);
      expect(r.target_repo).toBe("db-lyon/ue-mcp");
      expect(r.fell_back_to_core).toBe(true);
    } finally {
      delete process.env.UE_MCP_FEEDBACK_MODE;
    }
  });
});

describe("feedback(route)", () => {
  beforeEach(() => {
    delete process.env.UE_MCP_FEEDBACK_ROUTING;
    clearCoreSurfaceCache();
  });

  it("reports the plugin tracker without posting anything", async () => {
    mockSubmitFeedback.mockReset();
    const r = await route(makeCtx(undefined, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
      idealTool: "pie(action=replay)",
    });

    expect(r.target).toBe("plugin");
    expect(r.target_repo).toBe("db-lyon/pie-studio");
    expect((r.matched_plugin as { name: string }).name).toBe("PIE Studio");
    expect(mockSubmitFeedback).not.toHaveBeenCalled();
  });

  it("answers for an empty report instead of erroring", async () => {
    const r = await route(makeCtx(), {});
    expect(r.target).toBe("core");
    expect(r.target_repo).toBe("db-lyon/ue-mcp");
  });

  it("says so when routing is switched off", async () => {
    process.env.UE_MCP_FEEDBACK_ROUTING = "off";
    const r = await route(makeCtx(undefined, [pieStudioPlugin()]), {
      title: pieTitle,
      summary: pieSummary,
    });
    expect(r.routing_enabled).toBe(false);
    expect(r.target_repo).toBe("db-lyon/ue-mcp");
  });
});
