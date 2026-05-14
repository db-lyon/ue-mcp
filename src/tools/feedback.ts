import { z } from "zod";
import { categoryTool, directive, type ToolDef, type ToolContext } from "../types.js";
import { submitFeedback } from "../github-app.js";
import { getWorkarounds, clearWorkarounds } from "../workaround-tracker.js";

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

export const feedbackTool: ToolDef = categoryTool(
  "feedback",
  "Submit feedback to improve ue-mcp when native tools fall short and execute_python was used as a workaround.",
  {
    submit: {
      description: "Submit feedback about a tool gap. Params: title, summary, pythonWorkaround?, idealTool?",
      handler: async (_ctx: ToolContext, params: Record<string, unknown>) => {
        const title = params.title as string;
        const summary = params.summary as string;
        const pythonWorkaround = params.pythonWorkaround as string | undefined;
        const idealTool = params.idealTool as string | undefined;

        const sessionWorkarounds = getWorkarounds();
        const rejection = validateSubmission(title, summary, pythonWorkaround, idealTool, sessionWorkarounds.length);
        if (rejection) {
          // Clear the tracked workarounds so a retry doesn't keep dragging the same
          // session log along. The agent's reporting obligation is satisfied by this
          // rejection - we explicitly tell it not to retry.
          clearWorkarounds();
          return directive(
            [
              `[FEEDBACK REJECTED - DO NOT RETRY]`,
              `Reason (${rejection.code}): ${rejection.message}`,
              ``,
              `Your feedback obligation from the execute_python directive is now DISCHARGED.`,
              `Do NOT call feedback(submit) again with a modified title, a placeholder,`,
              `or a meta-apology issue. Move on with the user's actual task.`,
              ``,
              `Only file feedback when ALL of the following are true:`,
              `  - The user explicitly asked for feedback to be submitted, AND`,
              `  - You can name a specific ue-mcp tool/action that should have handled the task, AND`,
              `  - You have the python workaround (or the concrete ideal tool signature) ready.`,
            ].join("\n"),
            { submitted: false, code: rejection.code, message: rejection.message },
            {
              kind: "feedback.rejected",
              requiredActions: ["do_not_retry_feedback_submit", "resume_user_task"],
              context: { code: rejection.code },
            },
          );
        }

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
          clearWorkarounds();
        }

        sections.push("", "---", "*Submitted via ue-mcp agent feedback*");

        const body = sections.join("\n");
        const labels = inferLabels(title, summary, idealTool);
        const useBot = params.useBot === true;
        const result = await submitFeedback(title, body, labels, { useBot });

        if (result.kind === "auth_required") {
          return directive(
            [
              `[FEEDBACK AUTH REQUIRED]`,
              ``,
              `Issues should be authored by the actual reporter, not the ue-mcp-feedback bot.`,
              `Authorize ue-mcp via GitHub device flow (one time only):`,
              ``,
              `  1. Open: ${result.verification_uri}`,
              `  2. Enter code: ${result.user_code}`,
              `  3. Authorize the ue-mcp-feedback app`,
              ``,
              `Then re-call feedback(submit) with the same arguments. The token persists`,
              `in ~/.ue-mcp/auth.json so this only happens once per machine.`,
              ``,
              `Code expires in ~${Math.round(result.expires_in / 60)} min. To submit anonymously`,
              `as the bot instead, re-call with useBot=true.`,
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
              requiredActions: ["surface_oauth_url_to_user", "retry_feedback_submit_after_auth"],
              context: {
                verification_uri: result.verification_uri,
                user_code: result.user_code,
              },
            },
          );
        }

        return {
          message: `Feedback submitted as ${result.authoredAs === "user" ? `@${result.authoredBy}` : "bot"}`,
          issue_url: result.url,
          issue_number: result.number,
          authored_by: result.authoredBy,
          authored_as: result.authoredAs,
          labels,
        };
      },
    },
  },
  undefined,
  {
    title: z
      .string()
      .describe("Short title describing the tool gap (do not include project-specific details)"),
    summary: z
      .string()
      .describe("What the user was trying to accomplish and why the native tool couldn't handle it"),
    pythonWorkaround: z
      .string()
      .optional()
      .describe("The execute_python code that was used as a workaround"),
    idealTool: z
      .string()
      .optional()
      .describe("What tool/action should have handled this natively (e.g. 'blueprint(action=set_variable_default)')"),
    useBot: z
      .boolean()
      .optional()
      .describe("Submit as the ue-mcp-feedback bot instead of authoring as the real GitHub user. Default false - issues author as the user via OAuth device flow on first use."),
  },
);
