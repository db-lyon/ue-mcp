/**
 * GitHub repository names and the URLs built from them: the core repository,
 * where a gap is filed by hand, and the parsing and comparison the feedback
 * router and the plugin registry both need.
 */

export interface GitHubRepo {
  owner: string;
  repo: string;
}

/** ue-mcp core. Everything that is not plugin-owned lands here. */
export const CORE_REPO: GitHubRepo = { owner: "db-lyon", repo: "ue-mcp" };
/** Where a user files a gap by hand, when the feedback category is disabled. */
export const CORE_ISSUES_URL = `https://github.com/${CORE_REPO.owner}/${CORE_REPO.repo}/issues`;

/** `https://github.com/owner/repo(.git)` -> `{owner, repo}`. */
export function parseGitHubRepo(url: string | null | undefined): GitHubRepo | null {
  if (!url) return null;
  const cleaned = url
    .trim()
    .replace(/^git\+/, "")
    .replace(/\.git$/, "")
    .replace(/^git@github\.com:/, "https://github.com/");
  const m = cleaned.match(/github\.com[/:]([\w.-]+)\/([\w.-]+)/i);
  if (!m) return null;
  return { owner: m[1], repo: m[2] };
}

/** `owner/name` for display and comparison. */
export function repoSlug(r: GitHubRepo): string {
  return `${r.owner}/${r.repo}`;
}

export function sameRepo(a: GitHubRepo | null, b: GitHubRepo | null): boolean {
  if (!a || !b) return false;
  return a.owner.toLowerCase() === b.owner.toLowerCase() && a.repo.toLowerCase() === b.repo.toLowerCase();
}

/** Parse an `owner/name` string. Returns null for anything else. */
export function parseRepoSlug(s: string | null | undefined): GitHubRepo | null {
  if (!s) return null;
  const m = s.trim().match(/^([\w.-]+)\/([\w.-]+)$/);
  return m ? { owner: m[1], repo: m[2] } : null;
}

/**
 * Prefilled "new issue" URL. The escape hatch when the API post is refused
 * (issues disabled, private repo, bot not installed there) - the user can
 * still land the report in one click with the body already written.
 */
export function newIssueUrl(repo: GitHubRepo, title: string, body: string): string {
  // GitHub truncates very long query strings and some browsers cap the URL at
  // ~8KB. Keep the body well under that; the user can paste the rest.
  const MaxBody = 5000;
  const trimmed = body.length > MaxBody ? `${body.slice(0, MaxBody)}\n\n_(truncated - see the full report in your agent transcript)_` : body;
  const qs = `title=${encodeURIComponent(title)}&body=${encodeURIComponent(trimmed)}`;
  return `https://github.com/${repo.owner}/${repo.repo}/issues/new?${qs}`;
}
