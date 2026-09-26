import * as fs from "node:fs";
import * as path from "node:path";
import { randomBytes } from "node:crypto";
import { userDir } from "./user-dir.js";

/**
 * Storage for `feedback.mode = "defer"` submissions. The server writes the
 * fully-scrubbed payload (title + body + labels + author) to disk so a human
 * can review, approve, or discard later via `npx ue-mcp feedback ...`.
 *
 * Location: ~/.ue-mcp/pending-feedback/<sortable-id>.json
 *
 * User-scoped (not project-scoped) because feedback issues file against the
 * shared ue-mcp tracker, not against the user's project. The recorded
 * `project` field on each entry remembers which project originated the
 * submission so the reviewer has context.
 */

/** Resolved per call so tests (and runtime overrides) can change the env var
 *  between operations without re-importing the module. */
function pendingDir(): string {
  return (
    process.env.UE_MCP_PENDING_DIR ||
    path.join(userDir(), "pending-feedback")
  );
}

export interface DeferredFeedback {
  /** Stable id derived from timestamp + random suffix; matches the filename. */
  id: string;
  /** ISO-8601 timestamp captured at defer time. */
  createdAt: string;
  /** Project name that originated the submission, or null if none was loaded. */
  project: string | null;
  /** Scrubbed issue title. */
  title: string;
  /** Scrubbed issue body. */
  body: string;
  /** Inferred GitHub labels. */
  labels: string[];
  /** Authorship intent at defer time. Honored when the deferred entry is
   *  later approved via the CLI. */
  author: "user" | "bot";
  /** Target tracker as `owner/name`. Absent on entries written before
   *  plugin routing existed; those approve against ue-mcp core. */
  repo?: string;
  /** One line on why the entry is aimed at that tracker, shown by
   *  `feedback show`/`review` so a human can sanity-check the routing. */
  routing?: string;
  /** Short token a human can read out of the accompanying `<id>.md` and hand
   *  back to the agent to authorize posting these exact bytes (#991). Only
   *  written by the elicitation fallback path; deferred entries have none. */
  confirmToken?: string;
  /** Why the elicitation gate could not be reached, when this entry exists
   *  because of that rather than because the user chose defer mode. */
  blockedReason?: string;
}

function ensureDir(): void {
  const dir = pendingDir();
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
}

function generateId(now: Date): string {
  // Sortable: YYYYMMDDTHHmmssSSS-<6 hex>. Millisecond precision so two
  // entries created back-to-back in the same second still sort by
  // insertion order before the random suffix tiebreak kicks in.
  const pad = (n: number, w = 2) => String(n).padStart(w, "0");
  const ts = `${now.getUTCFullYear()}${pad(now.getUTCMonth() + 1)}${pad(now.getUTCDate())}T${pad(now.getUTCHours())}${pad(now.getUTCMinutes())}${pad(now.getUTCSeconds())}${pad(now.getUTCMilliseconds(), 3)}`;
  const rand = randomBytes(3).toString("hex");
  return `${ts}-${rand}`;
}

export function deferSubmission(
  payload: {
    title: string;
    body: string;
    labels: string[];
    repo?: string;
    routing?: string;
    confirmToken?: string;
    blockedReason?: string;
  },
  project: string | null,
  author: "user" | "bot",
): DeferredFeedback {
  ensureDir();
  const now = new Date();
  const id = generateId(now);
  const entry: DeferredFeedback = {
    id,
    createdAt: now.toISOString(),
    project,
    title: payload.title,
    body: payload.body,
    labels: payload.labels,
    author,
    ...(payload.repo ? { repo: payload.repo } : {}),
    ...(payload.routing ? { routing: payload.routing } : {}),
    ...(payload.confirmToken ? { confirmToken: payload.confirmToken } : {}),
    ...(payload.blockedReason ? { blockedReason: payload.blockedReason } : {}),
  };
  const filePath = path.join(pendingDir(), `${id}.json`);
  fs.writeFileSync(filePath, JSON.stringify(entry, null, 2), { mode: 0o600 });
  return entry;
}

export function listDeferred(): DeferredFeedback[] {
  const dir = pendingDir();
  if (!fs.existsSync(dir)) return [];
  const entries: DeferredFeedback[] = [];
  for (const name of fs.readdirSync(dir).sort()) {
    if (!name.endsWith(".json")) continue;
    try {
      const raw = fs.readFileSync(path.join(dir, name), "utf-8");
      const parsed = JSON.parse(raw) as DeferredFeedback;
      entries.push(parsed);
    } catch {
      // Skip unreadable / malformed entries silently. They stay on disk for
      // the user to inspect manually if they care.
    }
  }
  return entries;
}

export function loadDeferred(id: string): DeferredFeedback | null {
  const filePath = path.join(pendingDir(), `${id}.json`);
  if (!fs.existsSync(filePath)) return null;
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf-8")) as DeferredFeedback;
  } catch {
    return null;
  }
}

export function deleteDeferred(id: string): boolean {
  const filePath = path.join(pendingDir(), `${id}.json`);
  if (!fs.existsSync(filePath)) return false;
  fs.unlinkSync(filePath);
  return true;
}

export function getPendingDir(): string {
  return pendingDir();
}
