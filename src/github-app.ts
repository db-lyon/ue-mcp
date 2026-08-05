import { resolveUserAuth, clearUserAuth, type PendingDeviceFlow } from "./auth.js";
import { CORE_REPO, registryBase, repoSlug, sameRepo, type GitHubRepo } from "./registry-catalog.js";

/**
 * Hosted signing endpoint for the anonymous bot path.
 *
 * This package holds no GitHub App credential. Anonymous reports are POSTed as
 * plain JSON to the endpoint, which holds the App key in server-side secrets,
 * mints a short-lived installation token, and opens the issue. Overridable so a
 * self-hosted registry (or a local one during development) can serve it.
 */
function signingEndpoint(): string {
  const override = process.env.UE_MCP_FEEDBACK_ENDPOINT?.trim();
  if (override) return override.replace(/\/+$/, "");
  return `${registryBase()}/api/feedback`;
}

/** The signing endpoint should answer in well under this; a slow one is a dead one. */
const SIGNING_TIMEOUT_MS = 20_000;

/**
 * A report can be filed against a plugin's own tracker instead of core - see
 * src/feedback-routing.ts. Everything below takes the target repo as a
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
 * Anonymous bot path.
 *
 * No credential is involved on this side: the body goes to the hosted signing
 * endpoint, which holds the GitHub App key and opens the issue. Every failure
 * is an outcome the caller can act on (file as the user, or open the prefilled
 * URL), so the only thing thrown here is a genuinely unexpected response shape.
 */
async function submitAsBot(
  title: string,
  body: string,
  labels: string[],
  repo: GitHubRepo,
): Promise<SubmitResult> {
  const endpoint = signingEndpoint();
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
    return {
      kind: "bot_unavailable",
      code: "unreachable",
      message: `Could not reach the feedback signing service at ${endpoint} (${e instanceof Error ? e.message : String(e)}).`,
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
      kind: "submitted",
      url: payload.url,
      number: payload.number,
      authoredBy: payload.authoredBy ?? "ue-mcp-feedback[bot]",
      authoredAs: "bot",
      repo: payload.repo ?? repoSlug(repo),
    };
  }

  // The tracker said no. Same outcome as a direct refusal used to be, so the
  // callers' existing recovery (prefilled URL, or re-file on core) applies.
  if (payload.code === "repo_unavailable" || payload.code === "repo_not_allowed") {
    return {
      kind: "repo_unavailable",
      repo: repoSlug(repo),
      status: payload.status ?? res.status,
      message: (payload.error ?? "The tracker refused the issue.").slice(0, 300),
    };
  }

  if (res.status === 503 || payload.code === "signing_not_configured") {
    return {
      kind: "bot_unavailable",
      code: "not_configured",
      message: payload.error ?? "Anonymous feedback signing is not enabled on this deployment.",
    };
  }

  // A bare 404 is the endpoint not existing at this origin, not a tracker
  // saying no. Report it as the anonymous path being off.
  if (res.status === 404 && !payload.code) {
    return {
      kind: "bot_unavailable",
      code: "not_configured",
      message: `No feedback signing service at ${endpoint}.`,
    };
  }

  if (res.status === 429 || payload.code === "rate_limited") {
    return {
      kind: "bot_unavailable",
      code: "rate_limited",
      message: payload.error ?? "Too many anonymous submissions from here recently.",
      retryAfter: payload.retry_after ?? (Number(res.headers.get("retry-after")) || undefined),
    };
  }

  return {
    kind: "bot_unavailable",
    code: "rejected",
    message: `${payload.error ?? "The feedback signing service rejected the submission."} (HTTP ${res.status})`,
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
