# Security Policy

## Reporting a vulnerability

Use GitHub's private vulnerability reporting:

[**Report a vulnerability**](https://github.com/db-lyon/ue-mcp/security/advisories/new)

That opens a private thread between you and the maintainer. Please do **not** file security issues on the public issue tracker.

### Filling in the form

GitHub's form is the same one used to draft the eventual public Security Advisory, so the section prompts read awkwardly for first-time reporters. Treat it like this:

- **Impact** — this is the only one you need to fill in. Describe what you found, the steps to reproduce, the version it affects (`ue-mcp@1.0.x`), and what you think the impact is (consent-gate bypass, credential leak, path escape, etc.). Include any proof-of-concept that helps.
- **Patches** — leave blank. This gets filled in by the maintainer once a fix is decided; it's for the eventual advisory's "upgrade to X.Y.Z" message.
- **Workarounds** — leave blank unless you've found one. Maintainer-fillable during triage.
- **References** — optional. Link related CVEs, prior reports, or relevant documentation if you have any. Skip if you don't.

A short, clear **Impact** with reliable reproduction steps is the most valuable thing you can give us. Everything else can be sorted out on the advisory thread.

## What's in scope

The maintained surface of this repo:

- The `ue-mcp` npm package (`src/`, the published `dist/`)
- The `UE_MCP_Bridge` C++ plugin in `plugin/`
- The build, release, and publish workflows in `.github/workflows/`
- The bundled installation credential and its decoder (`assets/installation.bin`, `src/manifest-signature.ts`) — note: the credential is intentionally scoped to `issues:write` on this repo, so issue spam on this repo alone is the documented blast radius. Reports demonstrating use beyond that scope are very much in scope.

Examples of in-scope findings:

- Bypass of the `feedback(submit)` consent gate (anything that lets an agent post to the public tracker without explicit user approval in `interactive` mode)
- Bypass of the credential or privacy scrub (`src/secret-scrub.ts`, `src/privacy-scrub.ts`)
- Leakage of identifiers the privacy scrub is documented to redact
- Path traversal, arbitrary write, or local privilege escalation through the deferred feedback queue, the hook installer, or the plugin loader
- Remote interaction with the WebSocket bridge (it's loopback-only by design; anything that reaches it from off-host is in scope)
- An MCP tool action that lets the agent escape the documented surface (e.g. read or write outside the project, exfiltrate the OAuth cache, etc.)
- Anything the documentation describes as "the agent cannot…" turning out to be possible

## What's out of scope

- Bugs or security issues in Unreal Engine itself
- Bugs or security issues in the AI client (Claude Code, Claude Desktop, Cursor, …)
- Issues in the user's UE project where ue-mcp is installed
- Issues that only manifest when the user has explicitly opted into a behavior they were warned about (e.g. `feedback.mode = "auto-approve"` posting without prompting — that's the documented trade-off, not a bug)
- Non-security correctness bugs — file those on the public tracker

## Response expectations

This is a side project maintained by one person. Best-effort targets:

- **Acknowledge**: within 7 days
- **Triage / initial assessment**: within 14 days
- **Fix or "won't fix" decision**: within 90 days for confirmed findings

These are not SLAs. If you've waited longer than the target and heard nothing, feel free to ping the advisory thread.

## Disclosure

Coordinated disclosure is preferred. By default reports are kept private until a fix has shipped to npm and the release is public, after which the advisory is promoted and a CVE requested if appropriate.

If you'd like credit in the release notes, say so on the advisory thread. Anonymous reports are welcome too.

## Out-of-band channels

There is no email security alias for this repo. Use the [private vulnerability reporting form](https://github.com/db-lyon/ue-mcp/security/advisories/new) above.
