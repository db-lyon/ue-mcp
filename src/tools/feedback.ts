import { z } from "zod";
import { categoryTool, directive, type ToolDef, type ToolContext } from "../types.js";
import { submitFeedback } from "../github-app.js";
import { readUserAuth } from "../auth.js";
import { getWorkarounds, clearWorkarounds } from "../workaround-tracker.js";
import { scrubSecrets } from "../secret-scrub.js";
import { privacyScrub } from "../privacy-scrub.js";
import { deferSubmission } from "../feedback-deferred.js";
import { getFeedbackMode, type FeedbackMode } from "../user-state.js";
import { warn } from "../log.js";
import { routeFeedback, type RoutingDecision } from "../feedback-routing.js";
import { CORE_REPO, newIssueUrl, repoSlug, sameRepo, type GitHubRepo } from "../registry-catalog.js";

/**
 * Resolve the active feedback mode. Precedence (highest wins):
 *
 *   1. UE_MCP_FEEDBACK_MODE env var       - per-process override
 *   2. ~/.ue-mcp/state.json preference    - per-user-per-device, set via
 *                                            `npx ue-mcp feedback mode`
 *   3. default "interactive"
 *
 * Mode is NOT read from ue-mcp.yml. It's a per-user preference that varies
 * across machines and developers (am I at the keyboard, is this an
 * unattended run, etc.) - not project policy. The agent has no surface to
 * change this; it's set by the human running the server.
 */
function resolveFeedbackMode(_ctx: ToolContext): FeedbackMode {
  const env = (process.env.UE_MCP_FEEDBACK_MODE ?? "").trim().toLowerCase();
  if (env === "auto-approve" || env === "defer" || env === "interactive") return env;
  const pref = getFeedbackMode();
  if (pref) return pref;
  return "interactive";
}

const PLACEHOLDER_TITLE_RE =
  /^(noop|nop|test|tests?|testing|x|y|z|todo|tbd|tba|ignore|ignored|stop|dummy|temp|tmp|placeholder|accidental|oops|cleanup|n\/?a|none|null|undefined|na|misc|\.+|-+)$/i;

const PLACEHOLDER_PHRASE_RE =
  /\b(ignore (previous|accidental|placeholder)|accidental (feedback|submission|tool call|placeholder)|placeholder feedback|stop accidental|cleanup needed|test submission)\b/i;

interface RejectionReason {
  code: string;
  message: string;
}

// Gate feedback on spam/placeholder shape ONLY. A python workaround is the
// common trigger but is NOT required: crashes, missing actions, and dead-end
// gaps are all legitimate reports with nothing to "work around". Requiring an
// executed python workaround (or a session-tracked execute_python call) before
// a report can be filed was suppressing exactly the reports that matter most
// (e.g. "this action crashed the editor"). pythonWorkaround and idealTool are
// optional enrichment; a substantive title + summary is enough to file.
//
// The trailing params are kept for signature/back-compat; they no longer gate.
export function validateSubmission(
  title: string,
  summary: string,
  _pythonWorkaround?: string | undefined,
  _idealTool?: string | undefined,
  _sessionWorkaroundCount?: number,
): RejectionReason | null {
  const t = (title ?? "").trim();
  const s = (summary ?? "").trim();

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

  // Parse category from idealTool - e.g. "blueprint(action=foo)" or "asset(action=bar)"
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

/**
 * Labels that mean something on any tracker. Category labels ("blueprint",
 * "niagara") describe the ue-mcp core surface and are noise - or worse, newly
 * created clutter - on a plugin's repo.
 */
const PORTABLE_LABELS = new Set(["agent-feedback", "bug", "enhancement"]);

function labelsForRepo(labels: string[], repo: GitHubRepo): string[] {
  if (sameRepo(repo, CORE_REPO)) return labels;
  const kept = labels.filter((l) => PORTABLE_LABELS.has(l));
  return kept.length > 0 ? kept : ["agent-feedback"];
}

/**
 * The routing analysis, rendered for the issue body.
 *
 * Deliberately destination-independent: it states what the classifier found,
 * not where the report ended up. That keeps the posted bytes identical whether
 * the user accepts the suggested tracker or overrides it at the prompt, and it
 * still tells a core maintainer "this looks like it belongs to pie-studio".
 */
function routingSection(routing: RoutingDecision | null): string[] {
  if (!routing) return [];
  const winner = routing.candidate ?? routing.suggestions[0];
  if (!winner) return [];
  const where = winner.repo ? ` (${repoSlug(winner.repo)})` : "";
  const lines = ["", "## Routing", `Matched plugin: **${winner.name}**${where} - confidence ${winner.confidence}.`];
  for (const r of winner.reasons.slice(0, 5)) lines.push(`- ${r}`);
  if (routing.note) lines.push(`- ${routing.note}`);
  return lines;
}

interface AssembledPayload {
  title: string;
  body: string;
  labels: string[];
  scrubHits: number;
}

interface PrivacyInputs {
  projectRoot?: string;
  projectName?: string;
}

function assemblePayload(
  title: string,
  summary: string,
  pythonWorkaround: string | undefined,
  idealTool: string | undefined,
  privacy: PrivacyInputs,
  routing: RoutingDecision | null = null,
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

  sections.push(...routingSection(routing));

  sections.push("", "---", "*Submitted via ue-mcp agent feedback*");

  const rawBody = sections.join("\n");

  // Two-pass scrub. Order matters:
  //   1. Secret-shaped strings (PEMs, API tokens, JWTs, env-style secrets).
  //   2. Personal/project identifiers (project root path, home dir, project
  //      name, OS username). Applied second so a path that contained a
  //      secret-shaped substring has the secret redacted before we drop the
  //      surrounding path bytes.
  // The agent has no surface to bypass either pass - both are applied here
  // server-side before the body ever appears on the elicitation prompt or
  // crosses the GitHub API boundary.
  const secretsBody = scrubSecrets(rawBody);
  const privacyBody = privacyScrub(secretsBody.text, {
    projectRoot: privacy.projectRoot,
    projectName: privacy.projectName,
  });
  const secretsTitle = scrubSecrets(title);
  const privacyTitle = privacyScrub(secretsTitle.text, {
    projectRoot: privacy.projectRoot,
    projectName: privacy.projectName,
  });

  const allHits = [
    ...secretsBody.hits,
    ...privacyBody.hits,
    ...secretsTitle.hits,
    ...privacyTitle.hits,
  ];
  const scrubHits = allHits.reduce((n, h) => n + h.count, 0);

  if (scrubHits > 0) {
    warn(
      "feedback",
      `redacted ${scrubHits} string(s) before approval prompt: ${allHits.map((h) => `${h.rule}=${h.count}`).join(", ")}`,
    );
  }

  return {
    title: privacyTitle.text,
    body: privacyBody.text,
    // Inferred labels look at the (already-scrubbed) title + the
    // user-supplied summary. summary feeds keyword detection (blueprint,
    // niagara, etc.) which is structural classification, not user identity.
    labels: inferLabels(privacyTitle.text, summary, idealTool),
    scrubHits,
  };
}

/** What the CALLER wants. Pure intent, two values. */
export type AuthorIntent = "user" | "bot";

function buildApprovalMessage(
  p: AssembledPayload,
  authorPromptLine: string,
  repo: GitHubRepo,
  routing: RoutingDecision | null,
): string {
  const lines: string[] = [
    "REVIEW BEFORE SUBMITTING - nothing has been posted yet.",
    "",
    `Approving this prompt posts a new issue to the PUBLIC ${repoSlug(repo)} GitHub`,
    "tracker with the exact content shown below. Decline to discard.",
    "",
    `Title  : ${p.title}`,
    `Tracker: ${repoSlug(repo)}`,
    `Labels : ${p.labels.join(", ")}`,
    `Author : ${authorPromptLine}`,
  ];

  const winner = routing?.candidate ?? routing?.suggestions[0];
  if (winner) {
    lines.push("", "── ROUTING ──────────────────────────────────");
    if (routing?.target === "plugin") {
      lines.push(
        `This looks like a ${winner.name} issue, not a ue-mcp core issue`,
        `(confidence: ${winner.confidence}), so it is aimed at that plugin's`,
        `own tracker. Switch the Tracker field below to send it to core instead.`,
      );
    } else {
      lines.push(
        `${winner.name} also matched this report (confidence: ${winner.confidence}).`,
        `It is aimed at ue-mcp core anyway${routing?.note ? ` - ${routing.note}` : "."}`,
      );
      if (winner.repo) {
        lines.push(`Switch the Tracker field below to file it against ${repoSlug(winner.repo)}.`);
      }
    }
    for (const r of winner.reasons.slice(0, 4)) lines.push(`  - ${r}`);
  }
  if (p.scrubHits > 0) {
    lines.push(
      "",
      `NOTE: ${p.scrubHits} secret-shaped string(s) were auto-redacted during scrub. Review the body carefully.`,
    );
  }
  lines.push("", "── BODY ─────────────────────────────────────", p.body, "── END BODY ─────────────────────────────────");
  return lines.join("\n");
}

/**
 * Kill switch. `UE_MCP_FEEDBACK_ROUTING=off` (or 0/false/no) pins every
 * submission to the core tracker and skips the registry lookup entirely, for
 * air-gapped runs or anyone who wants the old single-tracker behaviour.
 */
function routingDisabled(): boolean {
  return /^(0|off|false|no)$/i.test((process.env.UE_MCP_FEEDBACK_ROUTING ?? "").trim());
}

async function resolveRouting(
  ctx: ToolContext,
  input: { title: string; summary: string; idealTool?: string; repo?: string },
): Promise<RoutingDecision | null> {
  if (routingDisabled()) return null;
  return routeFeedback({
    title: input.title,
    summary: input.summary,
    idealTool: input.idealTool,
    explicitRepo: input.repo,
    installed: ctx.getPlugins?.() ?? [],
  });
}

/** One line describing where a report is headed and why. */
function routingLine(routing: RoutingDecision | null, repo: GitHubRepo): string {
  if (!routing) return `${repoSlug(repo)} (routing disabled)`;
  const winner = routing.candidate ?? routing.suggestions[0];
  if (routing.target === "plugin" && routing.candidate) {
    return `${repoSlug(repo)} - matched ${routing.candidate.name} (${routing.candidate.confidence})`;
  }
  if (winner) return `${repoSlug(repo)} - ${routing.note ?? `${winner.name} matched but core owns this`}`;
  return repoSlug(repo);
}

export const feedbackTool: ToolDef = categoryTool(
  "feedback",
  "Submit feedback when a native tool falls short: a missing action, a wrong result, a crash, or a gap you had to work around with execute_python. A python workaround is a common trigger but is not required. Reports are routed to the tracker that owns the surface - ue-mcp core, or the plugin that provides it (PIE Studio, Perforce, Meshy, ...) - by consulting the published plugin registry.",
  {
    submit: {
      description:
        "Submit feedback about a tool gap (missing action, wrong behavior, crash, or a case you had to work around). Provide a specific title and a summary; pythonWorkaround and idealTool are optional enrichment, not prerequisites. Checks the plugin registry first and files against the owning plugin's repo when one matches, then blocks on an MCP elicitation prompt that asks the USER (not the agent) to approve or decline the exact payload - and to override the tracker - before anything is posted to GitHub.",
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
              `This was rejected only for being placeholder/spam-shaped, not for lacking`,
              `a python workaround (none is required). Do NOT call feedback(submit) again`,
              `with a modified title, a placeholder, or a meta-apology issue. If you have a`,
              `genuine, specific gap to report, file it once with a real title and summary;`,
              `otherwise move on with the user's actual task.`,
            ].join("\n"),
            { submitted: false, code: rejection.code, message: rejection.message },
            {
              kind: "feedback.rejected",
              requiredActions: ["do_not_retry_feedback_submit", "resume_user_task"],
              context: { code: rejection.code },
            },
          );
        }

        // Resolve mode BEFORE checking for elicit. In interactive mode the
        // missing elicit capability is a hard block. In auto-approve or
        // defer modes the user has explicitly opted out of the elicitation
        // gate, so elicit support is irrelevant.
        const mode = resolveFeedbackMode(ctx);

        if (mode === "interactive" && !ctx.elicit) {
          return directive(
            [
              `[FEEDBACK BLOCKED - NO APPROVAL CHANNEL]`,
              `This MCP client did not advertise the \`elicitation\` capability,`,
              `so the server has no deterministic way to obtain the user's approval`,
              `for posting an issue to a public tracker.`,
              ``,
              `Upgrade your client (Claude Code >= 2.1.76), or run`,
              `\`npx ue-mcp feedback mode auto-approve|defer\` (or set the`,
              `UE_MCP_FEEDBACK_MODE env var) to skip the prompt for this session.`,
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

        // ── Routing ────────────────────────────────────────────────
        // Work out whether a published plugin owns the surface being
        // reported before assembling anything. Never fatal: a registry
        // that is down, slow, or unreachable resolves to core, which is
        // the behaviour that existed before routing did.
        let routing: RoutingDecision | null = null;
        try {
          routing = await resolveRouting(ctx, { title, summary, idealTool, repo: params.repo as string | undefined });
        } catch (e) {
          warn("feedback", "routing lookup failed; filing against ue-mcp core", e);
        }
        let targetRepo: GitHubRepo = routing?.repo ?? CORE_REPO;

        const payload = assemblePayload(
          title,
          summary,
          pythonWorkaround,
          idealTool,
          {
            projectRoot: ctx.project?.projectDir ?? undefined,
            projectName: ctx.project?.projectName ?? undefined,
          },
          routing,
        );

        // Two independent checks: (1) capture intent above, (2) validate
        // auth here. Auth validation only matters when intent is "user".
        // If validation fails, return a normal error directive - there is
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

        // ── Defer mode ─────────────────────────────────────────────
        // User has explicitly opted out of the elicitation gate via
        // `npx ue-mcp feedback mode defer` (or the env override).
        // Write the scrubbed payload to ~/.ue-mcp/pending-feedback/ for
        // later review via `npx ue-mcp feedback list/approve/discard`.
        if (mode === "defer") {
          const entry = deferSubmission(
            {
              title: payload.title,
              body: payload.body,
              labels: labelsForRepo(payload.labels, targetRepo),
              repo: repoSlug(targetRepo),
              routing: routingLine(routing, targetRepo),
            },
            ctx.project?.projectName ?? null,
            useBotForSubmit ? "bot" : "user",
          );
          clearWorkarounds();
          return {
            message: `Feedback deferred locally for later review (id ${entry.id}), aimed at ${repoSlug(targetRepo)}.`,
            deferred: true,
            id: entry.id,
            createdAt: entry.createdAt,
            mode: "defer",
            target_repo: repoSlug(targetRepo),
            routing: routingLine(routing, targetRepo),
            review_with: "npx ue-mcp feedback list",
          };
        }

        // ── Auto-approve mode ──────────────────────────────────────
        // Skip the elicitation prompt entirely and post the scrubbed
        // body. Opt-in via config/env only; the agent has no surface
        // to set this.
        if (mode === "auto-approve") {
          let postedTo = targetRepo;
          let result = await submitFeedback(
            payload.title,
            payload.body,
            labelsForRepo(payload.labels, targetRepo),
            { useBot: useBotForSubmit, repo: targetRepo },
          );
          if (result.kind === "repo_unavailable") {
            // Nobody is at the keyboard in this mode. Losing the report
            // because a plugin tracker is closed is worse than filing it on
            // core, where the routing section still names the real owner.
            warn("feedback", `${result.repo} refused the issue (HTTP ${result.status}); falling back to ue-mcp core`);
            postedTo = CORE_REPO;
            result = await submitFeedback(
              payload.title,
              payload.body,
              labelsForRepo(payload.labels, CORE_REPO),
              { useBot: useBotForSubmit, repo: CORE_REPO },
            );
          }
          if (result.kind === "repo_unavailable") {
            return {
              message: `Feedback not posted: ${result.repo} refused the issue (HTTP ${result.status}).`,
              submitted: false,
              target_repo: result.repo,
              manual_url: newIssueUrl(targetRepo, payload.title, payload.body),
            };
          }
          if (result.kind === "bot_unavailable") {
            // Anonymous submission goes through the hosted signing service, and
            // it is not answering. The body is already assembled and scrubbed,
            // so hand back the prefilled URL rather than dropping the report.
            return {
              message: `Feedback not posted: anonymous submission is unavailable. ${result.message}`,
              submitted: false,
              code: `bot_${result.code}`,
              target_repo: repoSlug(postedTo),
              manual_url: newIssueUrl(targetRepo, payload.title, payload.body),
              hint: `Run \`npx ue-mcp auth\` and re-run with author="user" to post without the anonymous path.`,
            };
          }
          if (result.kind === "auth_required") {
            return directive(
              [
                `[FEEDBACK BLOCKED - CACHED GITHUB TOKEN REJECTED]`,
                ``,
                `Auto-approve mode tried to post as your user but GitHub`,
                `rejected the cached token (revoked or expired). Re-authorize`,
                `with \`npx ue-mcp auth\` or switch to author="bot".`,
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
          clearWorkarounds();
          return {
            message: `Feedback auto-approved and submitted to ${repoSlug(postedTo)} as ${result.authoredAs === "user" ? `@${result.authoredBy}` : "bot"} (auto-approve mode).`,
            issue_url: result.url,
            issue_number: result.number,
            authored_by: result.authoredBy,
            authored_as: result.authoredAs,
            labels: labelsForRepo(payload.labels, postedTo),
            target_repo: repoSlug(postedTo),
            routing: routingLine(routing, postedTo),
            ...(sameRepo(postedTo, targetRepo) ? {} : { fell_back_to_core: true }),
            mode: "auto-approve",
          };
        }

        // ── Interactive mode (default): elicitation gate ───────────
        // NOTE: ctx.elicit is guaranteed defined here because the
        // mode === "interactive" + !ctx.elicit case returned above.
        let elicitResult;
        // #772: a client that never renders the form still answers, and it
        // answers instantly. Time the round trip so an auto-decline can be
        // reported as "no form was shown" instead of being blamed on the user,
        // who in the reported case never saw anything at all.
        const elicitStartedAt = Date.now();

        // A second tracker only appears in the form when there actually is
        // one to choose. With no plugin candidate the schema is exactly what
        // it always was: a single optional revisions field.
        const altCandidate = routing?.target === "plugin"
          ? routing.suggestions.find((s) => s.repo) ?? null
          : (routing?.suggestions ?? []).find((s) => s.repo) ?? null;
        const altRepo: GitHubRepo | null =
          routing?.target === "plugin" ? CORE_REPO : (altCandidate?.repo ?? null);
        const altLabel =
          routing?.target === "plugin"
            ? `${repoSlug(CORE_REPO)} (ue-mcp core)`
            : altCandidate
              ? `${repoSlug(altCandidate.repo!)} (${altCandidate.name})`
              : "";

        try {
          elicitResult = await ctx.elicit!({
            message: buildApprovalMessage(payload, authorPromptLine, targetRepo, routing),
            // Radio semantics on `decision` (two-value enum, mutually
            // exclusive by schema). Filling the `revisions` text field is
            // its own choice and takes precedence over `decision` - no
            // extra checkbox to tick. Three outcomes the user can express:
            //
            //   decision = submit, revisions empty   → post the body as shown
            //   decision = reject, revisions empty   → discard
            //   revisions non-empty (any decision)   → return notes to the
            //                                          agent for a body
            //                                          rewrite; nothing posts
            //                                          until re-approval
            //
            // Form-level Decline/cancel always declines, regardless of
            // field values.
            // The form-level Accept / Decline buttons are Claude Code's
            // built-in form actions - they carry the submit/discard
            // decision. We only need ONE field in the schema, for the
            // optional revisions text. The three outcomes:
            //
            //   form Decline / cancel    → discard
            //   form Accept, empty text  → submit the body as shown
            //   form Accept, text filled → return notes to the agent;
            //                              nothing posts until re-approval
            requestedSchema: {
              type: "object",
              properties: {
                revisions: {
                  type: "string",
                  title: "Submit with revisions (optional)",
                  description:
                    "Leave EMPTY and click Accept to submit the body as shown. Fill in to ask the agent to rewrite the body per these notes - nothing posts until you re-approve the revised body. Click Decline to discard.",
                },
                // Tracker override. The body is identical either way - the
                // routing section states what was matched, not where it
                // landed - so flipping this cannot change the bytes you
                // just read.
                ...(altRepo
                  ? {
                      destination: {
                        type: "string" as const,
                        title: "Tracker",
                        description: `Where the issue is filed. Defaults to ${repoSlug(targetRepo)}.`,
                        enum: [repoSlug(targetRepo), repoSlug(altRepo)],
                        enumNames: [
                          `${repoSlug(targetRepo)}${routing?.target === "plugin" ? ` (${routing.candidate?.name})` : " (ue-mcp core)"}`,
                          altLabel,
                        ],
                        default: repoSlug(targetRepo),
                      },
                    }
                  : {}),
              },
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

        const revisions =
          typeof elicitResult.content?.revisions === "string"
            ? elicitResult.content.revisions.trim()
            : "";

        // Honour a tracker flip, but only to one of the two repos the form
        // actually offered. Anything else is ignored and the default stands.
        const chosen =
          typeof elicitResult.content?.destination === "string"
            ? elicitResult.content.destination.trim()
            : "";
        if (chosen && altRepo && chosen === repoSlug(altRepo)) {
          targetRepo = altRepo;
        }

        // form-level Accept = submit. form-level Decline/cancel = discard.
        // Revisions text presence routes to the rewrite path on Accept.
        if (elicitResult.action !== "accept") {
          // #772: no human reads a form and clicks Decline in under a second.
          // A response that fast means the client resolved the elicitation
          // itself without presenting anything, so saying "the user reviewed
          // the prompt and clicked Decline" is a false statement about someone
          // who was never asked - and it told the agent not to retry, leaving
          // no way to submit at all.
          const elapsedMs = Date.now() - elicitStartedAt;
          // Overridable so both branches are testable without sleeping, and so
          // an operator on a slow-but-real client can tune it. 0 disables the
          // heuristic entirely.
          const configured = Number(process.env.UE_MCP_ELICIT_MIN_HUMAN_MS);
          const NoHumanCouldRespondMs = Number.isFinite(configured) && configured >= 0 ? configured : 1000;
          if (NoHumanCouldRespondMs > 0 && elapsedMs < NoHumanCouldRespondMs) {
            return directive(
              [
                `[FEEDBACK NOT SUBMITTED - NO APPROVAL FORM WAS SHOWN]`,
                `The MCP client answered "${elicitResult.action}" in ${elapsedMs}ms, which is too fast for a human to have seen a form.`,
                `Treat this as the client not supporting or not rendering elicitation, NOT as the user refusing.`,
                ``,
                `Ask the user directly, in plain text, whether to submit this feedback.`,
                `Do NOT set autoApprove yourself - that bypasses the approval gate. If the user`,
                `says yes, they can re-run with it, or fix their client's elicitation support.`,
              ].join("\n"),
              {
                submitted: false,
                code: "form_not_presented",
                action: elicitResult.action,
                elapsedMs,
              },
              {
                kind: "feedback.blocked",
                requiredActions: ["ask_user_in_text", "do_not_self_approve"],
                context: { code: "form_not_presented", action: elicitResult.action, elapsedMs },
              },
            );
          }

          const reasonCode =
            elicitResult.action === "decline"
              ? "user_declined_form"
              : elicitResult.action === "cancel"
                ? "user_cancelled"
                : "user_did_not_approve";
          return directive(
            [
              `[FEEDBACK NOT SUBMITTED - USER DECLINED]`,
              `Reason: ${reasonCode} (action="${elicitResult.action}")`,
              ``,
              `The user reviewed the prompt and clicked Decline. Do not retry.`,
              `Resume the user's task.`,
            ].join("\n"),
            { submitted: false, code: reasonCode, action: elicitResult.action },
            {
              kind: "feedback.declined",
              requiredActions: ["do_not_retry_feedback_submit", "resume_user_task"],
              context: { code: reasonCode, action: elicitResult.action },
            },
          );
        }

        if (revisions) {
          // The user accepted the principle but wants the body rewritten
          // before anything posts. Hand the notes back to the agent; agent
          // revises the params and calls feedback(submit) again to surface
          // a fresh approval prompt for the revised body.
          return directive(
            [
              `[FEEDBACK NEEDS REVISION BEFORE SUBMIT]`,
              ``,
              `The user approved in principle but filled in the revisions field.`,
              `Nothing has been posted. Revision notes from the user:`,
              ``,
              revisions,
              ``,
              `Revise the title/summary/pythonWorkaround/idealTool to address`,
              `these notes and call feedback(submit) again. The user will see`,
              `a fresh approval prompt for the revised body.`,
            ].join("\n"),
            {
              submitted: false,
              code: "revisions_requested",
              revisions,
            },
            {
              kind: "feedback.revisions_requested",
              requiredActions: [
                "revise_submission_per_user_notes",
                "call_feedback_submit_again_with_revised_payload",
              ],
              context: { revisions },
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
          labelsForRepo(payload.labels, targetRepo),
          { useBot: useBotForSubmit, repo: targetRepo },
        );

        if (result.kind === "bot_unavailable") {
          // The user approved an anonymous post and the hosted signing service
          // could not make one. The approved bytes are still good, so give them
          // the two doors that do not need that service.
          const manualUrl = newIssueUrl(targetRepo, payload.title, payload.body);
          return directive(
            [
              `[FEEDBACK APPROVED BUT NOT POSTED - ANONYMOUS SUBMISSION UNAVAILABLE]`,
              `${result.message}`,
              ``,
              `Anonymous reports are signed by a hosted service so this package`,
              `carries no credentials. That service did not take the report.`,
              ``,
              `Nothing was posted. Two options for the user:`,
              `  1. Open it manually (body prefilled): ${manualUrl}`,
              `  2. Run \`npx ue-mcp auth\`, then re-run feedback(submit) with author="user".`,
              ``,
              `Surface both to the user; do not pick for them.`,
            ].join("\n"),
            {
              submitted: false,
              code: `bot_${result.code}`,
              target_repo: repoSlug(targetRepo),
              manual_url: manualUrl,
              ...(result.retryAfter ? { retry_after: result.retryAfter } : {}),
            },
            {
              kind: "feedback.blocked",
              requiredActions: ["surface_manual_issue_url_to_user", "offer_user_authored_submission"],
              context: { code: `bot_${result.code}` },
            },
          );
        }

        if (result.kind === "repo_unavailable") {
          // The user approved this exact body for this exact tracker and that
          // tracker said no. Re-aiming it at core without asking would post
          // content to a place they did not agree to, so hand back the two
          // honest options instead.
          const manualUrl = newIssueUrl(targetRepo, payload.title, payload.body);
          return directive(
            [
              `[FEEDBACK APPROVED BUT NOT POSTED - TRACKER REFUSED THE ISSUE]`,
              `${result.repo} returned HTTP ${result.status}. Issues may be disabled there,`,
              `or your account cannot open them on that repo.`,
              ``,
              `Nothing was posted anywhere. Two options for the user:`,
              `  1. Open it manually (body prefilled): ${manualUrl}`,
              `  2. Re-run feedback(submit) with repo="${repoSlug(CORE_REPO)}" to file it on ue-mcp core.`,
              ``,
              `Surface both to the user; do not pick for them.`,
            ].join("\n"),
            {
              submitted: false,
              code: "repo_unavailable",
              target_repo: result.repo,
              status: result.status,
              manual_url: manualUrl,
            },
            {
              kind: "feedback.blocked",
              requiredActions: ["surface_manual_issue_url_to_user", "offer_core_tracker_fallback"],
              context: { code: "repo_unavailable", target_repo: result.repo, status: result.status },
            },
          );
        }

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
          message: `Feedback submitted to ${repoSlug(targetRepo)} as ${result.authoredAs === "user" ? `@${result.authoredBy}` : "bot"}`,
          issue_url: result.url,
          issue_number: result.number,
          authored_by: result.authoredBy,
          authored_as: result.authoredAs,
          labels: labelsForRepo(payload.labels, targetRepo),
          target_repo: repoSlug(targetRepo),
          routing: routingLine(routing, targetRepo),
        };
      },
    },

    route: {
      description:
        "Dry run the tracker routing for a report without posting anything. Returns the repo the issue would be filed against, the matched plugin (if any), and why. Params: title, summary, idealTool?, repo?. Use it when you are unsure whether a gap belongs to ue-mcp core or to an installed/published plugin.",
      handler: async (ctx: ToolContext, params: Record<string, unknown>) => {
        const title = (params.title as string | undefined) ?? "";
        const summary = (params.summary as string | undefined) ?? "";
        const idealTool = params.idealTool as string | undefined;

        if (routingDisabled()) {
          return {
            target: "core",
            target_repo: repoSlug(CORE_REPO),
            routing_enabled: false,
            message: "Routing is disabled (UE_MCP_FEEDBACK_ROUTING). Everything files against ue-mcp core.",
          };
        }

        const decision = await routeFeedback({
          title,
          summary,
          idealTool,
          explicitRepo: params.repo as string | undefined,
          installed: ctx.getPlugins?.() ?? [],
        });

        return {
          target: decision.target,
          target_repo: repoSlug(decision.repo),
          confidence: decision.candidate?.confidence ?? null,
          matched_plugin: decision.candidate
            ? {
                name: decision.candidate.name,
                slug: decision.candidate.slug,
                package: decision.candidate.packageName,
                repo: decision.candidate.repo ? repoSlug(decision.candidate.repo) : null,
                installed: decision.candidate.installed,
                reasons: decision.candidate.reasons,
              }
            : null,
          suggestions: decision.suggestions.map((s) => ({
            name: s.name,
            slug: s.slug,
            repo: s.repo ? repoSlug(s.repo) : null,
            confidence: s.confidence,
            reasons: s.reasons,
          })),
          core_anchor: decision.coreAnchor,
          registry_reachable: decision.catalogAvailable,
          note: decision.note,
          message:
            decision.target === "plugin"
              ? `This report belongs on ${repoSlug(decision.repo)} (${decision.candidate?.name}). Call feedback(submit) as usual - it routes there on its own.`
              : `This report belongs on ${repoSlug(CORE_REPO)}.`,
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
        'Who the issue is authored by. "user" (default): credit me - requires a cached GitHub OAuth token. "bot": anonymous as the ue-mcp-feedback bot.',
      ),
    repo: z
      .string()
      .optional()
      .describe(
        'Override the tracker, as "owner/name". Leave it off: submit picks the right repo from the plugin registry on its own. Only ue-mcp core and repos owned by registered plugins are accepted; anything else is ignored and the report files against core.',
      ),
  },
);
