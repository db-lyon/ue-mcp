/**
 * The built-in `playtest` flow: draft a test script for a target with a model,
 * check it against the level the way pie(script_create) will, hand the
 * refusal back to the model, and save it once it passes.
 *
 *   1 playtest.gather  the target Blueprint, its input mappings, what PIE can watch;
 *                      a target path that resolves to nothing fails the flow here
 *   2 playtest.draft   agent step: no tools, a token budget, schema SCRIPT_DRAFT_SCHEMA
 *   3 playtest.check   pie(script_validate); a refusal becomes the next prompt's feedback
 *   4-7                two more draft and check rounds, each only when the last was refused
 *   8 playtest.save    pie(script_create)
 *
 * Run with flow(run, flowName: "playtest", params: { target, context? }).
 */
import type { TaskResult, TaskConstructor, OptionSpecs } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { runBridge } from "./run-action.js";
import { ModelAgentTask } from "./agent-step.js";

/** How many times a refused draft goes back to the model. */
export const PLAYTEST_RETRIES = 2;
const ATTEMPTS = PLAYTEST_RETRIES + 1;

/** Characters of each gathered section the prompt carries. */
const SECTION_CHARS = 12_000;

const OPS = ["decreasing", "increasing", "eq", "gt", "lt", "visible", "exists"];

/** The step format of pie(script_create), as the JSON Schema the agent step answers in. */
export const SCRIPT_STEP_SCHEMA: Record<string, unknown> = {
  type: "object",
  required: ["instruction"],
  additionalProperties: false,
  properties: {
    instruction: { type: "string", minLength: 1 },
    judge: { type: "string", enum: ["auto", "manual"] },
    agent_judgeable: { type: "boolean" },
    resolve_in_pie: { type: "boolean" },
    expect: {
      type: "object",
      required: ["value", "op"],
      additionalProperties: false,
      properties: {
        value: { type: "string", minLength: 1 },
        op: { type: "string", enum: OPS },
        target: { anyOf: [{ type: "number" }, { type: "boolean" }] },
        within_ms: { type: "integer", minimum: 0 },
      },
    },
  },
};

/** A drafted script: its steps and, for a script with no auto step, manual_only. */
export const SCRIPT_DRAFT_SCHEMA: Record<string, unknown> = {
  type: "object",
  required: ["steps"],
  additionalProperties: false,
  properties: {
    description: { type: "string" },
    manual_only: { type: "boolean" },
    steps: { type: "array", minItems: 1, items: SCRIPT_STEP_SCHEMA },
  },
};

const SYSTEM = [
  "You write Play In Editor test scripts for Unreal Engine as JSON.",
  "A script is an ordered list of steps. Each step has an instruction for the person playing.",
  "An auto step carries expect { value, op, target?, within_ms? } and the editor checks it on every frame;",
  "a manual step (judge: \"manual\") has no expect and waits for a person's verdict.",
  "expect.value must be one of the readable paths, actor labels, spawned classes or widgets you are given.",
  "Ops: decreasing and increasing compare with the value at step start and take no target;",
  "eq, gt and lt need a number target; a boolean value takes only eq with target true or false;",
  "an enum takes only eq with one of its values; exists takes an actor, visible a widget.",
  "Set resolve_in_pie: true only on a step whose value is created during play.",
  "Include at least one auto step. Answer with the JSON object only.",
].join(" ");

function clip(value: unknown, max = SECTION_CHARS): string {
  const text = typeof value === "string" ? value : JSON.stringify(value ?? null);
  return text.length > max ? `${text.slice(0, max)} (cut at ${max} characters)` : text;
}

/** pie(script_values)'s value list, one compact line each: path (type), enum values named. */
function watchable(values: Record<string, unknown>): string {
  return rows(values.values).map((v) => {
    const enumValues = rows(v.values).map((e) => `${String(e.value)}=${String(e.name)}`);
    return `${String(v.path)} (${String(v.type ?? v.kind)}${enumValues.length ? `: ${enumValues.join(", ")}` : ""})`;
  }).join("\n");
}

function str(v: unknown): string {
  return typeof v === "string" ? v.trim() : "";
}

function rows(v: unknown): Array<Record<string, unknown>> {
  return Array.isArray(v) ? (v as Array<Record<string, unknown>>) : [];
}

function scriptNameFor(target: string): string {
  const leaf = target.split(/[/.]/).filter(Boolean).pop() ?? "Target";
  const clean = leaf.replace(/[^A-Za-z0-9_]/g, "_").replace(/_+/g, "_").replace(/^_|_$/g, "").slice(0, 48);
  return `TS_Playtest_${clean || "Target"}`;
}

/** A bridge read that answers `success: false` instead of throwing, as the gather step treats a gap. */
async function read(task: { bridge: { call(m: string, p?: Record<string, unknown>): Promise<unknown> } }, method: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  try {
    const r = await task.bridge.call(method, params);
    return r && typeof r === "object" ? (r as Record<string, unknown>) : { result: r };
  } catch (e) {
    return { success: false, error: e instanceof Error ? e.message : String(e) };
  }
}

/** Step 1: the target Blueprint, its input mappings and the values PIE can watch. */
export class PlaytestGatherTask extends UeMcpTask<{ target?: string; context?: string; profile?: string; name?: string }> {
  /** What a run passes in params, which flow(plan) reports as the flow's inputs. */
  static optionsSchema = {
    target: { type: "string", required: true, description: "Asset path of the Blueprint to test, or a description of it to search for." },
    context: { type: "string", description: "What to test." },
    profile: { type: "string", description: "The pie(script_values) profile whose values the script may watch." },
    name: { type: "string", description: "Script name. Derived from the target when omitted." },
  } satisfies OptionSpecs;

  get taskName() { return "playtest.gather"; }

  async execute(): Promise<TaskResult> {
    const target = str(this.options.target);
    if (!target) return { success: false, error: new Error("playtest needs params.target: an asset path or a description of what to test.") };
    const context = str(this.options.context);
    const profile = str(this.options.profile);
    const self = { bridge: this.bridge };

    // An asset path is taken as given; a description is looked up by its words.
    let blueprint: string | null = null;
    let resolvedBy = "path";
    if (target.startsWith("/")) {
      blueprint = target;
    } else {
      resolvedBy = "search";
      const words = [target, ...target.split(/\s+/).filter((w) => w.length >= 4)];
      for (const query of words) {
        const found = await read(self, "search_assets", { query, limit: 20 });
        const hit = rows(found.results).find((r) => /Blueprint/i.test(String(r.className ?? "")));
        if (hit) {
          blueprint = String(hit.path);
          break;
        }
      }
    }

    const details = blueprint ? await read(self, "read_blueprint", { assetPath: blueprint }) : null;
    const inputs = await read(self, "list_input_assets", { limit: 50 });
    const contexts = rows(inputs.inputMappingContexts).slice(0, 4);
    const mappings: Array<Record<string, unknown>> = [];
    for (const imc of contexts) {
      const path = String(imc.path ?? imc.objectPath ?? "");
      if (path) mappings.push({ imc: path, read: await read(self, "read_imc", { imcPath: path }) });
    }
    const values = await read(self, "pie_script_values", { ...(blueprint ? { target: blueprint } : {}), ...(profile ? { profile } : {}), limit: 600 });
    if (values.success === false) {
      return { success: false, error: new Error(`pie(script_values) failed: ${String(values.error ?? "no answer")}`), data: { values } };
    }
    // A path that resolves to nothing fails here, before a model drafts a playtest of something else (#1610).
    const resolved = values.target && typeof values.target === "object" ? (values.target as Record<string, unknown>) : null;
    if (resolvedBy === "path" && resolved?.found === false) {
      return {
        success: false,
        error: new Error(`playtest target ${target} does not exist: no asset or actor class resolves at that path.`),
        data: { target, values: { target: resolved } },
      };
    }

    const prompt = [
      `Write a test script for: ${target}${blueprint && blueprint !== target ? ` (Blueprint ${blueprint})` : ""}.`,
      context ? `What to test: ${context}` : "",
      `Values PIE can watch, as path (type): ${clip(watchable(values), 30_000)}`,
      `The player pawn, the target and what else the level offers: ${clip({ pawn: values.pawn, target: values.target, actors: values.actors, spawned_classes: values.spawned_classes, widgets: values.widgets, profile_values: values.profile_values })}`,
      details && details.success !== false ? `The target Blueprint: ${clip(details)}` : "",
      mappings.length ? `Input mappings: ${clip(mappings)}` : "",
    ].filter(Boolean).join("\n\n");

    return {
      success: true,
      data: {
        target,
        blueprint,
        resolvedBy,
        profile,
        script_name: str(this.options.name) || scriptNameFor(blueprint ?? target),
        valueCount: rows(values.values).length,
        inputMappingContexts: contexts.length,
        prompt,
      },
    };
  }
}

interface CheckState {
  valid: boolean;
  attempt: number;
  draft?: Record<string, unknown>;
  errors: unknown[];
  retry: boolean;
  feedback?: string;
  summary?: string;
}

/** Steps 3, 5 and 7: pie(script_validate) on a draft, carried forward once one passes. */
export class PlaytestCheckTask extends UeMcpTask<{ draft?: unknown; previous?: unknown; attempt?: number; final?: boolean; profile?: string }> {
  get taskName() { return "playtest.check"; }

  async execute(): Promise<TaskResult> {
    const previous = this.options.previous as CheckState | undefined;
    if (previous && typeof previous === "object" && previous.valid) {
      return { success: true, data: { ...previous, retry: false } };
    }
    const attempt = Number(this.options.attempt ?? 1);
    const draft = this.options.draft && typeof this.options.draft === "object" ? (this.options.draft as Record<string, unknown>) : undefined;
    if (!draft || !Array.isArray(draft.steps)) {
      return { success: false, error: new Error(`playtest draft ${attempt} has no steps to check.`), data: { attempt } };
    }
    const profile = str(this.options.profile);
    const answer = await this.bridge.call("pie_script_validate", {
      steps: draft.steps,
      ...(profile ? { profile } : {}),
      ...(draft.manual_only === true ? { manual_only: true } : {}),
    }) as Record<string, unknown>;
    if (!answer || answer.success === false) {
      return { success: false, error: new Error(`pie(script_validate) failed: ${String(answer?.error ?? "no answer")}`), data: { attempt } };
    }
    const errors = Array.isArray(answer.errors) ? answer.errors : [];
    if (answer.valid === true) {
      const state: CheckState = { valid: true, attempt, draft, errors: [], retry: false };
      return { success: true, data: { ...state } };
    }
    const summary = String(answer.summary ?? "");
    if (this.options.final === true || attempt >= ATTEMPTS) {
      return {
        success: false,
        error: new Error(`The drafted script was still refused after ${attempt} attempts: ${summary}`),
        data: { valid: false, attempt, draft, errors, retry: false, summary },
      };
    }
    const feedback = [
      `Your previous draft was refused by pie(script_validate). Fix every problem and answer with the whole script again.`,
      `Previous draft: ${JSON.stringify(draft)}`,
      `Problems, as { step, field, message, candidates }: ${JSON.stringify(errors)}`,
    ].join("\n\n");
    const state: CheckState = { valid: false, attempt, draft, errors, retry: true, feedback, summary };
    return { success: true, data: { ...state } };
  }
}

/** Step 8: pie(script_create) with the draft that passed. */
export class PlaytestSaveTask extends UeMcpTask<{ checked?: unknown; name?: string; package_path?: string; profile?: string }> {
  static optionsSchema = {
    package_path: { type: "string", description: "Content folder the script is saved in." },
  } satisfies OptionSpecs;

  get taskName() { return "playtest.save"; }

  async execute(): Promise<TaskResult> {
    const checked = this.options.checked as CheckState | undefined;
    if (!checked?.valid || !checked.draft) {
      return { success: false, error: new Error("playtest has no checked draft to save."), data: { errors: checked?.errors ?? [] } };
    }
    const name = str(this.options.name);
    if (!name) return { success: false, error: new Error("playtest.save needs a script name.") };
    const profile = str(this.options.profile);
    const packagePath = str(this.options.package_path);
    const draft = checked.draft;
    return runBridge(this.ctx, "pie.script_create", "pie_script_create", undefined, undefined, {
      name,
      steps: draft.steps,
      ...(packagePath ? { package_path: packagePath } : {}),
      ...(profile ? { profile } : {}),
      ...(typeof draft.description === "string" && draft.description ? { description: draft.description } : {}),
      ...(draft.manual_only === true ? { manual_only: true } : {}),
      onConflict: "replace",
    });
  }
}

/** What the draft step runs with unless a step overrides it. */
export const PLAYTEST_DRAFT_OPTIONS: Record<string, unknown> = {
  system: SYSTEM,
  schema: SCRIPT_DRAFT_SCHEMA,
  tools: [],
  maxIterations: 2,
  tokenBudget: 120_000,
  maxTokens: 8000,
};

/** Steps 2, 4 and 6: the agent step, with the playtest defaults under the step's own options. */
class PlaytestDraftTask extends ModelAgentTask {
  constructor(ctx: ConstructorParameters<typeof ModelAgentTask>[0], options: ConstructorParameters<typeof ModelAgentTask>[1]) {
    super(ctx, { ...PLAYTEST_DRAFT_OPTIONS, ...options });
  }
  override get taskName() { return "playtest.draft"; }
}

export const PLAYTEST_TASK_CLASSES: Record<string, TaskConstructor> = {
  "playtest.draft": PlaytestDraftTask as unknown as TaskConstructor,
  "playtest.gather": PlaytestGatherTask as unknown as TaskConstructor,
  "playtest.check": PlaytestCheckTask as unknown as TaskConstructor,
  "playtest.save": PlaytestSaveTask as unknown as TaskConstructor,
};

/** Task definitions for the flow, merged into the built-in defaults. */
export function playtestTasks(): Record<string, unknown> {
  return {
    "playtest.gather": {
      class_path: "playtest.gather",
      group: "playtest",
      description: "Gather a playtest target: its Blueprint, input mappings and the values PIE can watch. Params: target, context?, profile?, name?",
    },
    "playtest.draft": {
      class_path: "playtest.draft",
      group: "playtest",
      description: "Draft a test script with the flow model: an agent step with no tools, a token budget, answered as the script step schema. Params: prompt",
    },
    "playtest.check": {
      class_path: "playtest.check",
      group: "playtest",
      description: "Check a drafted script with pie(script_validate); a refusal becomes feedback for the next draft.",
    },
    "playtest.save": {
      class_path: "playtest.save",
      group: "playtest",
      description: "Save the checked script with pie(script_create).",
    },
  };
}

/** The flow definition. */
export function playtestFlow(): Record<string, unknown> {
  const steps: Record<string, unknown> = {
    1: { task: "playtest.gather" },
    2: { task: "playtest.draft", options: { prompt: "${steps.1.prompt}" } },
    3: { task: "playtest.check", options: { draft: "${steps.2.parsed}", profile: "${steps.1.profile}", attempt: 1 } },
  };
  let n = 3;
  for (let attempt = 2; attempt <= ATTEMPTS; attempt++) {
    const check = n;
    steps[++n] = { task: "playtest.draft", when: `\${steps.${check}.retry}`, options: { prompt: `\${steps.1.prompt}\n\n\${steps.${check}.feedback}` } };
    const draft = n;
    steps[++n] = {
      task: "playtest.check",
      options: {
        draft: `\${steps.${draft}.parsed}`,
        previous: `\${steps.${check}}`,
        profile: "${steps.1.profile}",
        attempt,
        ...(attempt === ATTEMPTS ? { final: true } : {}),
      },
    };
  }
  steps[++n] = { task: "playtest.save", options: { checked: `\${steps.${n - 1}}`, name: "${steps.1.script_name}", profile: "${steps.1.profile}" } };
  return {
    playtest: {
      description:
        "Draft a PIE test script for a target with the flow model and save it. params: target (asset path or description), "
        + "context? (what to test), profile?, name?, package_path?. Gathers the target Blueprint, its input mappings and the "
        + `values PIE can watch, drafts with an agent step, checks the draft with pie(script_validate) and returns refusals to the model up to ${PLAYTEST_RETRIES} times, `
        + "then saves it with pie(script_create). Fails before drafting when target is a path that resolves to nothing, and with the "
        + "last errors when every draft is refused. Needs a model: a daemon "
        + "extension's provider, or ANTHROPIC_API_KEY.",
      steps,
    },
  };
}
