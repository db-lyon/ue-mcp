# Feedback

UE-MCP includes a built-in feedback system that helps improve tool coverage over time. When an AI agent has to fall back to `editor(action="execute_python")` because a native tool couldn't handle the task, it can submit structured feedback directly as a GitHub issue.

## How It Works

```mermaid
flowchart LR
    Agent[AI Agent] -->|notices tool gap| FT[feedback tool]
    FT -->|OAuth device flow| GH[GitHub Issues as you]
    GH -->|maintainers triage| Fix[New native tool/action]
```

1. During a session, the agent uses `editor(action="execute_python")` as a workaround for something a native tool should handle
2. When the task is complete, the agent asks: *"I had to use custom Python scripts to get this done. Would you like to submit feedback to improve ue-mcp?"*
3. If the user agrees, the agent calls `feedback(action="submit")` with details about the gap
4. **First time only**: ue-mcp surfaces a GitHub device flow URL + code. You authorize the `ue-mcp-feedback` app once, the token persists in `~/.ue-mcp/auth.json`, and every subsequent submission authors the issue as you.
5. A GitHub issue is created on the [ue-mcp repository](https://github.com/db-lyon/ue-mcp), authored by your real GitHub user

## Authorship

By default issues are authored as the actual reporter via OAuth device flow. The first `feedback(submit)` on a fresh machine returns a directive with a verification URL and one-time code; after you authorize the `ue-mcp-feedback` GitHub App, the access token is cached at `~/.ue-mcp/auth.json` (mode 600) and reused.

If you'd rather submit anonymously as the bot, pass `useBot=true`. The bot is the fallback, not the default.

## Privacy

The agent is instructed to **strip project-specific details** from feedback submissions. Issues should describe the general capability gap, not your project's internals. You can review the issue content before the agent submits it.

## Security model

`feedback(submit)` posts to a public issue tracker. Treat anything you let the agent submit as already-disclosed. Specific notes:

- **Secret scrubbing is best-effort.** Before submit, the assembled body and title are scanned for common credential shapes (GitHub PATs, AWS access keys, Slack/Stripe/OpenAI/Anthropic tokens, JWTs, PEM blocks, `SECRET=`/`TOKEN=`/`API_KEY=`/`PASSWORD=` style assignments) and replaced with `[REDACTED]`. Novel token shapes still pass through. Read the body before you let the agent post it.
- **The PostToolUse hook is opt-in.** `npx ue-mcp init` no longer pre-checks the "prompt agents to file a GitHub issue" box. If you opt in, the hook fires after every `execute_python` call and pushes the agent toward calling `feedback(submit)`; the workaround log it would submit includes the Python you ran. Leave it off if your session touches anything sensitive.
- **`useBot=true` ships an embedded GitHub App key.** The published npm package contains the `ue-mcp-feedback` App's PEM. The App's permissions are scoped to `issues: write` on `db-lyon/ue-mcp` and nowhere else, so the realistic blast radius from key disclosure is bot impersonation (spam-create, edit, or close issues on this one repo as `ue-mcp-feedback[bot]`); not RCE, not exfil of private code, not a foothold on your machine. The proper fix is server-side signing, tracked in [#461](https://github.com/db-lyon/ue-mcp/issues/461). In the meantime: the bot path is opt-in (default flow uses your device-auth token), the App's permissions stay locked, and revocation is one click on the App installation page.

## Submitting Feedback

The `feedback` tool has one action:

### `submit`

| Parameter | Required | Description |
|-----------|----------|-------------|
| `title` | Yes | Short title describing the tool gap (generic, no project details) |
| `summary` | Yes | What was attempted and why the native tool fell short |
| `pythonWorkaround` | No | The `execute_python` code used as a workaround |
| `idealTool` | No | What tool/action should handle this natively |

### Example

```
feedback(action="submit",
  title="Cannot set default values for Blueprint variables",
  summary="Tried to set a default value on a Blueprint variable. add_variable creates the variable but there's no action to set its default. Had to use execute_python to access the variable's DefaultValue property directly.",
  pythonWorkaround="import unreal; bp = unreal.load_asset('/Game/BP_Player'); ...",
  idealTool="blueprint(action='set_variable_default', assetPath, name, defaultValue)"
)
```

## For Maintainers

Feedback issues are created with the `agent-feedback` label and include:

- **Summary** — what the user was trying to do
- **Ideal Tool/Action** — suggested native tool signature
- **Python Workaround** — the code that solved it, useful for implementing the native handler

These issues form a prioritized backlog of tool gaps to close.

## Claude Code Hooks

If you ran `npx ue-mcp init` and selected "Agent behavior" hooks, Claude Code will automatically prompt agents to submit feedback whenever they fall back to `execute_python`. This is a Claude Code [PostToolUse hook](https://docs.anthropic.com/en/docs/claude-code/hooks) — it fires deterministically after the tool call, not as a suggestion the agent can ignore.

The hook is configured in your project's `.claude/settings.json` and calls `npx ue-mcp hook post-tool-use` under the hood.

## Resolving Feedback Issues

Once feedback issues are triaged, anyone can resolve them:

```bash
npx ue-mcp resolve <issue-number>
```

This fetches the issue, creates a branch, launches Claude Code to implement the fix, and opens a PR. See [Getting Started](getting-started.md#resolving-issues) for details.
