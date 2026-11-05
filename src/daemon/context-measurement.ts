/**
 * The file format for context strategy measurements: which strategy costs
 * least, per model vendor and per number of categories a task needs. A client
 * that runs the same tasks under micro, lean and full writes one of these, and
 * anything comparing clients reads it back through parseContextMeasurement.
 *
 * schemas/context-measurement.schema.json describes the same shape for tools
 * that are not TypeScript; a unit test keeps the two agreeing.
 */

export type ContextScopeBucket = "1-3" | "4-8" | "9+";

export interface ContextMeasurementTask {
  /** The task's stable id, the same across runs so they can be compared. */
  id: string;
  /** Every token the task spent: seed, discovery and work, input and output. */
  totalTokens: number;
  /** Model requests the task made. */
  modelCalls: number;
  /** Of the tool calls, those that only discovered the surface (search, describe, list). */
  discoveryCalls: number;
  passed: boolean;
}

export interface ContextMeasurementRun {
  model: { vendor: string; model: string };
  /** How many categories the run's context was scoped to, bucketed by scopeBucket. */
  scope: ContextScopeBucket;
  strategy: "micro" | "lean" | "full";
  /** The projection's seedTokens for that strategy and scope. */
  seedTokens: number;
  tasks: ContextMeasurementTask[];
}

export interface ContextMeasurement {
  version: 1;
  /** ISO 8601 date-time the measurement was taken. */
  measuredAt: string;
  /** The ue-mcp version measured. */
  ueMcpVersion: string;
  client?: { name: string; version: string };
  runs: ContextMeasurementRun[];
}

export type ContextMeasurementParse = { ok: true; value: ContextMeasurement } | { ok: false; errors: string[] };

const STRATEGIES = ["micro", "lean", "full"];
const SCOPES = ["1-3", "4-8", "9+"];
const ISO_DATE_TIME = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/;

/** The bucket a context scoped to `categoryCount` categories falls in. */
export function scopeBucket(categoryCount: number): ContextScopeBucket {
  if (!Number.isInteger(categoryCount) || categoryCount < 1) {
    throw new RangeError(`categoryCount is a whole number of at least 1, not ${categoryCount}`);
  }
  return categoryCount <= 3 ? "1-3" : categoryCount <= 8 ? "4-8" : "9+";
}

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

/**
 * Check a parsed JSON value against the format. Every problem is reported, by
 * path, rather than the first; unknown keys are refused so a typo in a field
 * name does not pass as a measurement without it.
 */
export function parseContextMeasurement(value: unknown): ContextMeasurementParse {
  const errors: string[] = [];
  const keys = (at: string, v: Record<string, unknown>, allowed: string[]) => {
    for (const k of Object.keys(v)) if (!allowed.includes(k)) errors.push(`${at}.${k} is not part of the format`);
  };
  const str = (at: string, v: unknown, nonEmpty = true) => {
    if (typeof v !== "string" || (nonEmpty && v.length === 0)) errors.push(`${at} must be a non-empty string`);
  };
  const count = (at: string, v: unknown) => {
    if (typeof v !== "number" || !Number.isInteger(v) || v < 0) errors.push(`${at} must be a whole number of at least 0`);
  };

  if (!isObject(value)) return { ok: false, errors: ["the measurement must be an object"] };
  keys("$", value, ["version", "measuredAt", "ueMcpVersion", "client", "runs"]);
  if (value.version !== 1) errors.push("$.version must be 1");
  if (typeof value.measuredAt !== "string" || !ISO_DATE_TIME.test(value.measuredAt)) {
    errors.push("$.measuredAt must be an ISO 8601 date-time");
  }
  str("$.ueMcpVersion", value.ueMcpVersion);
  if (value.client !== undefined) {
    if (!isObject(value.client)) errors.push("$.client must be an object");
    else {
      keys("$.client", value.client, ["name", "version"]);
      str("$.client.name", value.client.name);
      str("$.client.version", value.client.version);
    }
  }
  if (!Array.isArray(value.runs)) errors.push("$.runs must be an array");
  else {
    value.runs.forEach((run, i) => {
      const at = `$.runs[${i}]`;
      if (!isObject(run)) return void errors.push(`${at} must be an object`);
      keys(at, run, ["model", "scope", "strategy", "seedTokens", "tasks"]);
      if (!isObject(run.model)) errors.push(`${at}.model must be an object`);
      else {
        keys(`${at}.model`, run.model, ["vendor", "model"]);
        str(`${at}.model.vendor`, run.model.vendor);
        str(`${at}.model.model`, run.model.model);
      }
      if (!SCOPES.includes(run.scope as string)) errors.push(`${at}.scope must be one of ${SCOPES.join(", ")}`);
      if (!STRATEGIES.includes(run.strategy as string)) errors.push(`${at}.strategy must be one of ${STRATEGIES.join(", ")}`);
      count(`${at}.seedTokens`, run.seedTokens);
      if (!Array.isArray(run.tasks)) return void errors.push(`${at}.tasks must be an array`);
      run.tasks.forEach((task, j) => {
        const tat = `${at}.tasks[${j}]`;
        if (!isObject(task)) return void errors.push(`${tat} must be an object`);
        keys(tat, task, ["id", "totalTokens", "modelCalls", "discoveryCalls", "passed"]);
        str(`${tat}.id`, task.id);
        count(`${tat}.totalTokens`, task.totalTokens);
        count(`${tat}.modelCalls`, task.modelCalls);
        count(`${tat}.discoveryCalls`, task.discoveryCalls);
        if (typeof task.passed !== "boolean") errors.push(`${tat}.passed must be true or false`);
      });
    });
  }
  return errors.length === 0 ? { ok: true, value: value as unknown as ContextMeasurement } : { ok: false, errors };
}
