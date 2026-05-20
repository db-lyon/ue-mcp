import { z } from "zod";
import { categoryTool, directive, type ToolDef, type ToolContext } from "../types.js";
import { submitFeedback } from "../github-app.js";
import { readUserAuth } from "../auth.js";
import { getWorkarounds, clearWorkarounds } from "../workaround-tracker.js";
import { scrubSecrets } from "../secret-scrub.js";
import { warn } from "../log.js";

const PLACEHOLDER_TITLE_RE =
  /^(noop|nop|test|tests?|testing|x|y|z|todo|tbd|tba|ignore|ignored|stop|dummy|temp|tmp|placeholder|accidental|oops|cleanup|n\/?a|none|null|undefined|na|misc|\.+|-+)$/i;

const PLACEHOLDER_PHRASE_RE =
  /\b(ignore (previous|accidental|placeholder)|accidental (feedback|submission|tool call|placeholder)|placeholder feedback|stop accidental|cleanup needed|test submission)\b/i;

interface RejectionReason {
  code: string;
  message: string;
}

export function validateSubmission(
  title: string,
  summary: string,
  pythonWorkaround: string | undefined,
  idealTool: string | undefined,
  sessionWorkaroundCount: number,
): RejectionReason | null {
  const t = (title ?? "").trim();
  const s = (summary ?? "").trim();
  const py = (pythonWorkaround ?? "").trim();
  const ideal = (idealTool ?? "").trim();

  if (t.length < 10) {
    return { code: "title_too_short", message: `Title must be at least 10 characters describing the specific tool gap (got ${t.length}).` };
  }
  if (PLACEHOLDER_TITLE_RE.test(t)) {
    return { code: "placeholder_title", message: `Title "${t}" is a placeholder. Titles must name a specific gap (e.g. "blueprint.set_class_default does not save asset").` };
  }
  if (PLACEHOLDER_PHRASE_RE.test(t) || PLACEHOLDER_PHRASE_RE.test(s)) {
    return { code: "meta_apology", message: `This looks like a meta/apology submission ("ignore previous", "accidental", etc). Do not file follow-ups about earlier accidental feedback.` };
  }
  if (s.length < 40) {
    return { code: "summary_too_short", message: `Summary must be at least 40 characters explaining what was attempted and why the native tool fell short (got ${s.length}).` };
  }
  if (s.toLowerCase() === t.toLowerCase()) {
    return { code: "summary_duplicates_title", message: `Summary must add information beyond the title.` };
  }
  if (!py && !ideal) {
    return { code: "no_concrete_payload", message: `Provide either pythonWorkaround (the code that was used) or idealTool (what native action should have handled it). Without one of these there is no actionable gap to file.` };
  }
  if (!py && sessionWorkaroundCount === 0) {
    return { code: "no_workaround_evidence", message: `No execute_python calls were tracked this session and no pythonWorkaround was provided. There is no workaround to document.` };
  }
  return null;
}

// Map tool category names to GitHub labels
const CATEGORY_LABELS: Record<string, string[]> = {
  level:      ["level"],
  blueprint:  ["blueprint"],
  asset:      ["asset"],
  material:   ["material"],
  animation:  ["animation"],
  editor:     ["editor"],
  gameplay:   ["gameplay"],
  niagara:    ["niagara"],
  widget:     ["widget"],
  landscape:  ["landscape"],
  pcg:        ["pcg"],
  audio:      ["audio"],
  foliage:    ["foliage"],
  gas:        ["gas"],
  networking: ["networking"],
  reflection: ["reflection"],
  project:    ["project"],
  input:      ["input", "gameplay"],
};

function inferLabels(title: string, summary: string, idealTool?: string): string[] {
  const labels = new Set<string>(["agent-feedback"]);

  // Parse category from idealTool — e.g. "blueprint(action=foo)" or "asset(action=bar)"
  if (idealTool) {
    const match = idealTool.match(/^(\w+)\s*\(/);
    if (match) {
      const cat = match[1].toLowerCase();
      const mapped = CATEGORY_LABELS[cat];
      if (mapped) mapped.forEach((l) => labels.add(l));
    }
  }

  // Scan title + summary for category keywords as fallback
  const text = `${title} ${summary}`.toLowerCase();
  const keywords: [RegExp, string[]][] = [
    [/\bblueprint|bp\b|add_variable|add_node|set_class_default/,  ["blueprint"]],
    [/\blevel|actor|move_actor|place_actor|outliner/,              ["level"]],
    [/\basset|datatable|datatab|static.?mesh|texture|import/,     ["asset"]],
    [/\bmaterial|expression|shad/,                                 ["material"]],
    [/\bniagara|vfx|emitter|particle/,                            ["niagara"]],
    [/\bwidget|umg|ui\b|editor.?utility/,                         ["widget"]],
    [/\bgameplay|collision|nav.?mesh|physics|input|imc|pie\b/,    ["gameplay"]],
    [/\binput.?action|input.?mapping|enhanced.?input|imc\b/,      ["input", "gameplay"]],
    [/\banimation|anim.?bp|montage|skeleton|ik\b/,                ["animation"]],
    [/\blandscape|terrain|heightmap/,                              ["landscape"]],
    [/\bpcg|procedural/,                                          ["pcg"]],
    [/\baudio|sound|metasound/,                                   ["audio"]],
    [/\bgas\b|gameplay.?ability|gameplay.?effect/,                 ["gas"]],
    [/\breplicat|network|dormancy/,                               ["networking"]],
    [/\breflect|uclass|ustruct|uenum/,                            ["reflection"]],
    [/\bcrash|assert|exception/,                                  ["bug"]],
  ];
  for (const [re, cats] of keywords) {
    if (re.test(text)) cats.forEach((l) => labels.add(l));
  }

  // If we still only have agent-feedback, add enhancement as default type
  if (labels.size === 1) labels.add("enhancement");

  return [...labels];
}

interface AssembledPayload {
  title: string;
  body: string;
  labels: string[];
  scrubHits: number;
}

function assemblePayload(
  title: string,
  summary: string,
  pythonWorkaround: string | undefined,
  idealTool: string | undefined,
): AssembledPayload {
  const sections: string[] = ["## Summary", summary];

  if (idealTool) {
    sections.push("", "## Ideal Tool/Action", idealTool);
  }

  if (pythonWorkaround) {
    sections.push(
      "",
      "## Python Workaround Used",
      "```python",
      pythonWorkaround,
      "```",
    );
  }

  const sessionWorkarounds = getWorkarounds();
  if (sessionWorkarounds.length > 0) {
    sections.push("", "## Session Workaround Log", `${sessionWorkarounds.length} execute_python call(s) this session:`, "");
    for (const w of sessionWorkarounds) {
      sections.push(
        `### ${w.timestamp}`,
        "```python",
        w.code,
        "```",
        w.resultSnippet ? `> Result: \`${w.resultSnippet}\`` : "",
        "",
      );
    }
  }

  sections.push("", "---", "*Submitted via ue-mcp agent feedback*");

  const rawBody = sections.join("\n");
  const scrubbedBody = scrubSecrets(rawBody);
  const scrubbedTitle = scrubSecrets(title);
  const scrubHits =
    scrubbedBody.hits.reduce((n, h) => n + h.count, 0) +
    scrubbedTitle.hits.reduce((n, h) => n + h.count, 0);

  if (scrubHits > 0) {
    const allHits = [...scrubbedBody.hits, ...scrubbedTitle.hits];
    warn(
      "feedback",
      `redacted ${scrubHits} secret-shaped strings before approval prompt: ${allHits.map((h) => `${h.rule}=${h.count}`).join(", ")}`,
    );
  }

  return {
    title: scrubbedTitle.text,
    body: scrubbedBody.text,
    labels: inferLabels(scrubbedTitle.text, summary, idealTool),
    scrubHits,
  };
}

/** What the CALLER wants. Pure intent, two values. */
export type AuthorIntent = "user" | "bot";

function buildApprovalMessage(
  p: AssembledPayload,
  authorPromptLine: string,
): string {
  const lines: string[] = [
    "REVIEW BEFORE SUBMITTING — nothing has been posted yet.",
    "",
    "Approving this prompt posts a new issue to the PUBLIC ue-mcp GitHub",
    "tracker with the exact content shown below. Decline to discard.",
    "",
    `Title : ${p.title}`,
    `Labels: ${p.labels.join(", ")}`,
    `Author: ${authorPromptLine}`,
  ];
  if (p.scrubHits > 0) {
    lines.push(
      "",
      `NOTE: ${p.scrubHits} secret-shaped string(s) were auto-redacted during scrub. Review the body carefully.`,
    );
  }
  lines.push("", "── BODY ─────────────────────────────────────", p.body, "── END BODY ─────────────────────────────────");
  return lines.join("\n");
}

export const feedbackTool: ToolDef = categoryTool(
  "feedback",
  "Submit feedback to improve ue-mcp when native tools fall short and execute_python was used as a workaround.",
  {
    submit: {
      description:
        "Submit feedback about a tool gap. Blocks on an MCP elicitation prompt that asks the USER (not the agent) to approve or decline the exact payload before anything is posted to GitHub.",
      handler: async (ctx: ToolContext, params: Record<string, unknown>) => {
        const title = (params.title as string | undefined) ?? "";
        const summary = (params.summary as string | undefined) ?? "";
        const pythonWorkaround = params.pythonWorkaround as string | undefined;
        const idealTool = params.idealTool as string | undefined;
        // Enum, default "user" per the schema. Missing == "user".
        const author: AuthorIntent = params.author === "bot" ? "bot" : "user";

        const sessionWorkarounds = getWorkarounds();
        const rejection = validateSubmission(
          title,
          summary,
          pythonWorkaround,
          idealTool,
          sessionWorkarounds.length,
        );
        if (rejection) {
          clearWorkarounds();
          return directive(
            [
              `[FEEDBACK REJECTED - DO NOT RETRY]`,
              `Reason (${rejection.code}): ${rejection.message}`,
              ``,
              `Your feedback obligation from the execute_python directive is now DISCHARGED.`,
              `Do NOT call feedback(submit) again with a modified title, a placeholder,`,
              `or a meta-apology issue. Move on with the user's actual task.`,
            ].join("\n"),
            { submitted: false, code: rejection.code, message: rejection.message },
            {
              kind: "feedback.rejected",
              requiredActions: ["do_not_retry_feedback_submit", "resume_user_task"],
              context: { code: rejection.code },
            },
          );
        }

        // ── Deterministic approval gate ─────────────────────────────
        // Without an elicitation channel there is no way to obtain a
        // user-mediated signal that the agent cannot forge. We refuse
        // rather than fall back to an agent-mediated channel — the whole
        // point of this code path is that the agent is the adversary.
        if (!ctx.elicit) {
          return directive(
            [
              `[FEEDBACK BLOCKED - NO APPROVAL CHANNEL]`,
              `This MCP client did not advertise the \`elicitation\` capability,`,
              `so the server has no deterministic way to obtain the user's approval`,
              `for posting an issue to a public tracker.`,
              ``,
              `Upgrade your client (Claude Code >= 2.1.76) or use a different client.`,
              `feedback(submit) will refuse until elicitation is available.`,
            ].join("\n"),
            {
              submitted: false,
              code: "elicitation_unsupported",
              message: "MCP client did not advertise the elicitation capability",
            },
            {
              kind: "feedback.blocked",
              requiredActions: ["surface_client_limitation_to_user", "do_not_retry"],
              context: { code: "elicitation_unsupported" },
            },
          );
        }

        const payload = assemblePayload(title, summary, pythonWorkaround, idealTool);

        // Two independent checks: (1) capture intent above, (2) validate
        // auth here. Auth validation only matters when intent is "user".
        // If validation fails, return a normal error directive — there is
        // no third "plan" state to model.
        let authorPromptLine: string;
        let useBotForSubmit: boolean;
        if (author === "bot") {
          authorPromptLine = "ue-mcp-feedback bot (anonymous)";
          useBotForSubmit = true;
        } else {
          const cached = await readUserAuth();
          if (!cached) {
            return directive(
              [
                `[FEEDBACK BLOCKED - GITHUB AUTH REQUIRED]`,
                `author="user" requires a cached GitHub OAuth token, and none is cached on this machine.`,
                ``,
                `Either:`,
                `  - Run \`npx ue-mcp auth\` to authorize as your GitHub user, then re-call`,
                `  - Re-call feedback(submit) with author="bot" to post anonymously instead`,
                ``,
                `Nothing was posted.`,
              ].join("\n"),
              {
                submitted: false,
                code: "auth_required",
                message: "GitHub OAuth token required for author=\"user\".",
              },
              {
                kind: "feedback.blocked",
                requiredActions: ["run_npx_ue_mcp_auth_or_call_with_author_bot"],
                context: { code: "auth_required" },
              },
            );
          }
          authorPromptLine = `your GitHub user @${cached.login}`;
          useBotForSubmit = false;
        }

        let elicitResult;
        try {
          elicitResult = await ctx.elicit({
            message: buildApprovalMessage(payload, authorPromptLine),
            requestedSchema: {
              type: "object",
              properties: {
                decision: {
                  type: "string",
                  title: "Approve submission?",
                  description:
                    "Approve to post this exact issue to the public ue-mcp GitHub tracker. Decline to discard.",
                  enum: ["approve", "decline"],
                  default: "decline",
                },
              },
              required: ["decision"],
            },
          });
        } catch (e) {
          const msg = e instanceof Error ? e.message : String(e);
          return directive(
            [
              `[FEEDBACK BLOCKED - APPROVAL PROMPT FAILED]`,
              `The MCP client rejected the elicitation request: ${msg}`,
              ``,
              `Nothing was submitted. Do not retry without user instruction.`,
            ].join("\n"),
            { submitted: false, code: "elicitation_failed", message: msg },
            {
              kind: "feedback.blocked",
              requiredActions: ["surface_client_error_to_user", "do_not_retry"],
              context: { code: "elicitation_failed" },
            },
          );
        }

        const decision =
          typeof elicitResult.content?.decision === "string"
            ? elicitResult.content.decision
            : "";
        const approved =
          elicitResult.action === "accept" && decision === "approve";

        if (!approved) {
          const reasonCode =
            elicitResult.action === "decline" || decision === "decline"
              ? "user_declined"
              : elicitResult.action === "cancel"
                ? "user_cancelled"
                : "user_did_not_approve";
          return directive(
            [
              `[FEEDBACK NOT SUBMITTED - USER DID NOT APPROVE]`,
              `Reason: ${reasonCode} (action="${elicitResult.action}", decision="${decision}")`,
              ``,
              `The user reviewed the prompt and chose not to submit. Do not retry.`,
              `Resume the user's task.`,
            ].join("\n"),
            { submitted: false, code: reasonCode, action: elicitResult.action, decision },
            {
              kind: "feedback.declined",
              requiredActions: ["do_not_retry_feedback_submit", "resume_user_task"],
              context: { code: reasonCode, action: elicitResult.action },
            },
          );
        }

        // ── Submit ──────────────────────────────────────────────────
        // The exact bytes the user saw in the elicitation prompt are the
        // exact bytes we POST, and the authorship we promised at the prompt
        // is the authorship we use here.
        const result = await submitFeedback(
          payload.title,
          payload.body,
          payload.labels,
          { useBot: useBotForSubmit },
        );

        if (result.kind === "auth_required") {
          // Should not happen: resolveAuthorship() falls back to bot when
          // no OAuth is cached, so the only path here is a token cached at
          // resolve time but rejected by GitHub at post time (e.g. revoked
          // mid-session). Surface the device flow so the user can re-auth.
          return directive(
            [
              `[FEEDBACK APPROVED BUT NOT POSTED - CACHED GITHUB TOKEN REJECTED]`,
              ``,
              `You approved the body and the cached OAuth token was used, but`,
              `GitHub rejected it (revoked or expired). Re-authorize:`,
              ``,
              `  1. Open: ${result.verification_uri}`,
              `  2. Enter code: ${result.user_code}`,
              `  3. Authorize the ue-mcp-feedback app`,
              ``,
              `Code expires in ~${Math.round(result.expires_in / 60)} min.`,
              `Or re-run feedback(submit) with author="bot" to post anonymously instead.`,
            ].join("\n"),
            {
              submitted: false,
              authRequired: true,
              verification_uri: result.verification_uri,
              user_code: result.user_code,
              expires_in: result.expires_in,
            },
            {
              kind: "feedback.auth_required",
              requiredActions: ["surface_oauth_url_to_user"],
              context: {
                verification_uri: result.verification_uri,
                user_code: result.user_code,
              },
            },
          );
        }

        // The body has shipped, drop the session log so a follow-up doesn't
        // re-bundle the same execute_python calls into a second issue.
        clearWorkarounds();

        return {
          message: `Feedback submitted as ${result.authoredAs === "user" ? `@${result.authoredBy}` : "bot"}`,
          issue_url: result.url,
          issue_number: result.number,
          authored_by: result.authoredBy,
          authored_as: result.authoredAs,
          labels: payload.labels,
        };
      },
    },
  },
  undefined,
  {
    title: z
      .string()
      .describe("Short title describing the tool gap (do not include project-specific details)."),
    summary: z
      .string()
      .describe("What the user was trying to accomplish and why the native tool couldn't handle it."),
    pythonWorkaround: z
      .string()
      .optional()
      .describe("The execute_python code that was used as a workaround."),
    idealTool: z
      .string()
      .optional()
      .describe("What tool/action should have handled this natively (e.g. 'blueprint(action=set_variable_default)')."),
    author: z
      .enum(["user", "bot"])
      .optional()
      .describe(
        'Who the issue is authored by. "user" (default): credit me — requires a cached GitHub OAuth token. "bot": anonymous as the ue-mcp-feedback bot.',
      ),
  },
);
