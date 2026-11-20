// @session independent: Pure text redaction with no state.
/**
 * Credential redaction for log lines.
 *
 * Every line the server writes goes through `redactCredentials` in log.ts, so
 * a call site that interpolates a request, a header map or an error message
 * cannot put a credential in a log by accident. Logs are pasted into issues,
 * attached to crash reports and kept on disk by MCP clients and the daemon, so
 * a token that reaches one has left the machine as far as we can tell.
 *
 * The plugin applies the same rules to its handshake lines in
 * `UEMCPBridgeSessionToken::RedactCredentials` (BridgeSessionToken.cpp).
 * Change one and change the other; tests/unit/redact-credentials.test.ts runs
 * the shared cases against this side.
 */

export const REDACTED = "[REDACTED]";

/** Headers whose whole value is a credential. The scheme word, when there is one, is kept. */
const CREDENTIAL_HEADERS = ["authorization", "proxy-authorization", "x-ue-mcp-token", "cookie", "set-cookie"];

// "Authorization: Bearer abc", "authorization": "Bearer abc", authorization=abc.
// The value runs to the end of the line, a closing quote or a comma.
const HEADER_RE = new RegExp(
  String.raw`(["']?\b(?:${CREDENTIAL_HEADERS.join("|")})\b["']?\s*[:=]\s*["']?)([^"'\r\n,}]*)`,
  "gi",
);

// A bearer or basic credential wherever it appears, with or without a header name.
const SCHEME_RE = /\b(Bearer|Basic)(\s+)([A-Za-z0-9._~+/=-]{8,})/gi;

// token=..., "access_token": "...", api_key: ...: a key that names a secret, then its value.
const KEY_VALUE_RE =
  /((?:^|[^A-Za-z0-9])(?:[A-Za-z0-9]+[_-])*(?:token|secret|password|api[_-]?key)["']?\s*[:=]\s*["']?)([^\s"',;&}]{8,})/gi;

// "Bearer <token>" or "Bearer ...": usage text naming the shape, not a value.
const PLACEHOLDER_RE = /^(?:[A-Za-z]+\s+)?(?:<[^<>]*>|\.\.\.)$/;

/** Keep the scheme word of an Authorization value and redact what follows it. */
function redactHeaderValue(value: string): string {
  const trimmed = value.trim();
  if (trimmed.length === 0 || trimmed === REDACTED || PLACEHOLDER_RE.test(trimmed)) return value;
  const scheme = /^([A-Za-z][A-Za-z0-9-]*)\s+\S/.exec(trimmed);
  return scheme ? `${scheme[1]} ${REDACTED}` : REDACTED;
}

/**
 * The text with every credential replaced by `[REDACTED]`: Authorization and
 * token header values, bearer and basic credentials, and values assigned to a
 * key named like a secret. What surrounds a value stays, so a reader can still
 * see that a token was sent and where.
 */
export function redactCredentials(text: string): string {
  if (text.length === 0) return text;
  return text
    .replace(HEADER_RE, (_m, prefix: string, value: string) => prefix + redactHeaderValue(value))
    .replace(SCHEME_RE, (_m, scheme: string, gap: string) => `${scheme}${gap}${REDACTED}`)
    .replace(KEY_VALUE_RE, (_m, prefix: string) => prefix + REDACTED);
}
