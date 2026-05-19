// Best-effort redaction for strings that get posted to a public GitHub issue
// via feedback(submit). The agent assembles the body from session state, so
// we cannot rely on a human reviewer catching pasted credentials before they
// land in the issue body. This is a safety net, not a guarantee: novel token
// shapes will pass through. Tighten as new patterns show up in the wild.

interface SecretRule {
  name: string;
  re: RegExp;
}

const RULES: SecretRule[] = [
  // GitHub PATs and App tokens. All current prefixes are 4 chars + underscore.
  { name: "github-token", re: /\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{20,}\b/g },
  { name: "github-pat", re: /\bgithub_pat_[A-Za-z0-9_]{20,}\b/g },
  // AWS access key IDs (the secret is paired with these in env files).
  { name: "aws-access-key", re: /\b(?:AKIA|ASIA)[0-9A-Z]{16}\b/g },
  // Slack legacy + bot tokens.
  { name: "slack-token", re: /\bxox[abprs]-[0-9A-Za-z-]{10,}\b/g },
  // Stripe live/test secret keys.
  { name: "stripe-key", re: /\bsk_(?:live|test)_[A-Za-z0-9]{16,}\b/g },
  // Anthropic API keys.
  { name: "anthropic-key", re: /\bsk-ant-[A-Za-z0-9_-]{20,}\b/g },
  // OpenAI keys (broad - matches the visible prefix; intentionally narrow on
  // length to keep false positives manageable).
  { name: "openai-key", re: /\bsk-[A-Za-z0-9]{32,}\b/g },
  // JWTs (header.payload.signature).
  { name: "jwt", re: /\beyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\b/g },
  // PEM-encoded private keys.
  {
    name: "private-key",
    re: /-----BEGIN (?:RSA |EC |OPENSSH |DSA |PGP |ENCRYPTED |)?PRIVATE KEY-----[\s\S]*?-----END (?:RSA |EC |OPENSSH |DSA |PGP |ENCRYPTED |)?PRIVATE KEY-----/g,
  },
  // Generic key/value assignments where the key name screams "secret".
  // Captures up to the next whitespace/quote so multi-word secrets don't leak.
  {
    name: "env-style-secret",
    re: /\b(?:[A-Z][A-Z0-9_]*?_)?(?:SECRET|TOKEN|API[_-]?KEY|PASSWORD|PASSWD|PRIVATE[_-]?KEY|ACCESS[_-]?KEY|CLIENT[_-]?SECRET|AUTH[_-]?TOKEN|BEARER)[A-Z0-9_]*\s*[:=]\s*["']?([^"'\s]{6,})["']?/gi,
  },
];

export interface ScrubResult {
  text: string;
  hits: Array<{ rule: string; count: number }>;
}

export function scrubSecrets(input: string): ScrubResult {
  let text = input;
  const hits: Array<{ rule: string; count: number }> = [];
  for (const rule of RULES) {
    let count = 0;
    text = text.replace(rule.re, (match, ...args) => {
      count += 1;
      // For env-style assignments only the value group is sensitive; preserve
      // the key so reviewers can see which variable was redacted.
      if (rule.name === "env-style-secret") {
        const value = args[0] as string;
        const idx = match.lastIndexOf(value);
        return match.slice(0, idx) + "[REDACTED]";
      }
      return "[REDACTED]";
    });
    if (count > 0) hits.push({ rule: rule.name, count });
  }
  return { text, hits };
}
