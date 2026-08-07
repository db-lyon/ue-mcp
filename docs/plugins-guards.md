# Guards

A plugin can gate **every** bridge call - not just its own actions - with a guard. Guards run in an ordered pipeline around `IBridge.call`, in the shape of NestJS guards/interceptors: a `before` hook may veto a call (deny) or act on it (e.g. check a file out of source control), and an `after` hook may observe or replace the result (audit, transform). The pipeline is agnostic - source control, access policy, audit, and rate limiting are all just guards.

## Registering a guard

You register a guard by declaring a task whose name matches `guard.<name>.<phase>`. No new manifest section is needed - the loader already registers every `tasks:` entry by name, and the server discovers `guard.*` tasks at boot. With no guard registered the pipeline is a pass-through, so installing a guard plugin is the only thing that changes behavior.

| Phase | Runs |
|-------|------|
| `before` | before every call; return `success: false` (or throw) to **deny** it |
| `beforeWrite` | before a call only when it resolves to existing on-disk files it will modify (reads pay nothing) |
| `after` | after every successful call (side effects like audit) |
| `afterWrite` | after a write-classified call |

Prefer `beforeWrite`/`afterWrite` for anything write-oriented so reads are never gated. Use the plain `before`/`after` phases for cross-cutting concerns that apply to every call, like audit or rate limiting.

The guard task is invoked with `{ method, params, paths }` (`paths` = the existing on-disk files the call will touch, empty for non-writes) plus `result` for `after` phases.

The phase name has to be one of the four. A task named `guard.policy.beforeWrites` fails the server at boot with the list of valid phases, rather than registering a guard that never runs. A guard that silently does nothing is the worst outcome for the thing standing between an agent and a mutation, so the typo is worth a loud failure.

## A deny guard (access policy)

Blocks writes outside a sandbox path. A `before`/`beforeWrite` guard denies by returning `success: false` (or throwing); the underlying call never runs and the agent gets a `WRITE_BLOCKED` error carrying your message.

```yaml
# ue-mcp.plugin.yml
tasks:
  guard.policy.beforeWrite:
    class_path: tasks/PolicyGuard
    description: "Deny writes outside /Game/Sandbox"
```

```ts
// tasks/PolicyGuard.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";

export default class PolicyGuard extends UeMcpTask<{ paths?: string[]; method?: string }> {
  get taskName() { return "guard.policy.beforeWrite"; }
  async execute(): Promise<TaskResult> {
    const outside = (this.options.paths ?? []).filter((p) => !p.includes("/Content/Sandbox/"));
    if (outside.length) {
      return { success: false, error: new Error(`writes outside the sandbox are not allowed: ${outside.join(", ")}`) };
    }
    return { success: true };
  }
}
```

## An observe guard (audit)

Runs after every successful call for its side effect. An `after` guard's failure is logged but does not fail the already-completed call - it is for observation, not veto.

```yaml
tasks:
  guard.audit.after:
    class_path: tasks/AuditGuard
    description: "Append every action to an audit log"
```

```ts
// tasks/AuditGuard.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";
import { appendFileSync } from "node:fs";

export default class AuditGuard extends UeMcpTask<{ method?: string; paths?: string[] }> {
  get taskName() { return "guard.audit.after"; }
  async execute(): Promise<TaskResult> {
    const line = JSON.stringify({ method: this.options.method, paths: this.options.paths });
    appendFileSync("ue-mcp-audit.log", line + "\n");
    return { success: true };
  }
}
```

## Composition and ordering

Multiple guards compose into one pipeline. `before` hooks run in order, `after` hooks in reverse order (the same nesting an interceptor stack gives you). A guard that denies short-circuits the rest of the `before` chain and the call itself.

Guards from different plugins coexist - a source-control checkout guard, an access-policy deny guard, and an audit guard can all be installed at once, each shipped by whatever plugin owns that concern. The [`ue-mcp-perforce`](https://github.com/db-lyon/ue-mcp-perforce) plugin's `guard.sourcecontrol.beforeWrite` is the canonical example: it checks the target files out (or refuses when a human holds the lock) before the write reaches the editor.
