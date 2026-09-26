import { readEnv } from "../core/env.js";
import { resolveUserAuth, clearUserAuth, type PendingDeviceFlow } from "./github-auth.js";
import {
  CORE_REPO,
  feedbackBase,
  registryBase,
  repoSlug,
  sameRepo,
  type GitHubRepo,
} from "../extensions/registry-catalog.js";

/**
 * Hosted signing endpoints for the anonymous bot path, in the order they are
 * tried.
 *
 * This package holds no GitHub App credential. Anonymous reports are POSTed as
 * plain JSON to the endpoint, which holds the App key in server-side secrets,
 * mints a short-lived installation token, and opens the issue.
 *
 * Feedback has its own public name, `feedback.ue-mcp.com`, so it is no longer
 * addressed through the plugin registry's hostname. The registry path stays as
 * a fallback for two reasons: a deployment older than that name, or self-hosted,
 * still answers there, and while DNS for the new name propagates the anonymous
 * path keeps working instead of going dark.
 *
 * Overrides:
 *   UE_MCP_FEEDBACK_ENDPOINT  full URL. Exact, and the only candidate: an
 *                             operator who named a URL means that URL, and
 *                             silently posting somewhere else would be worse
 *                             than failing.
 *   UE_MCP_FEEDBACK           origin only, root path.
 *   UE_MCP_REGISTRY           when set without UE_MCP_FEEDBACK, that self-hosted
 *                             origin is tried first, since its operator meant
 *                             their deployment, not the public one.
 */
function signingEndpoints(): string[] {
  const override = readEnv("feedbackEndpoint")?.trim();
  if (override) return [override.replace(/\/+$/, "")];

  const hosted = `${feedbackBase()}/`;
  const viaRegistry = `${registryBase()}/api/feedback`;
  const selfHostedRegistry = Boolean(readEnv("registry")?.trim());
  const ordered = selfHostedRegistry ? [viaRegistry, hosted] : [hosted, viaRegistry];
  return ordered.filter((url, i) => ordered.indexOf(url) === i);
}

/** The signing endpoint should answer in well under this; a slow one is a dead one. */
const SIGNING_TIMEOUT_MS = 20_000;

/**
 * A report can be filed against a plugin's own tracker instead of core - see
 * src/feedback/feedback-routing.ts. Everything below takes the target repo as a
 * parameter and defaults to core, so existing callers are unaffected.
 */
function issuesEndpoint(repo: GitHubRepo): string {
  return `https://api.github.com/repos/${repo.owner}/${repo.repo}/issues`;
}

/**
 * Statuses that mean "this tracker will not take the issue" rather than
 * "something went wrong". Only meaningful off-core: the core tracker is known
 * good, so a failure there is a real error worth throwing.
 *
 *   403 - the token cannot open issues here
 *   404 - repo missing, renamed, or private to this token
 *   410 - issues are disabled on the repo
 */
const REPO_REFUSED_STATUSES = new Set([403, 404, 410]);

export type SubmitResult =
  | {
      kind: "submitted";
      url: string;
      number: number;
      authoredBy: string;
      authoredAs: "user" | "bot";
      repo: string;
    }
  | {
      kind: "auth_required";
      verification_uri: string;
      user_code: string;
      expires_in: number;
    }
  | {
      /** A tracker refused the post: issues disabled, private, or bot absent. */
      kind: "repo_unavailable";
      repo: string;
      status: number;
      message: string;
    }
  | {
      /**
       * The anonymous path itself is off: no signing endpoint reachable, none
       * configured on the deployment, or the caller is rate limited. The report
       * is intact and can still be filed as the user or opened manually.
       */
      kind: "bot_unavailable";
      code: "unreachable" | "not_configured" | "rate_limited" | "rejected";
      message: string;
      /** Seconds to wait, when the endpoint said "later". */
      retryAfter?: number;
    };

interface SigningResponse {
  ok?: boolean;
  url?: string;
  number?: number;
  repo?: string;
  authoredBy?: string;
  error?: string;
  code?: string;
  status?: number;
  retry_after?: number;
}

/**
 * One attempt against one endpoint.
 *
 * `endpointMissing` separates "there is no signing service at this URL" from
 * "the signing service answered". Only the first is worth retrying elsewhere:
 * a service that replied not-configured, rate-limited, or refused-destination
 * has given a real answer, and asking a different host the same question would
 * either duplicate the report or contradict the operator's intent.
 */
interface SigningAttempt {
  result: SubmitResult;
  endpointMissing: boolean;
}

/**
 * Anonymous bot path.
 *
 * No credential is involved on this side: the body goes to the hosted signing
 * endpoint, which holds the GitHub App key and opens the issue. Every failure
 * is an outcome the caller can act on (file as the user, or open the prefilled
 * URL), so the only thing thrown here is a genuinely unexpected response shape.
 */
async function attemptSigning(
  endpoint: string,
  title: string,
  body: string,
  labels: string[],
  repo: GitHubRepo,
): Promise<SigningAttempt> {
  let res: Response;
  try {
    res = await fetch(endpoint, {
      method: "POST",
      headers: {
        Accept: "application/json",
        "Content-Type": "application/json",
        "User-Agent": "ue-mcp",
      },
      body: JSON.stringify({ title, body, labels, repo: repoSlug(repo) }),
      signal: AbortSignal.timeout(SIGNING_TIMEOUT_MS),
    });
  } catch (e) {
    // DNS, TLS, timeout: nothing answered, so another origin is worth a try.
    return {
      endpointMissing: true,
      result: {
        kind: "bot_unavailable",
        code: "unreachable",
        message: `Could not reach the feedback signing service at ${endpoint} (${e instanceof Error ? e.message : String(e)}).`,
      },
    };
  }

  let payload: SigningResponse = {};
  try {
    payload = (await res.json()) as SigningResponse;
  } catch {
    // Non-JSON body: a proxy error page, or an endpoint that does not exist.
  }

  if (res.ok && payload.url && typeof payload.number === "number") {
    return {
      endpointMissing: false,
      result: {
        kind: "submitted",
        url: payload.url,
        number: payload.number,
        authoredBy: payload.authoredBy ?? "ue-mcp-feedback[bot]",
        authoredAs: "bot",
        repo: payload.repo ?? repoSlug(repo),
      },
    };
  }

  // The tracker said no. Same outcome as a direct refusal used to be, so the
  // callers' existing recovery (prefilled URL, or re-file on core) applies.
  if (payload.code === "repo_unavailable" || payload.code === "repo_not_allowed") {
    return {
      endpointMissing: false,
      result: {
        kind: "repo_unavailable",
        repo: repoSlug(repo),
        status: payload.status ?? res.status,
        message: (payload.error ?? "The tracker refused the issue.").slice(0, 300),
      },
    };
  }

  if (payload.code === "signing_not_configured") {
    return {
      endpointMissing: false,
      result: {
        kind: "bot_unavailable",
        code: "not_configured",
        message: payload.error ?? "Anonymous feedback signing is not enabled on this deployment.",
      },
    };
  }

  if (res.status === 429 || payload.code === "rate_limited") {
    return {
      endpointMissing: false,
      result: {
        kind: "bot_unavailable",
        code: "rate_limited",
        message: payload.error ?? "Too many anonymous submissions from here recently.",
        retryAfter: payload.retry_after ?? (Number(res.headers.get("retry-after")) || undefined),
      },
    };
  }

  /**
   * Nothing recognisable came back. A 404 or 405 is the endpoint not existing
   * at this origin, and a 5xx with no code of ours is a proxy or a cold
   * deployment answering instead of the handler. Neither is a tracker saying
   * no, so report the anonymous path as off and let the caller try the next
   * origin.
   */
  if (!payload.code && (res.status === 404 || res.status === 405 || res.status >= 500)) {
    return {
      endpointMissing: true,
      result: {
        kind: "bot_unavailable",
        code: "not_configured",
        message: `No feedback signing service at ${endpoint}.`,
      },
    };
  }

  return {
    endpointMissing: false,
    result: {
      kind: "bot_unavailable",
      code: "rejected",
      message: `${payload.error ?? "The feedback signing service rejected the submission."} (HTTP ${res.status})`,
    },
  };
}

/**
 * Try each known signing origin until one of them actually answers.
 *
 * The report itself is never at risk here: every branch below is an outcome the
 * caller can act on, so an offline service ends with a prefilled issue URL in
 * the user's hands rather than an exception.
 */
async function submitAsBot(
  title: string,
  body: string,
  labels: string[],
  repo: GitHubRepo,
): Promise<SubmitResult> {
  const endpoints = signingEndpoints();
  const missed: SubmitResult[] = [];

  for (const endpoint of endpoints) {
    const attempt = await attemptSigning(endpoint, title, body, labels, repo);
    if (!attempt.endpointMissing) return attempt.result;
    missed.push(attempt.result);
  }

  // One candidate: keep the original, more specific outcome.
  if (missed.length === 1) return missed[0];

  // Every origin came up empty. Name them all, because the usual cause is a
  // host that has not resolved yet and the user can see that from the message.
  const allUnreachable = missed.every((r) => r.kind === "bot_unavailable" && r.code === "unreachable");
  return {
    kind: "bot_unavailable",
    code: allUnreachable ? "unreachable" : "not_configured",
    message: `No feedback signing service answered at ${endpoints.join(" or ")}.`,
  };
}

function pendingResult(pending: PendingDeviceFlow): SubmitResult {
  return {
    kind: "auth_required",
    verification_uri: pending.verification_uri,
    user_code: pending.user_code,
    expires_in: Math.max(0, pending.expires_at - Math.floor(Date.now() / 1000)),
  };
}

export async function submitFeedback(
  title: string,
  body: string,
  labels: string[] = ["agent-feedback"],
  options: { useBot?: boolean; repo?: GitHubRepo } = {},
): Promise<SubmitResult> {
  const repo = options.repo ?? CORE_REPO;
  const repoName = `${repo.owner}/${repo.repo}`;

  if (options.useBot) {
    return submitAsBot(title, body, labels, repo);
  }

  const auth = await resolveUserAuth();
  if (auth.kind === "pending") {
    return pendingResult(auth.pending);
  }

  const post = (token: string) =>
    fetch(issuesEndpoint(repo), {
      method: "POST",
      headers: {
        Authorization: `token ${token}`,
        Accept: "application/vnd.github+json",
        "Content-Type": "application/json",
        "User-Agent": "ue-mcp",
      },
      body: JSON.stringify({ title, body, labels }),
    });

  const res = await post(auth.auth.token);

  if (res.status === 401) {
    // Token revoked or expired. Wipe and re-initiate device flow on the next
    // call so the user gets a fresh code instead of a silent bot fallback.
    await clearUserAuth();
    const retry = await resolveUserAuth();
    if (retry.kind === "pending") return pendingResult(retry.pending);
    // Fresh auth landed somehow - fall through to retry the post.
    const res2 = await post(retry.auth.token);
    if (!res2.ok) {
      const text = await res2.text();
      if (!sameRepo(repo, CORE_REPO) && REPO_REFUSED_STATUSES.has(res2.status)) {
        return { kind: "repo_unavailable", repo: repoName, status: res2.status, message: text.slice(0, 300) };
      }
      throw new Error(`Failed to create issue as user (after re-auth): ${res2.status} ${text}`);
    }
    const issue2 = (await res2.json()) as { html_url: string; number: number };
    return {
      kind: "submitted",
      url: issue2.html_url,
      number: issue2.number,
      authoredBy: retry.auth.login,
      authoredAs: "user",
      repo: repoName,
    };
  }

  if (!res.ok) {
    const text = await res.text();
    if (!sameRepo(repo, CORE_REPO) && REPO_REFUSED_STATUSES.has(res.status)) {
      return { kind: "repo_unavailable", repo: repoName, status: res.status, message: text.slice(0, 300) };
    }
    throw new Error(`Failed to create issue as user: ${res.status} ${text}`);
  }

  const issue = (await res.json()) as { html_url: string; number: number };
  return {
    kind: "submitted",
    url: issue.html_url,
    number: issue.number,
    authoredBy: auth.auth.login,
    authoredAs: "user",
    repo: repoName,
  };
}
