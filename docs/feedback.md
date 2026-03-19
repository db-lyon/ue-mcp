# Feedback

UE-MCP includes a built-in feedback system that helps improve tool coverage over time. When an AI agent has to fall back to `editor(action="execute_python")` because a native tool couldn't handle the task, it can submit structured feedback directly as a GitHub issue.

## How It Works

```mermaid
flowchart LR
    Agent[AI Agent] -->|notices tool gap| FT[feedback tool]
    FT -->|GitHub App auth| GH[GitHub Issues]
    GH -->|maintainers triage| Fix[New native tool/action]
```

1. During a session, the agent uses `editor(action="execute_python")` as a workaround for something a native tool should handle
2. When the task is complete, the agent asks: *"I had to use custom Python scripts to get this done. Would you like to submit feedback to improve ue-mcp?"*
3. If the user agrees, the agent calls `feedback(action="submit")` with details about the gap
4. A GitHub issue is created automatically on the [ue-mcp repository](https://github.com/db-lyon/ue-mcp)

## Privacy

The agent is instructed to **strip project-specific details** from feedback submissions. Issues should describe the general capability gap, not your project's internals. You can review the issue content before the agent submits it.

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
