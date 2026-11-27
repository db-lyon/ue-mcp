// @session independent: Pure helpers over their arguments: the hub's error type and readers of action answers.
/**
 * Small pieces every part of the Playtest Hub uses: the error a route turns
 * into its status, and reading ue-mcp's answers.
 */
import type { ActionResult } from "../contract/daemon.js";
import { StatusError } from "../core/status-error.js";
import type { HubFinding } from "./contract.js";

/** A refusal with the HTTP status its route answers: 400 a bad body, 404 nothing by that id, 409 not now. */
export class HubError extends StatusError {
  constructor(status: 400 | 404 | 409, message: string) {
    super(status, message);
  }
}

/** A body that does not read, with every finding: its route answers 400 { error, findings }. */
export class FindingError extends HubError {
  constructor(readonly findings: HubFinding[]) {
    super(400, findings.filter((f) => f.blocks).map((f) => f.message).join("; ") || "the body does not read");
  }
}

export const bad = (message: string): HubError => new HubError(400, message);
export const notFound = (message: string): HubError => new HubError(404, message);
export const conflict = (message: string): HubError => new HubError(409, message);

const text = (r: ActionResult): string => r.content.map((c) => c.text ?? "").join("\n");

/** An answer's JSON body, or null when the call failed or said success: false. */
export function answerOf(r: ActionResult | null): Record<string, unknown> | null {
  if (!r || r.isError) return null;
  try {
    const v = JSON.parse(text(r)) as unknown;
    if (!v || typeof v !== "object" || Array.isArray(v)) return null;
    return (v as Record<string, unknown>).success === false ? null : (v as Record<string, unknown>);
  } catch {
    return null;
  }
}

/** Why a call failed, or null when it worked. */
export function failureOf(r: ActionResult): string | null {
  if (r.isError) return text(r) || "the call failed";
  try {
    const v = JSON.parse(text(r)) as Record<string, unknown> | null;
    if (v && v.success === false) return typeof v.error === "string" ? v.error : "the call failed";
  } catch {
    // Not JSON: not a refusal either.
  }
  return null;
}

export const isObject = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);

/** JSON with every object's keys sorted, so the same value always gives the same text. */
export function canonical(v: unknown): string {
  if (Array.isArray(v)) return `[${v.map((x) => canonical(x === undefined ? null : x)).join(",")}]`;
  if (v && typeof v === "object") {
    const o = v as Record<string, unknown>;
    return `{${Object.keys(o).filter((k) => o[k] !== undefined).sort().map((k) => `${JSON.stringify(k)}:${canonical(o[k])}`).join(",")}}`;
  }
  return JSON.stringify(v) ?? "null";
}
