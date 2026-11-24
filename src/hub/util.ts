// @session independent: Pure helpers over their arguments: the hub's error type and readers of action answers.
/**
 * Small pieces every part of the Playtest Hub uses: the error a route turns
 * into its status, and reading ue-mcp's answers.
 */
import type { ActionResult } from "../contract/daemon.js";
import { StatusError } from "../core/status-error.js";
import { TRACKER_LINKS_MAX, parseCheckId, trackerLinkOf, uniqueLinks, type CheckRef, type TrackerLink } from "./contract.js";

/** A refusal with the HTTP status its route answers: 400 a bad body, 404 nothing by that id, 409 not now. */
export class HubError extends StatusError {
  constructor(status: 400 | 404 | 409, message: string) {
    super(status, message);
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

/** A check reference from a body or a record, normalized: a script by its asset path without the object name, however it was written. Null when it is not one. */
export function checkRefOf(v: unknown): CheckRef | null {
  if (!v || typeof v !== "object" || Array.isArray(v)) return null;
  const o = v as Record<string, unknown>;
  if (o.kind !== "script" || typeof o.target !== "string") return null;
  let target = o.target.trim();
  if (!target.startsWith("/")) return null;
  const dot = target.lastIndexOf(".");
  if (dot > target.lastIndexOf("/")) target = target.slice(0, dot);
  return { kind: "script", target, ...(o.required === false ? { required: false } : {}) };
}

/** Links from a request body: every one valid, or a 400 naming the first that is not. */
export function linksOf(v: unknown, where = "links"): TrackerLink[] {
  if (!Array.isArray(v)) throw bad(`${where} must be a list of { url, title?, rel? }`);
  if (v.length > TRACKER_LINKS_MAX) throw bad(`${where} holds at most ${TRACKER_LINKS_MAX} links`);
  return uniqueLinks(v.map((l, i) => {
    const r = trackerLinkOf(l);
    if ("problem" in r) throw bad(`${where} ${i + 1}: ${r.problem}`);
    return r.link;
  }));
}

/** One link from a request body, or a 400. */
export function linkOf(v: unknown): TrackerLink {
  const r = trackerLinkOf(v);
  if ("problem" in r) throw bad(r.problem);
  return r.link;
}

/** Links from a file: the valid ones, and what was wrong with the rest. Absent is none. */
export function readLinks(v: unknown): { links: TrackerLink[]; problems: string[] } {
  if (v === undefined || v === null) return { links: [], problems: [] };
  if (!Array.isArray(v)) return { links: [], problems: ["links is not a list"] };
  const links: TrackerLink[] = [];
  const problems: string[] = [];
  for (const [i, l] of v.entries()) {
    const r = trackerLinkOf(l);
    if ("problem" in r) problems.push(`link ${i + 1}: ${r.problem}`);
    else links.push(r.link);
  }
  return { links: uniqueLinks(links), problems };
}

/** A check id from a path parameter, already decoded by the daemon; null when it names no check. */
export function checkOfId(id: string): CheckRef | null {
  const p = parseCheckId(id);
  return p ? checkRefOf(p) : null;
}

export const isObject = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);

/** JSON with every object's keys sorted, so the same value always gives the same text: a script's version, two records' content compared. */
export function canonical(v: unknown): string {
  if (Array.isArray(v)) return `[${v.map((x) => canonical(x === undefined ? null : x)).join(",")}]`;
  if (v && typeof v === "object") {
    const o = v as Record<string, unknown>;
    return `{${Object.keys(o).filter((k) => o[k] !== undefined).sort().map((k) => `${JSON.stringify(k)}:${canonical(o[k])}`).join(",")}}`;
  }
  return JSON.stringify(v) ?? "null";
}
